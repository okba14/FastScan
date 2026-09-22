const fastscan = require('../src/index');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const testDir = path.join(__dirname, 'sandbox');
if (!fs.existsSync(testDir)) {
    fs.mkdirSync(testDir, { recursive: true });
}

console.log("==========================================================");
console.log("🛡️  Running Chunk-Drop & Multi-Thread Completeness Test");
console.log("==========================================================\n");

// Create a file large enough to trigger multi-threading (e.g. 130 MB > 2 * 64MB chunks)
const testFile = path.join(testDir, 'test_chunk_drop_large.bin');
const FILE_SIZE = 135 * 1024 * 1024; // 135 MB
const PATTERN = "CANARY_TOKEN_MATCH";

console.log(`[*] Generating 135 MB test file to trigger multi-chunk dispatch...`);
const fd = fs.openSync(testFile, 'w');

// Fill with padding
const blockSize = 1024 * 1024; // 1 MB
const block = Buffer.alloc(blockSize, 0x20); // spaces
let written = 0;
while (written < FILE_SIZE) {
    const toWrite = Math.min(blockSize, FILE_SIZE - written);
    fs.writeSync(fd, block, 0, toWrite);
    written += toWrite;
}

// Plant canary tokens:
// 1. At the very beginning (Chunk 0)
// 2. In the middle (Chunk 1)
// 3. Near the very end of the file (Last chunk - which was previously dropped by the bug!)
// 4. At the absolute last byte of the file
const pos0 = 100;
const pos1 = 68 * 1024 * 1024 + 500;
const pos2 = 130 * 1024 * 1024 + 1000;
const pos3 = FILE_SIZE - PATTERN.length;

const tokenBuf = Buffer.from(PATTERN, 'utf8');
fs.writeSync(fd, tokenBuf, 0, tokenBuf.length, pos0);
fs.writeSync(fd, tokenBuf, 0, tokenBuf.length, pos1);
fs.writeSync(fd, tokenBuf, 0, tokenBuf.length, pos2);
fs.writeSync(fd, tokenBuf, 0, tokenBuf.length, pos3);
fs.closeSync(fd);

console.log(`[*] Planted 4 canary tokens at offsets:`);
console.log(`    [Chunk 0] offset ${pos0}`);
console.log(`    [Chunk 1] offset ${pos1}`);
console.log(`    [Last Chunk] offset ${pos2}`);
console.log(`    [Exact End of File] offset ${pos3}\n`);

const results = fastscan.scanFile(testFile, PATTERN, 100);
console.log(`[*] FastScan found ${results.length} matches:`, Array.from(results).map(n => Number(n)));

assert.strictEqual(results.length, 4, `Expected 4 matches, but found ${results.length}! (Check if last chunk was dropped)`);
assert.strictEqual(results[0], BigInt(pos0), "Chunk 0 match mismatch");
assert.strictEqual(results[1], BigInt(pos1), "Chunk 1 match mismatch");
assert.strictEqual(results[2], BigInt(pos2), "Last chunk match mismatch (Previously silently dropped!)");
assert.strictEqual(results[3], BigInt(pos3), "Exact end-of-file match mismatch");

console.log("\n\x1b[32m✔ PASS: 100% of chunks scanned. Zero chunks dropped. The silent false negative bug is ELIMINATED.\x1b[0m\n");

// Cleanup
try { fs.unlinkSync(testFile); } catch (_) {}
