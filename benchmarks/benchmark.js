const fs = require('fs');
const path = require('path');
const fastscan = require('./src/index');

// ==========================================
// CONFIGURATION
// ==========================================
const TARGET_FILE = path.join(__dirname, 'big_data.log');
const PATTERN = "ERROR";
const ITERATIONS = 5; 

if (!fs.existsSync(TARGET_FILE)) {
    console.error("Error: File 'big_data.log' not found.");
    process.exit(1);
}

// ==========================================
// UTILITIES
// ==========================================
const formatBytes = (bytes) => (bytes / 1024 / 1024).toFixed(2) + ' MB';

function getMemoryUsage() {
    const usage = process.memoryUsage();
    return {
        heapUsed: usage.heapUsed,
        rss: usage.rss,
        external: usage.external
    };
}

function printSeparator() {
    console.log("------------------------------------------------------------");
}

// ==========================================
// BENCHMARK 1: SYNC THROUGHPUT (Speed)
// ==========================================
async function runSyncBench() {
    console.log("\n🚀 BENCHMARK 1: Throughput Comparison (Buffer.indexOf vs FastScan)");
    printSeparator();

    // 1. Load File into Buffer (Best case for Node.js)
    console.log(`[Setup] Loading file into Node.js Buffer...`);
    const fileBuffer = fs.readFileSync(TARGET_FILE); 
    
    const nodeTimes = [];
    const fastTimes = [];
    let nodeMatches = 0;
    let fastMatches = 0;

    // Warmup run
    console.log(`[Setup] Warmup run...`);
    fs.readFileSync(TARGET_FILE); 
    fastscan.scanFile(TARGET_FILE, PATTERN, 10000000);

    // Real Runs
    for (let i = 0; i < ITERATIONS; i++) {
        // --- Native Node.js (Buffer.indexOf) ---
        process.gc && process.gc(); 
        const memBeforeNode = getMemoryUsage().heapUsed;
        
        const startNode = process.hrtime.bigint();
        let nMatches = 0;
        let pos = 0;
        while (true) {
            const idx = fileBuffer.indexOf(PATTERN, pos);
            if (idx === -1) break;
            nMatches++;
            pos = idx + 1;
        }
        const endNode = process.hrtime.bigint();
        const timeNode = Number(endNode - startNode) / 1000000;
        nodeTimes.push(timeNode);
        nodeMatches = nMatches; 
        
        // --- FastScan ---
        process.gc && process.gc();
        const memBeforeFast = getMemoryUsage().heapUsed;

        const startFast = process.hrtime.bigint();
        const fMatches = fastscan.scanFile(TARGET_FILE, PATTERN, 10000000);
        const endFast = process.hrtime.bigint();
        const timeFast = Number(endFast - startFast) / 1000000;
        fastTimes.push(timeFast);
        fastMatches = fMatches;

        process.stdout.write(`\r  Run ${i + 1}/${ITERATIONS} ... Node: ${timeNode.toFixed(2)}ms | FastScan: ${timeFast.toFixed(2)}ms`);
    }
    console.log("\n");

    // Calculate Stats
    const avgNode = nodeTimes.reduce((a, b) => a + b, 0) / ITERATIONS;
    const avgFast = fastTimes.reduce((a, b) => a + b, 0) / ITERATIONS;
    
    console.log(`[Results] Matches: ${nodeMatches}`);
    console.log(`[Node.js] Avg Time: ${avgNode.toFixed(2)} ms`);
    console.log(`[FastScan] Avg Time: ${avgFast.toFixed(2)} ms`);
    console.log(`[Speedup] FastScan is ${(avgNode / avgFast).toFixed(2)}x FASTER`);
    printSeparator();
}

// ==========================================
// BENCHMARK 2: MEMORY FOOTPRINT
// ==========================================
function runMemoryBench() {
    console.log("\n💾 BENCHMARK 2: Memory Consumption Analysis");
    printSeparator();

    process.gc && process.gc();
    const startMem = process.memoryUsage().heapUsed;

    // Node.js Way: Read into memory
    const buffer = fs.readFileSync(TARGET_FILE);
    const midMem = process.memoryUsage().heapUsed;
    const nodeMemUsed = midMem - startMem;

    // FastScan Way: Mmap (Virtual Memory, not Heap)
    fastscan.scanFile(TARGET_FILE, PATTERN, 10000000);
    const endMem = process.memoryUsage().heapUsed;
    const fastMemUsed = endMem - midMem; 

    console.log(`[Node.js] Heap Allocated: ${formatBytes(nodeMemUsed)}`);
    console.log(`[FastScan] Heap Allocated: ${formatBytes(fastMemUsed)}`);
    console.log(`[Verdict] FastScan uses ${((nodeMemUsed / (fastMemUsed || 1))).toFixed(0)}x LESS Heap Memory`);
    console.log(`(Note: FastScan uses OS Page Cache, not Heap, allowing scanning GBs of files)`);
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
    const intervalId = setInterval(() => {
        tasksCompleted++;
        process.stdout.write(`\r  [Main Thread] Heartbeat... Tasks Completed: ${tasksCompleted}`);
    }, 1); // Try to run every ms

    const startAsync = process.hrtime.bigint();
    
    // Execute Async Scan
    await fastscan.scanFileAsync(TARGET_FILE, PATTERN, 10000000);
    
    const endAsync = process.hrtime.bigint();
    const timeAsync = Number(endAsync - startAsync) / 1000000;

    clearInterval(intervalId);
    console.log(`\n  [Async Scan] Finished in ${timeAsync.toFixed(2)} ms`);
    console.log(`  [Main Thread] Total Tasks Executed: ${tasksCompleted}`);
    
    if (tasksCompleted > 0) {
        console.log(`[Verdict] ✅ NON-BLOCKING. Main thread was alive during scan.`);
    } else {
        console.log(`[Verdict] ❌ BLOCKING. Main thread froze.`);
    }
    printSeparator();
}

// ==========================================
// MAIN EXECUTION
// ==========================================
(async () => {
    try {
        await runSyncBench();
        runMemoryBench();
        await runAsyncBlockingTest();
        
        console.log("\n🏁 BENCHMARK FINISHED. FastScan is ready.");
    } catch (err) {
        console.error("Benchmark Failed:", err);
    }
})();
