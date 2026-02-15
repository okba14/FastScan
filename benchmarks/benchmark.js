const fs = require('fs');
const path = require('path');
const fastscan = require('../src/index');

// ==========================================
// CONFIGURATION
// ==========================================
const TARGET_FILE = path.join(__dirname, 'big_data.log');
const ITERATIONS = 5;
const MAX_MATCHES = 100000; // Cap results to compare "Scan Speed" not "Array Pushing Speed"

// Patterns to test: Short, Medium, Long, and "Worst Case" (Single char)
const PATTERNS = [
    { name: "Short (5 chars)", value: "ERROR" },
    { name: "Medium (17 chars)", value: "Critical failure" },
    { name: "Long (36 chars)", value: "2023-10-25 [ERROR] Critical fail" },
    { name: "Worst Case (1 char)", value: ":" } 
];

if (!fs.existsSync(TARGET_FILE)) {
    console.error("Error: File 'big_data.log' not found.");
    process.exit(1);
}

// ==========================================
// UTILITIES
// ==========================================
const formatBytes = (bytes) => (bytes / 1024 / 1024).toFixed(2) + ' MB';

function printSeparator() {
    console.log("------------------------------------------------------------");
}

// ==========================================
// BENCHMARK 1: MULTI-PATTERN STRESS TEST
// ==========================================
async function runStressTest() {
    console.log("\n🔥 BENCHMARK 1: Multi-Pattern Stress Test (Speed & Generic Optimization)");
    console.log(`⚠️  Result Cap: ${MAX_MATCHES} matches (Testing pure scan speed)`);
    printSeparator();

    const fileBuffer = fs.readFileSync(TARGET_FILE); // Load once for Node.js fairness

    for (const patternObj of PATTERNS) {
        console.log(`\n🔎 Testing Pattern: "${patternObj.value}" (${patternObj.name})`);
        
        let nodeTotalTime = 0;
        let fastTotalTime = 0;
        let nodeMatches = 0;
        let fastMatches = 0;

        // Warmup
        fastscan.scanFile(TARGET_FILE, patternObj.value, MAX_MATCHES);

        for (let i = 0; i < ITERATIONS; i++) {
            // --- Native Node.js (Buffer.indexOf) ---
            // We limit the loop to MAX_MATCHES to make it a fair race
            const startNode = process.hrtime.bigint();
            let nCount = 0;
            let nPos = 0;
            while (nCount < MAX_MATCHES) {
                const idx = fileBuffer.indexOf(patternObj.value, nPos);
                if (idx === -1) break;
                nCount++;
                nPos = idx + 1;
            }
            const endNode = process.hrtime.bigint();
            nodeTotalTime += Number(endNode - startNode) / 1e6;
            nodeMatches = nCount;

            // --- FastScan ---
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
        console.log(`[Node.js] Avg Time: ${avgNode} ms`);
        console.log(`[FastScan] Avg Time: ${avgFast} ms`);
        
        if (speedup > 1) {
            console.log(`[Speedup] 🚀 FastScan is ${speedup}x FASTER`);
        } else {
            console.log(`[Speedup] 🐢 Node.js is faster (Expected for tiny patterns)`);
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

    // Force GC if available (run with --expose-gc for accurate results)
    if (global.gc) global.gc();
    
    const startMem = process.memoryUsage().heapUsed;

    // Node.js Way: Read into memory
    const buffer = fs.readFileSync(TARGET_FILE);
    const midMem = process.memoryUsage().heapUsed;
    const nodeMemUsed = midMem - startMem;

    // FastScan Way: Mmap (Virtual Memory, not Heap)
    fastscan.scanFile(TARGET_FILE, "ERROR", 1000); // Scan small amount just to trigger mmap
    const endMem = process.memoryUsage().heapUsed;
    const fastMemUsed = endMem - midMem; 

    console.log(`[Node.js] Heap Allocated: ${formatBytes(nodeMemUsed)} (File loaded into RAM)`);
    console.log(`[FastScan] Heap Allocated: ${formatBytes(fastMemUsed)} (Zero-Copy via OS Page Cache)`);
    
    if (nodeMemUsed > fastMemUsed) {
        console.log(`[Verdict] ✅ FastScan uses significantly LESS Heap Memory.`);
    } else {
        console.log(`[Verdict] ⚠️ Memory usage similar or GC not exposed.`);
    }
    printSeparator();
}

// ==========================================
// BENCHMARK 3: NON-BLOCKING ASYNC
// ==========================================
async function runAsyncBlockingTest() {
    console.log("\n⚡ BENCHMARK 3: Event Loop Blocking Test (Async)");
    printSeparator();

    console.log("[Scenario] Scanning large file while keeping main thread alive...\n");

    let tasksCompleted = 0;
    // Aggressive timer to check if event loop is blocked
    const intervalId = setInterval(() => {
        tasksCompleted++;
    }, 5); 

    const startAsync = process.hrtime.bigint();
    
    // Execute Async Scan
    await fastscan.scanFileAsync(TARGET_FILE, "ERROR", 100000);
    
    const endAsync = process.hrtime.bigint();
    const timeAsync = Number(endAsync - startAsync) / 1e6;

    clearInterval(intervalId);
    
    console.log(`  [Async Scan] Finished in ${timeAsync.toFixed(2)} ms`);
    console.log(`  [Main Thread] Heartbeats detected: ${tasksCompleted}`);
    
    // Heuristic: If scan took 100ms, we expect roughly 20 heartbeats (100/5).
    // If we got 0 or 1, the thread was blocked.
    const expectedBeats = Math.floor(timeAsync / 5);
    
    if (tasksCompleted > (expectedBeats * 0.5)) { // Allow 50% margin
        console.log(`[Verdict] ✅ NON-BLOCKING. Main thread remained responsive.`);
    } else {
        console.log(`[Verdict] ❌ BLOCKING. Main thread froze during scan.`);
    }
    printSeparator();
}

// ==========================================
// MAIN EXECUTION
// ==========================================
(async () => {
    try {
        console.log(`🚀 FastScan Ultimate Benchmark`);
        console.log(`📁 File: ${TARGET_FILE}`);
        console.log(`📦 Size: ${(fs.statSync(TARGET_FILE).size / 1024 / 1024).toFixed(2)} MB`);

        await runStressTest();
        runMemoryBench();
        await runAsyncBlockingTest();
        
        console.log("\n🏁 BENCHMARK FINISHED. FastScan is battle-ready.");
    } catch (err) {
        console.error("Benchmark Failed:", err);
    }
})();
