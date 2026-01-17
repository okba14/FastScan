# FastScan 🚀

FastScan is a **high‑performance file scanning engine for Node.js** designed to search massive files (GB‑scale logs) with **near‑native speed**, **minimal memory usage**, and **non‑blocking async execution**.

It combines:

* ⚙️ Native C (POSIX + mmap)
* ⚡ SIMD acceleration (SSE2)
* 🧵 Parallel scanning (multi‑core)
* 🔒 Safe memory ownership with Zero‑Copy results
* 🟢 Clean Node.js API (sync + async)

FastScan is built for developers who need **real systems‑level performance** without leaving the Node.js ecosystem.

---

## ✨ Key Features

* **Up to 8× faster** than `Buffer.indexOf`
* **Zero‑copy results** using `ExternalArrayBuffer`
* **Scans files larger than RAM** (uses OS page cache)
* **BigInt offsets** (supports files > 2GB safely)
* **Async, non‑blocking API**
* **Parallel execution** using all CPU cores
* **CLI tool** for direct usage

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

### 2️⃣ Install Dependencies

```bash
npm install
```

---

### 3️⃣ Build Native Addon

```bash
npm run rebuild
```

This compiles the native scanner using `node-gyp`.

---

## 🧪 Basic Usage

### ▶️ Run Tests

```bash
node test.js
node async-test.js
```

---

### 🖥️ CLI Usage

Search for the word **"ERROR"** in a large log file:

```bash
node cli.js big_data.log "ERROR" 50
```

**Arguments:**

1. File path
2. Search pattern
3. Maximum number of matches

---

### 📄 Example Output

```text
[*] Scanning: big_data.log
[*] Pattern: "ERROR"
[*] Please wait...

✅ Scan Finished.
   Found 88 matches

[Match #1] Offset: 407
2023-10-25 [ERROR] Critical failure detected
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
node benchmark.js
```

FastScan benchmarks itself against native Node.js scanning methods.

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





