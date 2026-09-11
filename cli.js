#!/usr/bin/env node

const fastscan = require('./src/index');
const path = require('path');
const { performance } = require('perf_hooks');

const args = process.argv.slice(2);

if (args.length < 2) {
    console.log("Usage: fastscan <filepath> <pattern(s)> [maxMatches] [contextSize]");
    console.log("Tip:   Pass multiple patterns separated by comma: fastscan app.log \"ERROR,CRITICAL,FATAL\"");
    process.exit(1);
}

const filepath = args[0];
const rawPattern = args[1];
const maxMatches = parseInt(args[2], 10) || 100;
const contextSize = parseInt(args[3], 10) || 50;

console.log(`\x1b[36m⚡ FastScan Ultra v2.0 — Hardware-Accelerated Multi-Platform Scanner\x1b[0m`);
try {
    const cpu = fastscan.getCpuFeatures();
    const hwList = [];
    if (cpu.avx512) hwList.push("AVX-512 (512-bit SIMD, 128B Loop)");
    else if (cpu.avx2) hwList.push("AVX2 (256-bit SIMD, 64B Loop)");
    else if (cpu.sse2) hwList.push("SSE2 (128-bit SIMD)");
    else if (cpu.neon) hwList.push("ARM NEON");
    console.log(`\x1b[33m[Hardware]\x1b[0m Engine: ${hwList.join(', ') || 'Scalar'} + Persistent Thread Pool`);
} catch (e) {}

console.log(`[*] Target: ${filepath}`);

const isMulti = rawPattern.includes(',');
if (isMulti) {
    const patterns = rawPattern.split(',').map(s => s.trim()).filter(Boolean);
    console.log(`[*] Multi-Pattern Mode (${patterns.length} patterns): [${patterns.join(', ')}]`);
    console.log(`[*] Max Matches: ${maxMatches}`);
    console.log(`[*] Single-Pass Scanning...\n`);

    const startTime = performance.now();
    try {
        const results = fastscan.scanFileMulti(filepath, patterns, maxMatches);
        const elapsed = (performance.now() - startTime).toFixed(2);
        console.log(`\x1b[32m✅ Scan Finished in ${elapsed} ms\x1b[0m`);
        console.log(`   Found ${results.length} matches`);

        const previewCount = Math.min(results.length, 10);
        if (previewCount > 0) {
            console.log(`\n--- Results Preview (First ${previewCount} matches) ---`);
            for (let i = 0; i < previewCount; i++) {
                const item = results[i];
                console.log(`\x1b[34m[Match #${i + 1}]\x1b[0m Pattern: \x1b[33m"${item.pattern}"\x1b[0m | Offset: ${item.offset}`);
            }
        }
    } catch (err) {
        console.error("\x1b[31m❌ Error:\x1b[0m", err.message);
        process.exit(1);
    }
} else {
    console.log(`[*] Pattern: "${rawPattern}"`);
    console.log(`[*] Max Matches: ${maxMatches}`);
    console.log(`[*] Scanning with Vectorized Line Indexing...\n`);

    const startTime = performance.now();

    fastscan.scanWithPositions(filepath, rawPattern, { maxMatches, contextSize })
        .then((results) => {
            const elapsed = (performance.now() - startTime).toFixed(2);
            console.log(`\x1b[32m✅ Scan Finished in ${elapsed} ms\x1b[0m`);
            console.log(`   Found ${results.length} matches`);
            
            const previewCount = Math.min(results.length, 10);
            if (previewCount > 0) {
                console.log(`\n--- Results Preview (First ${previewCount} matches) ---`);
                for (let i = 0; i < previewCount; i++) {
                    const item = results[i];
                    console.log(`\x1b[34m[Match #${i + 1}]\x1b[0m Line ${item.line}:${item.column} (Offset: ${item.offset})`);
                    console.log(`   ... ${item.snippet.replace(/[\r\n]+/g, ' ')} ...\n`);
                }
            }
        })
        .catch((err) => {
            if (err.name === 'FileNotFoundError') {
                console.error("\x1b[31m❌ Error: File not found.\x1b[0m");
            } else if (err.name === 'MemoryError') {
                console.error("\x1b[31m❌ Error: Memory allocation failed. Try reducing maxMatches.\x1b[0m");
            } else {
                console.error("\x1b[31m❌ Error:\x1b[0m", err.message);
            }
            process.exit(1);
        });
}
