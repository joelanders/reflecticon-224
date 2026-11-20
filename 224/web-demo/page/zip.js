// A minimal .zip reader: every file in the archive as {name, bytes}. Enough
// for a folder of ROM chips: stored and deflated entries (the browser's own
// DecompressionStream inflates), no encryption, no zip64.
export async function readZip(buffer) {
  const data = new Uint8Array(buffer), view = new DataView(buffer);
  // The end-of-central-directory record: the last "PK\5\6" in the file.
  let end = data.length - 22;
  while (end >= 0 && view.getUint32(end, true) !== 0x06054b50) end--;
  if (end < 0) throw new Error('not a zip file');
  const count = view.getUint16(end + 10, true);
  let entry = view.getUint32(end + 16, true);
  const files = [];
  for (let i = 0; i < count; i++) {
    if (view.getUint32(entry, true) !== 0x02014b50) throw new Error('damaged zip directory');
    const method = view.getUint16(entry + 10, true), size = view.getUint32(entry + 20, true);
    const nameLength = view.getUint16(entry + 28, true), extra = view.getUint16(entry + 30, true);
    const comment = view.getUint16(entry + 32, true), local = view.getUint32(entry + 42, true);
    const name = new TextDecoder().decode(data.subarray(entry + 46, entry + 46 + nameLength));
    entry += 46 + nameLength + extra + comment;
    if (name.endsWith('/') || name.startsWith('__MACOSX/')) continue;
    const start = local + 30 + view.getUint16(local + 26, true) + view.getUint16(local + 28, true);
    const packed = data.subarray(start, start + size);
    let bytes = packed;
    if (method === 8) {
      const stream = new Blob([packed]).stream().pipeThrough(new DecompressionStream('deflate-raw'));
      bytes = new Uint8Array(await new Response(stream).arrayBuffer());
    } else if (method !== 0) {
      continue;                     // a compression method we don't read
    }
    files.push({ name: name.split('/').pop(), path: name, bytes });
  }
  return files;
}
