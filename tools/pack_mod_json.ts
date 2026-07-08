#!/usr/bin/env -S deno run --allow-read --allow-write --allow-env

// Pack builder for data.jsonpack archives.
// Scans a mod's JSON directory, computes FNV-1a-64 hash, and writes a binary pack file.
// Contract: identical hash computation with C++ side (relative paths, forward slashes).

// FNV-1a-64 constants (same as C++)
const fnv1a_offset_basis = 14695981039346656037n;
const fnv1a_prime = 1099511628211n;
const fnv1a_mask = 0xFFFFFFFFFFFFFFFFn;

// Accumulate hash (BigInt version with 64-bit masking)
function fnv1a_64_accumulate(hash: bigint, data: Uint8Array | string): bigint {
  const bytes = typeof data === "string" ? new TextEncoder().encode(data) : data;
  for (const byte of bytes) {
    hash = ((hash ^ BigInt(byte)) * fnv1a_prime) & fnv1a_mask;
  }
  return hash;
}

// Main pack builder
async function buildPack(jsonDir: string): Promise<void> {
  const packPath = jsonDir + "/data.jsonpack";

  interface FileEntry {
    relPath: string;
    data: Uint8Array;
    size: number;
  }
  const entries: FileEntry[] = [];
  let hash = fnv1a_offset_basis;

  // Walk directory recursively, collect .json files
  // BFS walk to match get_files_from_path order (level-by-level, sorted within each level)
  async function walkBFS(root: string): Promise<void> {
    const queue: string[] = [root];
    while (queue.length > 0) {
      const dir = queue.shift()!;
      const entries_list: Deno.DirEntry[] = [];
      for await (const entry of Deno.readDir(dir)) {
        entries_list.push(entry);
      }
      // Sort entries within this level (matches std::sort in find_file_if_bfs)
      entries_list.sort((a, b) => (a.name < b.name ? -1 : a.name > b.name ? 1 : 0));

      const subdirs: string[] = [];
      for (const entry of entries_list) {
        const fullPath = dir + "/" + entry.name;
        if (entry.isDirectory && !entry.name.startsWith('.')) {
          subdirs.push(fullPath);
        } else if (entry.isFile && entry.name.endsWith(".json")) {
          const data = await Deno.readFile(fullPath);
          const relPath = dir === root
            ? entry.name
            : dir.substring(root.length + 1) + "/" + entry.name;
          entries.push({ relPath, data, size: data.byteLength });
        }
      }
      // Add subdirs in sorted order (matches BFS queue behavior)
      queue.push(...subdirs);
    }
  }

  // Scan directory
  if (!await Deno.stat(jsonDir).catch(() => null)) {
    console.error(`Error: directory not found: ${jsonDir}`);
    Deno.exit(1);
  }

  await walkBFS(jsonDir);

  // Compute hash (path + size string)
  // Compute hash (path + size string) — entries already in BFS order
  for (const entry of entries) {
    const hashInput = entry.relPath + entry.size.toString();
    hash = fnv1a_64_accumulate(hash, hashInput);
  }

  // Build binary pack file
  const headerSize = 20;
  let offset = headerSize;
  for (const entry of entries) {
    offset += 4 + new TextEncoder().encode(entry.relPath).byteLength + 4 + entry.data.byteLength;
  }

  const buf = new Uint8Array(offset);
  const view = new DataView(buf.buffer);

  // Write header
  buf.set([0x43, 0x42, 0x4E, 0x50], 0); // "CBNP" magic
  view.setUint32(4, 1, true); // version = 1 (LE)
  view.setUint32(8, entries.length, true); // entry_count (LE)
  view.setBigUint64(12, hash, true); // hash (LE)

  // Write entries
  let pos = 20;
  for (const entry of entries) {
    const pathBytes = new TextEncoder().encode(entry.relPath);

    // Write path_len
    view.setUint32(pos, pathBytes.byteLength, true);
    pos += 4;

    // Write path
    buf.set(pathBytes, pos);
    pos += pathBytes.byteLength;

    // Write data_len
    view.setUint32(pos, entry.data.byteLength, true);
    pos += 4;

    // Write data
    buf.set(entry.data, pos);
    pos += entry.data.byteLength;
  }

  // Write pack file
  await Deno.writeFile(packPath, buf);
  console.log(`Packed ${entries.length} files (${offset} bytes) → ${packPath}`);
}

// CLI entry point
const jsonDir = Deno.args[0];
if (!jsonDir) {
  console.error("Usage: pack_mod_json.ts <json_directory>");
  console.error("Example: pack_mod_json.ts data/mods/bn");
  Deno.exit(1);
}

buildPack(jsonDir).catch((err) => {
  console.error(`Error: ${err.message}`);
  Deno.exit(1);
});
