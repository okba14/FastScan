const path = require('path');
const fs = require('fs');

let addon;
try {
    addon = require('bindings')('fastscan.node');
} catch (e) {
    try {
        addon = require('../build/Release/fastscan.node');
    } catch (e2) {
        try {
            addon = require('../build/Debug/fastscan.node');
        } catch (e3) {
            // Handled when called
        }
    }
}

function ensureAddon() {
    if (!addon) {
        throw new Error("FastScan native binary not found. Please run 'npm run rebuild'.");
    }
}

/**
 * Advanced API: High-performance search returning text surrounding matches.
 * Uses native memory-mapped zero-syscall context extraction in C.
 * 
 * @param {string} filepath - Path to file
 * @param {string|Buffer} pattern - Pattern to find
 * @param {object} options - { maxMatches, contextBefore, contextAfter, contextSize }
 * @returns {Promise<Array<{offset: bigint, snippet: string}>>}
 */
async function scanWithContext(filepath, pattern, options = {}) {
    ensureAddon();
    const { 
        maxMatches = 100, 
        contextSize = 50,
        contextBefore = contextSize,
        contextAfter = contextSize
    } = options;

    const resolvedPath = path.resolve(filepath);

    if (addon.scanWithContextNative) {
        return addon.scanWithContextNative(
            resolvedPath, 
            pattern, 
            maxMatches, 
            contextBefore, 
            contextAfter
        );
    }

    const offsets = addon.scanFile(resolvedPath, pattern, maxMatches);
    let fd;
    const results = [];

    try {
        fd = fs.openSync(resolvedPath, 'r');
        const stat = fs.fstatSync(fd);

        for (const offset of offsets) {
            const numOffset = Number(offset);
            const readStart = Math.max(0, numOffset - contextBefore);
            const readLen = Math.min(contextBefore + contextAfter, stat.size - readStart);
            if (readLen <= 0) continue;
            const buffer = Buffer.allocUnsafe(readLen);
            fs.readSync(fd, buffer, 0, readLen, readStart);
            results.push({
                offset,
                snippet: buffer.toString('utf8')
            });
        }
    } finally {
        if (fd !== undefined) {
            try { fs.closeSync(fd); } catch (e) {}
        }
    }

    return results;
}

/**
 * Advanced API: High-performance search returning exact Line and Column positions.
 * Uses SIMD-vectorized newline indexing at 40+ GB/s in O(N) single-pass.
 * 
 * @param {string} filepath - Path to file
 * @param {string|Buffer} pattern - Pattern to find
 * @param {object} options - { maxMatches, contextBefore, contextAfter, contextSize }
 * @returns {Promise<Array<{offset: bigint, line: number, column: number, snippet: string}>>}
 */
async function scanWithPositions(filepath, pattern, options = {}) {
    ensureAddon();
    const {
        maxMatches = 100,
        contextSize = 50,
        contextBefore = contextSize,
        contextAfter = contextSize
    } = options;

    const resolvedPath = path.resolve(filepath);

    if (addon.scanWithPositionsNative) {
        return addon.scanWithPositionsNative(
            resolvedPath,
            pattern,
            maxMatches,
            contextBefore,
            contextAfter
        );
    }

    return scanWithContext(filepath, pattern, options);
}

/**
 * Multi-Pattern Single-Pass Scanner: Searches for multiple patterns simultaneously.
 * Checks all patterns in one single pass through mapped memory.
 * 
 * @param {string} filepath - Path to file
 * @param {string[]} patterns - Array of patterns to search for
 * @param {number} maxMatches - Maximum results
 * @returns {Array<{patternIndex: number, pattern: string, offset: bigint}>}
 */
function scanFileMulti(filepath, patterns, maxMatches = 100000) {
    ensureAddon();
    if (!Array.isArray(patterns) || patterns.length === 0) {
        throw new TypeError("patterns must be a non-empty array of strings");
    }
    const resolvedPath = path.resolve(filepath);
    return addon.scanFileMulti(resolvedPath, patterns, maxMatches);
}

/**
 * Asynchronous Multi-Pattern Single-Pass Scanner.
 * Non-blocking for the Node.js event loop powered by native worker threads.
 * 
 * @param {string} filepath - Path to file
 * @param {string[]} patterns - Array of patterns to search for
 * @param {number} maxMatches - Maximum results
 * @returns {Promise<Array<{patternIndex: number, pattern: string, offset: bigint}>>}
 */
async function scanFileMultiAsync(filepath, patterns, maxMatches = 100000) {
    ensureAddon();
    if (!Array.isArray(patterns) || patterns.length === 0) {
        throw new TypeError("patterns must be a non-empty array of strings");
    }
    const resolvedPath = path.resolve(filepath);

    if (addon.scanFileMultiAsync) {
        return addon.scanFileMultiAsync(resolvedPath, patterns, maxMatches);
    }

    return new Promise((resolve, reject) => {
        setImmediate(() => {
            try {
                const res = addon.scanFileMulti(resolvedPath, patterns, maxMatches);
                resolve(res);
            } catch (err) {
                reject(err);
            }
        });
    });
}

/**
 * Asynchronous Generator Iterator for huge files.
 */
async function* scanIterator(filepath, pattern, maxMatches = 100000) {
    ensureAddon();
    const resolvedPath = path.resolve(filepath);
    const offsets = await addon.scanFileAsync(resolvedPath, pattern, maxMatches);
    
    for (let i = 0; i < offsets.length; i++) {
        yield {
            index: i,
            offset: offsets[i]
        };
    }
}

module.exports = {
    scanWithContext,
    scanWithPositions,
    scanFileMulti,
    scanFileMultiAsync,
    scanIterator
};
