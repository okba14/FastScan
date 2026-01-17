# FastScan Security Overview

FastScan is designed with a focus on **memory safety, thread safety, and predictable behavior** to ensure that even when scanning untrusted or extremely large files, it remains secure and robust. This document outlines the security considerations and best practices.

---

## 1. Memory Safety

* **No buffer overflows**: All scan operations validate bounds using `limit` pointers and `pattern_len`.
* **Off-heap scanning**: File data is accessed via memory-mapped pages (mmap), avoiding V8 heap allocations and potential heap corruption.
* **Zero-copy ownership**: Scan results are managed through `External ArrayBuffer` with a GC finalizer, ensuring memory is freed correctly.
* **Safe local buffers per thread**: Each worker thread has its own allocated memory to prevent race conditions or memory corruption.

---

## 2. Thread Safety

* **Isolated contexts**: `fastscan_ctx_t` is local to each scan and never shared between threads.
* **No global mutable state**: There are no static/global variables that could cause data races.
* **Controlled thread creation**: Thread boundaries are carefully calculated, including overlap for patterns that span chunk edges.
* **Cleanup on error**: Threads that fail to create or allocate memory trigger a cleanup routine to avoid resource leaks.

---

## 3. Input Validation

* **File paths**: Checked for NULL and valid UTF-8.
* **Pattern lengths**: Validated to prevent zero-length or overly large patterns.
* **Maximum matches**: `max_matches` parameter is enforced to avoid excessive allocations.
* **CLI and API**: Both sync and async entry points perform argument validation.

---

## 4. Error Handling

* All errors are reported via status codes in C and propagated to JavaScript as exceptions or rejected Promises.
* Known error types:

  * File not found (`FS_ERROR_OPEN_FAILED`)
  * Memory mapping failure (`FS_ERROR_MMAP_FAILED`)
  * Buffer allocation failure (`FS_ERROR_OUT_OF_BOUNDS`)
* No internal errors leak sensitive information or leave memory in an inconsistent state.

---

## 5. File Scanning Security Considerations

* **Large file handling**: Memory-mapped scanning allows handling files larger than available RAM safely.
* **Pattern matching**: The scanning engine uses deterministic algorithms without regex backtracking to prevent denial-of-service via complex patterns.
* **Async safety**: Event loop is never blocked; async scans are isolated in worker threads.

---

## 6. Integration Security

* **N-API boundary**: Proper use of finalizers ensures that native memory is correctly released when JS objects are garbage collected.
* **Ownership transfer**: Native buffers are only freed once; ownership semantics prevent double-free or use-after-free.
* **Promise-based async**: Errors in async scans are propagated cleanly to JS, preventing uncaught exceptions.

---

## 7. Recommendations

* Always prefer `scanFileAsync` in long-running applications to avoid event-loop blocking.
* Limit `max_matches` according to memory constraints.
* Keep Node.js and system libraries updated to benefit from OS-level security patches for mmap and threading.
* Avoid scanning untrusted files with extremely long patterns that could exhaust CPU resources.

---

## 8. Summary

FastScan combines **native performance** with **robust security design**:

* Memory-safe, thread-safe, and predictable behavior
* Zero-copy results to prevent heap misuse
* Deterministic pattern scanning

These measures ensure that FastScan is suitable for **production environments, including security-critical applications** such as log analysis, intrusion detection, and monitoring systems.
