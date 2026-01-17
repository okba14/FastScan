# FastScan Architecture

## Overview

FastScan is a high-performance file scanning engine for Node.js, designed to search massive files (GB-scale) with minimal memory usage and zero impact on the Node.js event loop. The architecture combines:

* Native C (N-API) for performance-critical paths
* Memory-mapped file access (mmap)
* Zero-copy data transfer between native and JavaScript
* Async worker threads for non-blocking execution

The design goal is simple: **operate at OS / systems level speed while remaining idiomatic and safe in Node.js**.

---

## High-Level Data Flow

```
JavaScript API / CLI
        │
        ▼
N-API Binding (addon.c)
        │
        ▼
Async Worker Thread (ExecuteScan)
        │
        ▼
FastScan Core (scanner.c / parser.c)
        │
        ▼
Memory-Mapped File (mmap)
        │
        ▼
Match Offsets (fs_size_t*)
        │
        ▼
External ArrayBuffer (Zero-Copy)
        │
        ▼
BigInt64Array in JavaScript
```

---

## Module Responsibilities

### 1. JavaScript Layer (`src/`)

* **index.js**: Public entry point, re-exports native bindings
* **api.js**: Input validation, argument normalization
* **errors.js**: Maps native errors to JS-friendly messages

This layer contains *no heavy logic* and never touches file contents.

---

### 2. N-API Binding Layer (`native/src/addon.c`)

This is the critical bridge between Node.js and native code.

Responsibilities:

* Argument marshaling (JS → C)
* Promise lifecycle management
* Async worker creation
* Ownership transfer of native memory
* Zero-copy TypedArray exposure

Key architectural decisions:

* **No file I/O on the main thread**
* **No V8 heap allocations for scan results**
* **Explicit ownership transfer using finalizers**

---

### 3. Async Execution Model

#### Async Entry Point

* `scanFileAsync(path, pattern, maxMatches)`
* Returns a Promise immediately
* Allocates `AsyncScanData`

#### Worker Thread (`ExecuteScan`)

Runs outside the event loop:

* Initializes FastScan context
* Memory-maps the target file
* Executes pattern scanning
* Produces a raw `fs_size_t*` match array

Important guarantees:

* No interaction with V8
* No JS heap access
* No GC pressure

#### Completion Phase (`CompleteScan`)

Runs on the main thread:

* Wraps native memory in an **External ArrayBuffer**
* Creates `BigInt64Array` view
* Transfers ownership to JS GC

---

## FastScan Core (`native/`)

### Context Object (`fastscan_ctx_t`)

Central structure containing:

* Memory-mapped file region
* Pattern metadata
* Match result buffer
* Match counters

Ownership rules:

* File mapping owned by context
* Match buffer detached and transferred to JS when needed

---

### Memory Mapping (`mmap_reader.c`)

* Uses OS page cache instead of heap buffers
* Supports very large files
* Eliminates redundant reads and copies

Benefits:

* Near-zero memory overhead
* OS-level read-ahead
* Excellent cache locality

---

### Scanner (`scanner.c`)

* Linear scan over mapped memory
* Bounds-safe pointer arithmetic
* Designed for predictable branch behavior

Key properties:

* O(n) scan time
* Zero allocations during scan
* No recursion

---

### Parser (`parser.c`)

* Pattern preprocessing
* Precomputes lookup structures
* Avoids regex backtracking

This allows deterministic performance regardless of input.

---

## Zero-Copy Memory Model

### Problem

Typical Node.js native addons:

* Allocate memory
* Copy into V8 buffers
* Trigger GC pressure

### FastScan Solution

1. Native allocates match buffer via `malloc`
2. Buffer wrapped using `napi_create_external_arraybuffer`
3. JS receives a `BigInt64Array` view
4. GC finalizer frees native memory

Result:

* **No copies**
* **No heap growth**
* **Deterministic memory usage**

---

## Error Handling Strategy

Errors are categorized:

* File access errors
* Mapping failures
* Bounds / allocation failures

Rules:

* Native layer returns status codes
* JS layer receives human-readable errors
* No partial or corrupted state is exposed

---

## Thread Safety Guarantees

* Each scan uses an isolated context
* No global mutable state
* No shared buffers between scans

Safe for:

* Parallel async scans
* Server environments
* Long-running processes

---

## Why This Architecture Works

* **OS primitives instead of JS abstractions**
* **Explicit memory ownership**
* **No hidden copies**
* **Event-loop friendly**

FastScan behaves closer to tools like `ripgrep` than traditional Node.js libraries — while remaining fully embeddable in JS applications.

---

## Summary

FastScan’s architecture is intentionally minimal, explicit, and systems-oriented:

* Native speed
* Predictable memory
* Safe JS integration

This makes it suitable for:

* Log analysis
* Security tooling
* Data pipelines
* High-throughput servers
