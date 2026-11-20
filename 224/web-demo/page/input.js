// The input: what the page feeds the machine(s), shared by the page
// (index.html, app.js) and the comparison page (compare.html, compare.js).
//
// The sources are a click once a period (sounds/click.flac), a single-sample
// impulse (for measuring), a riff played on an SH-101
// (sounds/aphex-twin-tha-riff.flac, which loops), silence, a 440 Hz sine,
// noise bursts, the microphone or line in, and the visitor's own audio file
// (which loops). Both pages carry the same controls, by id:
//   #source (with #fileOption), #audioFile, #riffCredit (shown with the riff),
//   #period, #periodText, #level, #levelText, #muteInput, #audioStatus (where
//   a microphone error is shown).
//
// The samples themselves come from two plain functions without any DOM,
// inputSample() and advanceInput(), so tests/input_sources.mjs can check
// them in Node.
import { RATE } from './larc.js';

// What the sources need to remember between samples.
export function createSourceState() {
  return {
    phase: 0,          // the sine's phase
    cycle: 0,          // frames into the current repeat period (#period)
    fileData: null,    // {left, right, position}: the visitor's file, at RATE
    clickData: null,   // {left, right}: sounds/click.flac, at RATE, once it has loaded
    riffData: null,    // {left, right}: the riff, at RATE, once it has loaded (on first use)
  };
}

// One input sample for `channel` (0 left, 1 right). settings: {source, level
// (dB, #level), muted}; live: [left, right] of the microphone this frame.
export function inputSample(state, settings, channel, live) {
  if (settings.muted) return 0;     // let the tail ring out
  const level = 10 ** (settings.level / 20);
  const own = level / 2;            // the built-in sounds: 6 dB below clipping at 0 dB
  switch (settings.source) {
    case 'mic': return level * live[channel];
    case 'click': if (state.clickData) {           // until the file loads: the impulse
        const click = channel === 0 ? state.clickData.left : state.clickData.right;
        return state.cycle < click.length ? own * click[state.cycle] : 0;
      }
      return channel === 0 && state.cycle === 0 ? own : 0;
    case 'impulse': return channel === 0 && state.cycle === 0 ? own : 0;
    case 'sine': return own * Math.sin(state.phase);
    case 'noise': return state.cycle < 4800 ? own * (Math.random() * 2 - 1) : 0;   // a 100 ms burst
    case 'riff': if (!state.riffData || state.cycle >= state.riffData.left.length) return 0;       // silent until it loads
      return level * (channel === 0 ? state.riffData.left : state.riffData.right)[state.cycle];
    case 'file': if (!state.fileData || state.cycle >= state.fileData.left.length) return 0;       // silence after its end
      return level * (channel === 0 ? state.fileData.left : state.fileData.right)[state.cycle];
    default: return 0;
  }
}

// Move on one frame. period: the repeat period in seconds (#period).
export function advanceInput(state, period) {
  state.phase += 2 * Math.PI * 440 / RATE;
  // The click, the impulse, the noise burst, the riff and the file start again every period.
  state.cycle = (state.cycle + 1) % Math.max(1, Math.round(period * RATE));
}

// Wire the controls. context(): the page's AudioContext (made on first use).
// Returns {fill, attach, toggleMute}.
export function createInput(context) {
  const $ = (id) => document.getElementById(id);
  const state = createSourceState();
  let node = null;          // the page's ScriptProcessor while audio runs
  let mic = null;           // {stream, source} while the microphone is the input
  let inputMuted = false;
  let lastSource = $('source').value;
  let periodBeforeRiff = null;   // the repeat period to go back to when the riff stops

  $('audioFile').onchange = async (event) => {
    const file = event.target.files[0];
    if (!file) return;
    const buffer = await context().decodeAudioData(await file.arrayBuffer());
    state.fileData = { left: buffer.getChannelData(0), right: buffer.getChannelData(buffer.numberOfChannels > 1 ? 1 : 0), position: 0 };
    showFileName(file.name);
    setPeriod(buffer.duration);
    setSource('file');   // (which releases the microphone if it was on)
  };
  // The click: a short recorded click, decoded once (at RATE, by an offline
  // context, so no live context is created before the visitor starts audio).
  (async () => {
    try {
      const response = await fetch('sounds/click.flac');
      if (!response.ok) return;
      const buffer = await new OfflineAudioContext(2, 1, RATE).decodeAudioData(await response.arrayBuffer());
      state.clickData = { left: buffer.getChannelData(0), right: buffer.getChannelData(buffer.numberOfChannels > 1 ? 1 : 0) };
    } catch (e) {
      console.warn('no click sound, using the impulse:', e);
    }
  })();
  // The riff: about 1 MB, so it is fetched the first time it is chosen.
  let riffLoading = null;
  function loadRiff() {
    riffLoading ??= (async () => {
      try {
        const response = await fetch('sounds/aphex-twin-tha-riff.flac');
        if (!response.ok) throw new Error(`HTTP ${response.status}`);
        const buffer = await new OfflineAudioContext(2, 1, RATE).decodeAudioData(await response.arrayBuffer());
        state.riffData = { left: buffer.getChannelData(0), right: buffer.getChannelData(buffer.numberOfChannels > 1 ? 1 : 0) };
      } catch (e) {
        $('audioStatus').textContent = `no riff: ${e.message}`;
        riffLoading = null;
      }
    })();
    return riffLoading;
  }

  // If the server offers a sound (serve.py --sound), it becomes the default input.
  (async () => {
    try {
      const response = await fetch('sound');
      if (!response.ok) return;
      // An offline context decodes (and resamples to RATE) without creating the
      // live one: a live context made before a click starts suspended.
      const decoder = new OfflineAudioContext(2, 1, RATE);
      const buffer = await decoder.decodeAudioData(await response.arrayBuffer());
      state.fileData = { left: buffer.getChannelData(0), right: buffer.getChannelData(buffer.numberOfChannels > 1 ? 1 : 0), position: 0 };
      showFileName('the default sound');
      setPeriod(buffer.duration);
      setSource('file');
    } catch (e) {
      console.warn('no default sound:', e);
    }
  })();

  // Mute input: a toggle button (and the M key, handled by the page).
  function toggleMute() {
    inputMuted = !inputMuted;
    $('muteInput').setAttribute('aria-pressed', String(inputMuted));
    $('muteInput').textContent = inputMuted ? 'Input muted' : 'Mute input';
  }
  $('muteInput').onclick = toggleMute;

  // The input: the dropdown's built-in sources, or the visitor's own file
  // (its entry opens the file picker, then shows the file's name).
  function source() {
    return $('source').value;
  }
  function setSource(value) {
    $('source').value = value;
    onSourceChange();
  }
  $('source').onchange = () => {
    if (source() === 'another' || (source() === 'file' && !state.fileData)) {
      $('audioFile').click();          // chosen: audioFile.onchange; cancelled: back to the last one
      $('source').value = lastSource;
      return;
    }
    onSourceChange();
  };
  // The repeat period: a file's own length loops it seamlessly; sine and the
  // microphone are continuous.
  function setPeriod(seconds) {
    if (seconds > +$('period').max) $('period').max = String(Math.ceil(seconds));   // a long file
    $('period').value = String(seconds);
    showPeriodText();
  }
  function showPeriodText() {
    $('periodText').textContent = `${(+$('period').value).toFixed(2)} s`;
  }
  $('period').oninput = showPeriodText;
  $('level').oninput = () => {
    const db = +$('level').value;
    $('levelText').textContent = `${db > 0 ? '+' : ''}${db} dB`;
  };
  function showPeriod() {
    $('period').disabled = source() === 'sine' || source() === 'mic' || source() === 'silence';
  }

  function showFileName(name) {
    $('fileOption').textContent = `file: ${name} (loops)`;
    if (![...$('source').options].some((o) => o.value === 'another')) $('source').add(new Option('another audio file…', 'another'));
  }

  // The microphone (or a line input): raw, without the browser's voice processing.
  async function onSourceChange() {
    lastSource = source();
    $('riffCredit').hidden = source() !== 'riff';
    // The riff loops at its own length; the other sources get their period back.
    if (source() === 'riff') {
      periodBeforeRiff ??= +$('period').value;
      await loadRiff();
      if (state.riffData && source() === 'riff') setPeriod(state.riffData.left.length / RATE);
    } else if (periodBeforeRiff !== null) {
      if (source() !== 'file') setPeriod(periodBeforeRiff);
      periodBeforeRiff = null;
    }
    showPeriod();
    if (source() !== 'mic') {
      if (mic) { mic.stream.getTracks().forEach((t) => t.stop()); mic.source.disconnect(); mic = null; }
      return;
    }
    if (mic) return;
    try {
      const stream = await navigator.mediaDevices.getUserMedia({ audio:
        { echoCancellation: false, noiseSuppression: false, autoGainControl: false, channelCount: 2 } });
      mic = { stream, source: context().createMediaStreamSource(stream) };
      if (node) mic.source.connect(node);
    } catch (e) {
      $('audioStatus').textContent = `no microphone: ${e.message}`;
      setSource('click');
    }
  }

  // The page's default source may be the riff: load it and show its credit.
  if (source() === 'riff') onSourceChange();

  return {
    // Fill inL/inR (frames long) with the input for one audio block, from the
    // ScriptProcessor's event (its input buffer carries the microphone).
    fill(event, inL, inR, frames) {
      const liveL = event.inputBuffer.getChannelData(0);
      const liveR = event.inputBuffer.getChannelData(event.inputBuffer.numberOfChannels > 1 ? 1 : 0);
      const settings = { source: source(), level: +$('level').value, muted: inputMuted };
      const period = +$('period').value;
      for (let f = 0; f < frames; f++) {
        const live = [liveL[f], liveR[f]];
        inL[f] = inputSample(state, settings, 0, live);
        inR[f] = inputSample(state, settings, 1, live);
        advanceInput(state, period);
      }
    },
    // The ScriptProcessor that now runs the audio (null: stopped); the
    // microphone feeds it.
    attach(processor) {
      node = processor;
      if (node && mic) mic.source.connect(node);
    },
    toggleMute,
  };
}
