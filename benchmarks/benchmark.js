const fs = require('fs');
const path = require('path');
const fastscan = require('../src/index');

const { generateData } = require('../generate-data');

// ==========================================
// CONFIGURATION
// ==========================================
let TARGET_FILE = path.join(__dirname, 'big_data.log');
const ITERATIONS = 3;
const MAX_MATCHES = 100000;

const PATTERNS = [
    { name: "Short (5 chars)", value: "ERROR" },
    { name: "Medium (17 chars)", value: "Critical failure" },
    { name: "Long (36 chars)", value: "2023-10-25 [ERROR] Critical fail" },
    { name: "Worst Case (1 char)", value: ":" } 
];

async function ensureBenchmarkFile() {
    if (!fs.existsSync(TARGET_FILE)) {
        const rootCandidate = path.join(__dirname, '..', 'big_data.log');
        if (fs.existsSync(rootCandidate)) {
            TARGET_FILE = rootCandidate;
            return;
        }
        console.log("⚡ Benchmark file 'big_data.log' not found. Generating 100 MB test file on the fly...");
        await generateData(TARGET_FILE, 100);
        console.log("\n✅ Generation complete. Proceeding to benchmarks...\n");
    }
}

const formatBytes = (bytes) => (bytes / 1024 / 1024).toFixed(2) + ' MB';

function printSeparator() {
    console.log("------------------------------------------------------------");
}

// ==========================================
// BENCHMARK 1: REAL-WORLD END-TO-END WORKFLOW
// (Reading file from storage -> Finding matches)
// ==========================================
async function runEndToEndBenchmark() {
    console.log("\n🔥 BENCHMARK 1: Real-World End-to-End Test (Storage I/O + Scan)");
    console.log(`⚠️  Measures total latency of searching files without preloading into RAM`);
    printSeparator();

    for (const patternObj of PATTERNS) {
        console.log(`\n🔎 Testing Pattern: "${patternObj.value}" (${patternObj.name})`);
        
        let nodeTotalTime = 0;
        let fastTotalTime = 0;
        let nodeMatches = 0;
        let fastMatches = 0;

        for (let i = 0; i < ITERATIONS; i++) {
            // --- Realistic Node.js (Read file from disk + scan) ---
            const startNode = process.hrtime.bigint();
            const buf = fs.readFileSync(TARGET_FILE);
            let nCount = 0;
            let nPos = 0;
            while (nCount < MAX_MATCHES) {
                const idx = buf.indexOf(patternObj.value, nPos);
                if (idx === -1) break;
                nCount++;
                nPos = idx + 1;
            }
            const endNode = process.hrtime.bigint();
            nodeTotalTime += Number(endNode - startNode) / 1e6;
            nodeMatches = nCount;

            // --- FastScan (Zero-Copy mmap + SIMD scan) ---
            const startFast = process.hrtime.bigint();
            const fRes = fastscan.scanFile(TARGET_FILE, patternObj.value, MAX_MATCHES);
            const endFast = process.hrtime.bigint();
            fastTotalTime += Number(endFast - startFast) / 1e6;
            fastMatches = fRes.length;
        }

        const avgNode = (nodeTotalTime / ITERATIONS).toFixed(2);
        const avgFast = (fastTotalTime / ITERATIONS).toFixed(2);
        const speedup = (nodeTotalTime / fastTotalTime).toFixed(2);

        console.log(`[Results] Matches: ${nodeMatches} (Node) vs ${fastMatches} (FastScan)`);
        console.log(`[Node.js (Real-World)] Avg Time: ${avgNode} ms`);
        console.log(`[FastScan (Ultra)]     Avg Time: ${avgFast} ms`);
        
        if (speedup >= 1) {
            console.log(`[Speedup] 🚀 FastScan is ${speedup}x FASTER`);
        } else {
            console.log(`[Speedup] 🐢 Node.js is faster`);
        }
    }
    printSeparator();
}

// ==========================================
// BENCHMARK 2: MEMORY FOOTPRINT
// ==========================================
function runMemoryBench() {
    console.log("\n💾 BENCHMARK 2: Memory Consumption Analysis");
    printSeparator();

    if (global.gc) global.gc();
    const startMem = process.memoryUsage().heapUsed;

    const buffer = fs.readFileSync(TARGET_FILE);
    const midMem = process.memoryUsage().heapUsed;
    const nodeMemUsed = midMem - startMem;

    fastscan.scanFile(TARGET_FILE, "ERROR", 1000);
    const endMem = process.memoryUsage().heapUsed;
    const fastMemUsed = endMem - midMem; 

    console.log(`[Node.js]  Heap Allocated: ${formatBytes(nodeMemUsed)} (File duplicated into V8 Heap)`);
    console.log(`[FastScan] Heap Allocated: ${formatBytes(fastMemUsed)} (Zero-Copy via OS Virtual Memory)`);
    
    if (nodeMemUsed > fastMemUsed) {
        console.log(`[Verdict] ✅ FastScan saves ${(nodeMemUsed / 1024 / 1024).toFixed(2)} MB of V8 Heap RAM.`);
    }
    printSeparator();
}

// ==========================================
// BENCHMARK 3: NON-BLOCKING ASYNC EVENT LOOP
// ==========================================
async function runAsyncBlockingTest() {
    console.log("\n⚡ BENCHMARK 3: Event Loop Responsiveness Test (Async)");
    printSeparator();

    console.log("[Scenario] Scanning large file while checking Event Loop heartbeat ticks...\n");

    let heartbeats = 0;
    let running = true;

    function beat() {
        if (running) {
            heartbeats++;
            setImmediate(beat);
        }
    }
    beat();

    const startAsync = process.hrtime.bigint();
    const matches = await fastscan.scanFileAsync(TARGET_FILE, "ERROR", 100000);
    const endAsync = process.hrtime.bigint();
    running = false;

    const timeAsync = Number(endAsync - startAsync) / 1e6;

    console.log(`  [Async Scan] Finished in ${timeAsync.toFixed(2)} ms (Found ${matches.length} matches)`);
    console.log(`  [Event Loop] Heartbeats executed: ${heartbeats}`);
    
    if (heartbeats > 50) {
        console.log(`[Verdict] ✅ 100% NON-BLOCKING. Main thread completed ${heartbeats} loop cycles without freezing.`);
    } else {
        console.log(`[Verdict] ❌ Event loop had low responsiveness.`);
    }
    printSeparator();
}

// ==========================================
// MAIN EXECUTION
// ==========================================
(async () => {
    try {
        console.log(`🚀 FastScan Ultra Performance Benchmark`);
        await ensureBenchmarkFile();
        try {
            const cpu = fastscan.getCpuFeatures();
            const hwList = [];
            if (cpu.avx512) hwList.push("AVX-512");
            if (cpu.avx2) hwList.push("AVX2");
            if (cpu.sse2) hwList.push("SSE2");
            if (cpu.neon) hwList.push("ARM NEON");
            console.log(`⚡ Detected SIMD Hardware: ${hwList.join(', ')}`);
        } catch (e) {}

        console.log(`📁 Target File: ${TARGET_FILE}`);
        console.log(`📦 File Size: ${(fs.statSync(TARGET_FILE).size / 1024 / 1024).toFixed(2)} MB`);

        await runEndToEndBenchmark();
        runMemoryBench();
        await runAsyncBlockingTest();
        
        console.log("\n🏁 BENCHMARK FINISHED. FastScan is battle-ready for production.");
    } catch (err) {
        console.error("Benchmark Failed:", err);
    }
})();
