// The visitor's firmware, remembered in their own browser (IndexedDB), so a
// returning visitor does not pick the chip files again. It never leaves the
// browser. Every call fails quietly (private windows, blocked storage, or
// storage that never answers, as in headless Chrome: 2 s at most), and the
// page then simply asks for the files as usual.
const DB = 'lexicon224x', STORE = 'firmware', KEY = 'last';

function open() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB, 1);
    request.onupgradeneeded = () => request.result.createObjectStore(STORE);
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

async function run(mode, action) {
  const work = open().then((db) => new Promise((resolve, reject) => {
    const request = action(db.transaction(STORE, mode).objectStore(STORE));
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  }));
  const timeout = new Promise((_, reject) => setTimeout(() => reject(new Error('storage did not answer')), 2000));
  return Promise.race([work, timeout]);
}

// files: [{name, bytes: Uint8Array}]. key: which files these are (the
// Lexicon's by default; the comparison page keeps its MIDIVerb ROMs under 'midiverb').
export async function saveFirmware(files, key = KEY) {
  try { await run('readwrite', (store) => store.put({ files, saved: Date.now() }, key)); } catch {}
}

// {files: [{name, bytes}], saved} or null
export async function loadFirmware(key = KEY) {
  try { return (await run('readonly', (store) => store.get(key))) || null; } catch { return null; }
}

export async function forgetFirmware(key = KEY) {
  try { await run('readwrite', (store) => store.delete(key)); } catch {}
}
