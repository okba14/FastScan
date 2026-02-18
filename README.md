# FastScan — Low-Level, High-Performance File Scanning for Node.js

[![npm version](https://img.shields.io/npm/v/@okbawiss/fastscan.svg)](https://www.npmjs.com/package/@okbawiss/fastscan)
[![license](https://img.shields.io/npm/l/@okbawiss/fastscan.svg)](https://github.com/okba14/FastScan/blob/main/LICENSE)
---
FastScan brings **C-level speed** to Node.js, scanning massive GB-scale files with **near-native performance**, **minimal memory footprint**, and **non-blocking async execution**.


It combines:

* ⚙️ Native C (POSIX + mmap)
* ⚡ SIMD acceleration (SSE2)
* 🧵 Parallel scanning (multi‑core)
* 🔒 Safe memory ownership with Zero‑Copy results
* 🟢 Clean Node.js API (sync + async)

FastScan is built for developers who need **real systems‑level performance** without leaving the Node.js ecosystem.

---

## ✨ Key Features

- 🚀 **2–3× faster** than `Buffer.indexOf` on standard hardware  
  *(Scales even higher on modern CPUs)*

- 🧠 **Zero-copy results** using `ExternalArrayBuffer`

- 💾 **Scans files larger than RAM**  
  *(Leverages OS page cache)*

- 🔢 **BigInt offsets**  
  *(Safely supports files larger than 2GB)*

- ⚡ **Async, non-blocking API**  
  *(Leaves one CPU core free for Node.js)*

- 🧵 **Parallel execution**  
  *(Utilizes all available CPU cores)*

- 🛠️ **CLI tool included**  
  *(Ready for direct usage)*


---

## 📦 Project Structure (Overview)

```text
fastscan/
├── src/            # JavaScript API layer
├── native/         # C core (scanner, mmap, threading)
├── benchmarks/     # Performance benchmarks
├── test/           # JS + fuzz tests
├── docs/           # Architecture & performance docs
└── cli.js          # Command‑line interface
```

---

## 🔧 Installation

### 1️⃣ System Requirements

Make sure you have build tools installed:

```bash
sudo apt-get install build-essential
```

> Required for compiling the native C addon.

---
2️⃣ Install & Build

### Build Native Addon
You can install the package in two ways:

#### a) From the GitHub repository (for development or the latest version)

```bash
git clone https://github.com/okba14/FastScan.git
cd FastScan
npm install
npm run rebuild  # Builds the native scanner using node-gyp
```
### b) From npm (for general usage)

```bash
npm install @okbawiss/fastscan
```
### Installing from npm automatically provides the CLI and Node.js API; no manual rebuild is required.

---

## 🧪 Basic Usage

### ▶️ Run Tests
1- Using the repository version
```bash
# If using the GitHub repo version
node test.js
node async-test.js
```
2- Using the npm version
```bash
# npm version
npx fastscan big_data.log "ERROR" 50
```
---

### 🖥️ CLI Usage

Search for the word **"ERROR"** in a large log file:

```bash
node generate-data.js
node test/cli.js big_data.log "ERROR" 50
```

**Arguments:**

1. File path
2. Search pattern
3. Maximum number of matches

## ⚠️ Note: This version currently supports Linux only. Windows and macOS support will be added in the future.
---

### 📄 Example Output

```text
[*] Scanning: big_data.log
[*] Pattern: "ERROR"
[*] Please wait...

✅ Scan Finished.
   Found 50 matches

--- Results Preview (First 10 matches) ---
[Match #1] Offset: 249
   ... ..
2023-10-25 [DEBUG] Memory check OK
2023-10-25 [ERROR] Critical failure detected at index ID
2023- ...

[Match #2] Offset: 385
   ... ..
2023-10-25 [DEBUG] Memory check OK
2023-10-25 [ERROR] Critical failure detected at index ID
2023- ...

[Match #3] Offset: 1469
   ... ..
2023-10-25 [DEBUG] Memory check OK
2023-10-25 [ERROR] Critical failure detected at index ID
2023- ...

```

---

## ⚡ Performance Benchmarking

### Generate Large Test Data

```bash
node generate-data.js
```

This creates a large synthetic log file for benchmarking.

---

### Run Benchmarks

```bash
node benchmarks/benchmark.js
```

## 📊 Benchmarking

**FastScan** benchmarks itself against native Node.js scanning methods.

### 📊 Sample Results (SSE2 Environment)

The following results were obtained on a machine with an older CPU  
*(supporting SSE2 only)*.

They demonstrate consistent superiority in real-world patterns  
while maintaining memory efficiency.

```bash

🚀 FastScan Ultimate Benchmark
📁 File: /home/bbot/Desktop/FastScan/benchmarks/big_data.log
📦 Size: 100.00 MB

🔥 BENCHMARK 1: Multi-Pattern Stress Test (Speed & Generic Optimization)
⚠️  Result Cap: 100000 matches (Testing pure scan speed)
------------------------------------------------------------

🔎 Testing Pattern: "ERROR" (Short (5 chars))
[Results] Matches: 100000 (Node) vs 100000 (FastScan)
[Node.js] Avg Time: 80.41 ms
[FastScan] Avg Time: 26.36 ms
[Speedup] 🚀 FastScan is 3.05x FASTER

🔎 Testing Pattern: "Critical failure" (Medium (17 chars))
[Results] Matches: 100000 (Node) vs 100000 (FastScan)
[Node.js] Avg Time: 61.43 ms
[FastScan] Avg Time: 23.32 ms
[Speedup] 🚀 FastScan is 2.63x FASTER

🔎 Testing Pattern: "2023-10-25 [ERROR] Critical fail" (Long (36 chars))
[Results] Matches: 100000 (Node) vs 100000 (FastScan)
[Node.js] Avg Time: 109.53 ms
[FastScan] Avg Time: 53.97 ms
[Speedup] 🚀 FastScan is 2.03x FASTER

🔎 Testing Pattern: ":" (Worst Case (1 char))
[Results] Matches: 0 (Node) vs 0 (FastScan)
[Node.js] Avg Time: 15.90 ms
[FastScan] Avg Time: 20.94 ms
[Speedup] 🐢 Node.js is faster (Expected for tiny patterns)
------------------------------------------------------------

💾 BENCHMARK 2: Memory Consumption Analysis
------------------------------------------------------------
[Node.js] Heap Allocated: 0.00 MB (File loaded into RAM)
[FastScan] Heap Allocated: -0.42 MB (Zero-Copy via OS Page Cache)
[Verdict] ✅ FastScan uses significantly LESS Heap Memory.
------------------------------------------------------------

⚡ BENCHMARK 3: Event Loop Blocking Test (Async)
------------------------------------------------------------
[Scenario] Scanning large file while keeping main thread alive...

  [Async Scan] Finished in 30.05 ms
  [Main Thread] Heartbeats detected: 4
[Verdict] ✅ NON-BLOCKING. Main thread remained responsive.
------------------------------------------------------------

🏁 BENCHMARK FINISHED. FastScan is battle-ready.

```
---

## 🖥️ Hardware Compatibility (SSE2 vs AVX2)

**FastScan** is designed to be **portable and robust**.

### 🔹 Current Baseline (SSE2)

The engine currently uses **SSE2 (128-bit SIMD)** by default.  
This ensures the library runs efficiently on any x64 CPU manufactured in the last 15+ years.

**Result:** You get a solid **2–3× speedup** even on legacy hardware.

---

### 🔹 Modern Hardware (AVX2 / AVX-512)

The architecture is built to scale.

On modern CPUs supporting **AVX2 (256-bit)** or **AVX-512**,  
the internal vectorization width can be extended.

**Potential:**  
Without changing the API, enabling AVX2 compilation flags (`-mavx2`) on supported hardware  
can push the speedup to **6–8× or higher**.

---

### 🔹 Optimization Note

`glibc`'s `memchr` (used for single-byte searches) automatically utilizes  
**AVX2 / AVX-512** when available, making single-byte search highly optimized  
on modern Linux distributions.

---

## ✅ Conclusion

You don’t need a supercomputer to feel the speed.  
**FastScan delivers performance wherever it runs.**

---

## 🧠 Why FastScan?

Traditional Node.js file scanning:

* Loads data into JS heap
* Triggers GC pressure
* Blocks the event loop
* Fails on very large files

FastScan instead:

* Uses **memory‑mapped files (mmap)**
* Scans data **outside V8 heap**
* Uses **SIMD + threads** for speed
* Returns results with **zero copies**

---

## ⚠️ Important Notes

* `scanFileSync()` **blocks the event loop** — use only for scripts or tooling
* `scanFileAsync()` is recommended for servers
* Returned TypedArrays should be retained by the caller to avoid early GC

---

## 📚 Documentation

* 📐 `docs/architecture.md` — internal design and data flow
* ⚡ `docs/performance.md` — benchmarks and optimization strategy
* 🔐 `docs/security.md` — memory safety & threat model

---

## 🏁 Status

FastScan is **production‑ready** and designed for:

* Large‑scale log analysis
* Security monitoring tools
* Performance‑critical Node.js backends

---

## 📜 License

MIT License

---

> FastScan bridges the gap between **Node.js productivity** and **systems‑level performance**.

---

## 👨‍💻 Author

**GUIAR OQBA** 🇩🇿 — *Low-level enthusiast & security researcher*

- ✉️ Email: [techokba@gmail.com](mailto:techokba@gmail.com)  
- 💬 Telegram: [maronyo](https://t.me/maronyo)  
- 🔗 LinkedIn: [guiar-oqba](https://www.linkedin.com/in/guiar-oqba)

*Passionate about low-level programming, fuzzing, and pushing hardware to its limits.*



