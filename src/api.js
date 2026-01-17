const fs = require('fs');
const errors = require('./errors');

// FIX: Require the Native Addon directly to avoid Circular Dependency with index.js
// The path is relative to the 'src' folder
const addon = require('../build/Release/fastscan.node');

/**
 * Helper to read a chunk of text around a specific offset
 * Efficiently uses fs.read to avoid loading whole file
 */
function readContext(filepath, offset, beforeBytes, afterBytes) {
    // Convert BigInt to Number for Node.js fs API
    // fs.readSync expects a Number, so we explicitly cast it.
    // For small context reads, this is safe even on 64-bit systems.
    const safeOffset = Number(offset);

    const buffer = Buffer.allocUnsafeSlow(beforeBytes + afterBytes);
    const fd = fs.openSync(filepath, 'r');
    
    // Calculate start position safely (don't read before 0)
    const readStart = Math.max(0, safeOffset - beforeBytes);
    const readLen = Math.min(beforeBytes + afterBytes, fs.fstatSync(fd).size - readStart);

    let context = '';
    if (readLen > 0) {
        fs.readSync(fd, buffer, 0, readLen, readStart);
        context = buffer.toString('utf8', 0, readLen);
    }
    
    fs.closeSync(fd);
    return context;
}

/**
 * Advanced API: Search and return text surrounding matches
 * 
 * @param {string} filepath - Path to file
 * @param {string} pattern - Pattern to find
 * @param {object} options - { maxMatches, contextSize }
 * @returns {Promise<Array<{offset: number, snippet: string}>>}
 */
async function scanWithContext(filepath, pattern, options = {}) {
    const { maxMatches = 100, contextSize = 50 } = options;
    
    // 1. Get Raw Offsets directly from Native Addon (Fast C Scan)
    const offsets = addon.scanFile(filepath, pattern, maxMatches);
    
    // 2. Enrich with context (JS IO)
    const results = [];
    for (const offset of offsets) {
        const snippet = readContext(filepath, offset, contextSize, contextSize);
        results.push({ offset, snippet });
    }
    
    return results;
}

/**
 * Advanced API: Create an Async Iterator for huge files
 * Useful for processing results as they arrive (simulated via chunking)
 * Note: Since our C implementation returns all offsets at once, this is a wrapper
 * for convenience to allow async/await loops.
 */
async function* scanIterator(filepath, pattern, maxMatches = 100000) {
    // Get offsets directly from Native Addon
    const offsets = addon.scanFile(filepath, pattern, maxMatches);
    
    for (let i = 0; i < offsets.length; i++) {
        yield {
            index: i,
            offset: offsets[i],
            // You could add lazy context loading here
        };
        // Simulate async flow if needed
        await setImmediate(); 
    }
}

module.exports = {
    scanWithContext,
    scanIterator
};
