const fastscan = require('../src/index');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const testDir = path.join(__dirname, 'sandbox');
if (!fs.existsSync(testDir)) {
    fs.mkdirSync(testDir, { recursive: true });
}

console.log("=========================================");
console.log("🧪 Running Multi-Thread Boundary Overlap Test");
console.log("=========================================\n");

const boundaryFile = path.join(testDir, 'test_boundary_70mb.dat');
const FILE_SIZE = 70 * 1024 * 1024; // 70 MB triggers 2 threads
const PATTERN = "BOUNDARY_SECRET_TOKEN";
const PAT_LEN = PATTERN.length; // 21 bytes

console.log(`[*] Generating ${FILE_SIZE / (1024 * 1024)} MB test file...`);

// Fill with spaces (0x20)
const buffer = Buffer.alloc(FILE_SIZE, 0x20);

// Thread 0 owns [0, splitOffset)
// Thread 1 owns [splitOffset, FILE_SIZE)
const splitOffset = Math.floor(FILE_SIZE / 2); // 36,700,160

// Ensure distance between any two patterns is strictly > PAT_LEN (21 bytes)
const expectedOffsets = [
    0,                                      // Match 1: At offset 0 (Chunk 0)
    splitOffset - 100,                      // Match 2: Well before boundary (Chunk 0)
    splitOffset - 10,                       // Match 3: Spanning across boundary! (Starts at splitOffset - 10, finishes at splitOffset + 11)
    splitOffset + 30,                       // Match 4: 19 bytes after previous match ends (Chunk 1)
    splitOffset + 100,                      // Match 5: Inside Chunk 1
    FILE_SIZE - PAT_LEN                     // Match 6: At the very end of file (Chunk 1)
];

for (const off of expectedOffsets) {
    buffer.write(PATTERN, off, 'utf8');
}

fs.writeFileSync(boundaryFile, buffer);
console.log(`[*] File generated with ${expectedOffsets.length} non-overlapping test tokens:`);
console.log("    Expected offsets:", expectedOffsets);

const matches = fastscan.scanFile(boundaryFile, PATTERN, 100);
console.log(`\n[*] FastScan found ${matches.length} matches.`);
const foundOffsets = Array.from(matches).map(Number);
console.log("    Found offsets:   ", foundOffsets);

assert.strictEqual(foundOffsets.length, expectedOffsets.length, "Match count mismatch!");
assert.deepStrictEqual(foundOffsets, expectedOffsets, "Found offsets do not match expected offsets!");

// Check for duplicates
const uniqueOffsets = new Set(foundOffsets);
assert.strictEqual(uniqueOffsets.size, foundOffsets.length, "Found duplicate matches across boundaries!");

console.log("\n\x1b[32m✔ PASS: Boundary overlap logic is mathematically flawless (No duplicates, no missed matches).\x1b[0m\n");

// Clean up
try { fs.unlinkSync(boundaryFile); } catch (e) {}
