const addon = require('../build/Release/fastscan.node');
const { 
    FastScanError, 
    FileNotFoundError, 
    MemoryError, 
    InvalidArgumentError,
    MappingError 
} = require('./errors');
const { scanWithContext, scanIterator } = require('./api');

// Map C Error Codes to JS Error Classes
const ERROR_MAP = {
    'File not found': FileNotFoundError,
    'Memory mapping failed': MappingError,
    'Buffer allocation failed': MemoryError

};

/**
 * Internal helper to validate input arguments
 */
function validate(filepath, pattern, maxMatches) {
    if (!filepath || typeof filepath !== 'string') {
        throw new InvalidArgumentError('Filepath must be a string');
    }
    if (!pattern || typeof pattern !== 'string') {
        throw new InvalidArgumentError('Pattern must be a string');
    }
    if (typeof maxMatches !== 'number' || maxMatches <= 0) {
        throw new InvalidArgumentError('maxMatches must be a positive number');
    }
}

/**
 * Scans a file synchronously using native C and mmap.
 * WARNING: This function blocks the event loop. Use only for CLI tools or scripts.
 * 
 * @param {string} filepath - Absolute or relative path to file.
 * @param {string} pattern - The text pattern to search for.
 * @param {number} maxMatches - Maximum number of matches to return.
 * @returns {BigUint64Array} - Array of byte offsets (Zero-Copy TypedArray)
 */
function scanFile(filepath, pattern, maxMatches = 100000) {
    validate(filepath, pattern, maxMatches);
    
    try {
        // Native Call
        const result = addon.scanFile(filepath, pattern, maxMatches);
        return result; // This is a BigUint64Array now (Efficient!)
    } catch (err) {
        // Enhance error handling
        const ErrorClass = ERROR_MAP[err.message] || FastScanError;
        throw new ErrorClass(err.message);
    }
}

/**
 * Scans a file asynchronously.
 * Ideal for Web Servers. Does not block the event loop.
 * 
 * @param {string} filepath - Absolute or relative path to file.
 * @param {string} pattern - The text pattern to search for.
 * @param {number} maxMatches - Maximum number of matches to return.
 * @returns {Promise<BigUint64Array>} - Resolves with an array of byte offsets.
 */
function scanFileAsync(filepath, pattern, maxMatches = 100000) {
    validate(filepath, pattern, maxMatches);
    
    // The native C addon directly creates and returns a Promise
    // We wrap it to catch errors and transform them to our classes
    return addon.scanFileAsync(filepath, pattern, maxMatches).catch(err => {
        const ErrorClass = ERROR_MAP[err.message] || FastScanError;
        throw new ErrorClass(err.message);
    });
}

// Export Main Features
module.exports = {
    // Core
    scanFile,
    scanFileAsync,
    
    // High Level API
    scanWithContext,
    scanIterator,
    
    // Types (for instanceof checks)
    errors: {
        FastScanError,
        FileNotFoundError,
        MemoryError
    }
};
