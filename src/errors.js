/**
 * Custom Errors for FastScan
 * Provides detailed error codes and handling
 */

class FastScanError extends Error {
    constructor(message, code, syscall = '') {
        super(message);
        this.name = 'FastScanError';
        this.code = code;
        this.syscall = syscall;
        Error.captureStackTrace(this, FastScanError);
    }
}

class FileNotFoundError extends FastScanError {
    constructor(filepath) {
        super(`File not found: ${filepath}`, 'FS_FILE_NOT_FOUND', 'open');
        this.name = 'FileNotFoundError';
    }
}

class MemoryError extends FastScanError {
    constructor(message) {
        super(`Memory allocation failed: ${message}`, 'FS_OUT_OF_MEMORY', 'malloc');
        this.name = 'MemoryError';
    }
}

class InvalidArgumentError extends FastScanError {
    constructor(message) {
        super(`Invalid argument: ${message}`, 'FS_INVALID_ARG', 'validate');
        this.name = 'InvalidArgumentError';
    }
}

class MappingError extends FastScanError {
    constructor(message) {
        super(`Failed to map file to memory: ${message}`, 'FS_MMAP_FAILED', 'mmap');
        this.name = 'MappingError';
    }
}

module.exports = {
    FastScanError,
    FileNotFoundError,
    MemoryError,
    InvalidArgumentError,
    MappingError
};
