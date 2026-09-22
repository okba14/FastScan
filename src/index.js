const path = require('path');

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
            // Handled when function called
        }
    }
}

const { 
    FastScanError, 
    FileNotFoundError, 
    MemoryError, 
    InvalidArgumentError,
    MappingError,
    UnsupportedFileTypeError
} = require('./errors');

const { 
    scanWithContext, 
    scanWithPositions, 
    scanFileMulti, 
    scanFileMultiAsync, 
    scanIterator 
} = require('./api');

const ERROR_MAP = {
    'File not found': FileNotFoundError,
    'Memory mapping failed': MappingError,
    'Buffer allocation failed': MemoryError,
    'Invalid argument': InvalidArgumentError,
    'File truncated during scan': MemoryError,
    'Unsupported file type (directories and special devices not supported)': UnsupportedFileTypeError
};

function ensureAddon() {
    if (!addon) {
        throw new FastScanError(
            "FastScan native addon is not compiled. Please run 'npm run rebuild' or 'node-gyp rebuild'."
        );
    }
}

function validateFile(filepath, pattern, maxMatches) {
    if (!filepath || typeof filepath !== 'string') {
        throw new InvalidArgumentError('Filepath must be a non-empty string');
    }
    validatePatternAndMatches(pattern, maxMatches);
}

function validatePatternAndMatches(pattern, maxMatches) {
    const isString = typeof pattern === 'string';
    const isBuffer = Buffer.isBuffer(pattern) || (pattern instanceof Uint8Array);
    if (!isString && !isBuffer) {
        throw new InvalidArgumentError('Pattern must be a string or Buffer');
    }
    if (pattern.length === 0) {
        throw new InvalidArgumentError('Pattern must be non-empty');
    }
    if (typeof maxMatches !== 'number' || maxMatches <= 0) {
        throw new InvalidArgumentError('maxMatches must be a positive number');
    }
}

/**
 * Scans a file synchronously using native C, AVX-512/AVX2/NEON, and zero-copy memory mapping.
 * Supported on Windows, Linux, and macOS.
 * 
 * @param {string} filepath - Absolute or relative path to file.
 * @param {string|Buffer|Uint8Array} pattern - The text or binary pattern to search for.
 * @param {number} maxMatches - Maximum number of matches to return.
 * @returns {BigUint64Array} - Zero-copy TypedArray of 64-bit byte offsets.
 */
function scanFile(filepath, pattern, maxMatches = 100000) {
    ensureAddon();
    validateFile(filepath, pattern, maxMatches);

    const resolvedPath = path.resolve(filepath);

    try {
        return addon.scanFile(resolvedPath, pattern, maxMatches);
    } catch (err) {
        const ErrorClass = ERROR_MAP[err.message] || FastScanError;
        throw new ErrorClass(err.message);
    }
}

/**
 * Scans a file asynchronously in a native background worker thread.
 * Completely non-blocking for the Node.js event loop.
 * 
 * @param {string} filepath - Absolute or relative path to file.
 * @param {string|Buffer|Uint8Array} pattern - The text or binary pattern to search for.
 * @param {number} maxMatches - Maximum number of matches to return.
 * @returns {Promise<BigUint64Array>} - Resolves with byte offsets.
 */
function scanFileAsync(filepath, pattern, maxMatches = 100000) {
    ensureAddon();
    validateFile(filepath, pattern, maxMatches);

    const resolvedPath = path.resolve(filepath);

    return addon.scanFileAsync(resolvedPath, pattern, maxMatches).catch(err => {
        const ErrorClass = ERROR_MAP[err.message] || FastScanError;
        throw new ErrorClass(err.message);
    });
}

/**
 * Scans an in-memory Buffer or Uint8Array directly with zero file I/O.
 * 
 * @param {Buffer|Uint8Array|string} buffer - The input data buffer to search within.
 * @param {string|Buffer|Uint8Array} pattern - The pattern to find.
 * @param {number} maxMatches - Maximum number of matches.
 * @returns {BigUint64Array} - Byte offsets.
 */
function scanBuffer(buffer, pattern, maxMatches = 100000) {
    ensureAddon();
    const isBuffer = Buffer.isBuffer(buffer) || (buffer instanceof Uint8Array) || typeof buffer === 'string';
    if (!isBuffer) {
        throw new InvalidArgumentError('First argument must be a Buffer, Uint8Array, or string');
    }
    validatePatternAndMatches(pattern, maxMatches);

    try {
        return addon.scanBuffer(buffer, pattern, maxMatches);
    } catch (err) {
        const ErrorClass = ERROR_MAP[err.message] || FastScanError;
        throw new ErrorClass(err.message);
    }
}

/**
 * Inspect detected CPU vector capabilities (SSE2, AVX2, AVX-512, NEON)
 * @returns {{ sse2: boolean, avx2: boolean, avx512: boolean, neon: boolean }}
 */
function getCpuFeatures() {
    ensureAddon();
    if (addon.getCpuFeatures) {
        return addon.getCpuFeatures();
    }
    return { sse2: true, avx2: false, avx512: false, neon: false };
}

module.exports = {
    // Core APIs
    scanFile,
    scanFileAsync,
    scanBuffer,
    
    // Multi-Pattern Single-Pass APIs
    scanFileMulti,
    scanFileMultiAsync,

    // High-Level Ergonomic APIs with Positions & Snippets
    scanWithContext,
    scanWithPositions,
    scanIterator,
    
    // Hardware Inspection
    getCpuFeatures,

    // Error Types
    errors: {
        FastScanError,
        FileNotFoundError,
        MemoryError,
        InvalidArgumentError,
        MappingError,
        UnsupportedFileTypeError
    }
};
