#!/usr/bin/env node

const fastscan = require('./src/index');
const fs = require('fs');
const path = require('path');

const args = process.argv.slice(2);

if (args.length < 2) {
    console.log("Usage: fastscan <filepath> <pattern> [maxMatches]");
    process.exit(1);
}

const filepath = args[0];
const pattern = args[1];
const maxMatches = parseInt(args[2]) || 100; 
const CONTEXT_SIZE = 50; 

console.log(`[*] Scanning: ${filepath}`);
console.log(`[*] Pattern: "${pattern}"`);
console.log(`[*] Please wait...\n`);

// Use High-Level API
fastscan.scanWithContext(filepath, pattern, { maxMatches })
    .then((results) => {
        const duration = results.time || 0; 
        console.log(`✅ Scan Finished.`);
        console.log(`   Found ${results.length} matches`);
        
        console.log(`\n--- Results Preview (First ${Math.min(results.length, 10)} matches) ---`);
        
        for (let i = 0; i < Math.min(results.length, 10); i++) {
            const item = results[i];
            const offset = item.offset;
            const snippet = item.snippet;

            console.log(`[Match #${i + 1}] Offset: ${offset}`);
            console.log(`   ... ${snippet} ...`);
            console.log();
        }
    })
    .catch((err) => {
        if (err.name === 'FileNotFoundError') {
            console.error("❌ Error: The specified file was not found.");
        } else if (err.name === 'MemoryError') {
            console.error("❌ Error: Out of memory. Try reducing maxMatches.");
        } else {
            console.error("❌ An unexpected error occurred:", err.message);
        }
        process.exit(1);
    });
    
