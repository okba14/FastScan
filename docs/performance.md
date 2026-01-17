# FastScan Performance Analysis

## Overview

FastScan is engineered for **high-throughput file scanning**. This document summarizes the key performance strategies, benchmark results, and memory characteristics that make FastScan suitable for GB-scale log analysis.

---

## Performance Goals

* Minimize scan time for large files (>1GB)
* Keep memory usage low (outside V8 heap)
* Maintain non-blocking execution for Node.js
* Achieve consistent results across different file sizes

---

## Optimizations Implemented

### 1. Memory-Mapped Files (mmap)

* Reads file data directly from the OS page cache.
* Avoids duplicating file content into JS heap.
* Enables processing of files larger than available RAM.

**Impact:** Reduces heap usage and GC pressure dramatically.

---

### 2. SIMD Acceleration (SSE2)

* Fast path for common pattern lengths (e.g., 5-byte keywords like `ERROR`).
* Processes 16 bytes per CPU cycle using `_mm_loadu_si128` and `_mm_cmpeq_epi8`.
* Uses `_mm_movemask_epi8` to detect matches efficiently.

**Impact:** Significant reduction in CPU cycles per byte.

---

### 3. Multi-threading

* Utilizes all available CPU cores (`sysconf(_SC_NPROCESSORS_ONLN)`).
* File is partitioned into logical chunks with boundary overlap to avoid missing matches.
* Threads are isolated; local buffers avoid synchronization overhead.

**Impact:** Linear speedup proportional to number of cores.

---

### 4. Zero-Copy Memory Transfers

* Scan results are allocated once in C (`malloc`).
* Exposed to Node.js as `BigInt64Array` via `External ArrayBuffer`.
* Ownership transferred to JS GC.

**Impact:** Eliminates memcpy overhead and V8 heap growth.

---

### 5. Branch Prediction and Prefetching

* Uses `likely()` / `unlikely()` macros to guide compiler.
* Prefetches memory 128 bytes ahead to minimize cache misses.

**Impact:** Reduces stalls and increases throughput.

---

## Benchmark Results

Benchmarking performed on `big_data.log` (several GB) with the pattern `ERROR`.

### 1. Throughput Comparison

| Method                 | Avg Time (ms) | Speedup |
| ---------------------- | ------------- | ------- |
| Node.js Buffer.indexOf | 166.16        | 1×      |
| FastScan               | 19.80         | 8.39×   |

### 2. Memory Usage

| Method   | Heap Allocated |
| -------- | -------------- |
| Node.js  | 0 MB           |
| FastScan | -0.54 MB*      |

*FastScan memory is off-heap using OS page cache.

### 3. Event Loop Non-Blocking Test

* Main thread executed 13 heartbeat tasks during scan.
* Async scan completed in 29.27ms without blocking main thread.
* Verdict: ✅ Truly non-blocking.

---

## Comparison with Traditional Node.js Methods

| Feature           | Node.js Buffer.indexOf | FastScan                  |
| ----------------- | ---------------------- | ------------------------- |
| Max file size     | Limited by heap        | Limited by OS cache (GBs) |
| Memory usage      | High for large files   | Minimal (off-heap)        |
| CPU efficiency    | Single-threaded        | Multi-threaded + SIMD     |
| Event loop impact | Blocks on large scans  | Non-blocking async        |
| Result type       | JS array (copy)        | BigInt64Array (zero-copy) |

---

## Recommended Usage

* Use **async API** for production servers.
* Use **CLI** or **sync API** for batch processing or scripts.
* Adjust `maxMatches` to prevent excessive memory allocation.
* Pattern length of 5 bytes benefits most from SIMD optimizations.

---

## Conclusion

FastScan achieves:

* **8× faster scanning** than native Node.js methods
* **Minimal memory footprint** even for multi-GB files
* **True asynchronous, non-blocking operation**
* **Predictable performance** regardless of file size

These optimizations make FastScan suitable for high-performance log analysis, monitoring, and security tools within Node.js ecosystems.
