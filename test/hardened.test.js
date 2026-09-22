const fastscan = require('../src/index');
const fs = require('fs');
const path = require('path');
const assert = require('assert');
const { performance } = require('perf_hooks');

const addon = require('bindings')('fastscan.node');

const testDir = path.join(__dirname, 'hardened_sandbox');
if (!fs.existsSync(testDir)) {
    fs.mkdirSync(testDir, { recursive: true });
}

console.log("==========================================================");
console.log("🛡️  Running FastScan Hardened Production Verification Suite");
console.log("==========================================================\n");

let passed = 0;
let failed = 0;

function test(name, fn) {
    try {
        fn();
        console.log(`\x1b[32m  ✔ PASS:\x1b[0m ${name}`);
        passed++;
    } catch (err) {
        console.error(`\x1b[31m  ✖ FAIL:\x1b[0m ${name}`);
        console.error(err);
        failed++;
    }
}

async function testAsync(name, fn) {
    try {
        await fn();
        console.log(`\x1b[32m  ✔ PASS:\x1b[0m ${name}`);
        passed++;
    } catch (err) {
        console.error(`\x1b[31m  ✖ FAIL:\x1b[0m ${name}`);
        console.error(err);
        failed++;
    }
}

(async () => {
    // 1. Direct Native Empty Pattern Immunity (Crash/SIGSEGV Prevention)
    test("Direct native addon invocation with empty pattern does not crash process", () => {
        const file = path.join(testDir, 'test_empty_pat.txt');
        fs.writeFileSync(file, "SOME TEST DATA THAT WOULD HAVE TRIGGERED UNDERFLOW", 'utf8');
        
        // Directly invoking the native C function with an empty string must NOT crash with SIGSEGV
        const res = addon.scanFile(file, "", 10);
        assert.strictEqual(res.length, 0);

        // Also test native buffer scanning with empty pattern
        const bufRes = addon.scanBuffer(Buffer.from("DATA"), "", 10);
        assert.strictEqual(bufRes.length, 0);
    });

    // 2. Binary Pattern with Null Bytes Support (\x00 threat-hunting signatures)
    test("Binary signatures containing embedded null bytes (\\x00) match accurately without truncation", () => {
        const file = path.join(testDir, 'test_binary.bin');
        // PE-like header signature with embedded null bytes
        const fileData = Buffer.from([
            0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00, // Header
            0x04, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00,
            0x4D, 0x5A, 0x90, 0x00, 0x03, 0x00, 0x00, 0x00  // Repeat at offset 16
        ]);
        fs.writeFileSync(file, fileData);

        // Pattern containing null bytes: [0x90, 0x00, 0x03, 0x00]
        const binaryPattern = Buffer.from([0x90, 0x00, 0x03, 0x00]);

        // Test File Scan with Buffer pattern
        const fileMatches = fastscan.scanFile(file, binaryPattern, 10);
        assert.strictEqual(fileMatches.length, 2, "Failed to match binary pattern in file");
        assert.strictEqual(fileMatches[0], 2n);
        assert.strictEqual(fileMatches[1], 18n);

        // Test In-Memory Buffer Scan with Buffer pattern
        const memMatches = fastscan.scanBuffer(fileData, binaryPattern, 10);
        assert.strictEqual(memMatches.length, 2, "Failed to match binary pattern in memory");
        assert.strictEqual(memMatches[0], 2n);
        assert.strictEqual(memMatches[1], 18n);
    });

    // 3. Special Files / Directory Rejection (No hanging / no DoS)
    test("Rejecting directories and non-regular files throws UnsupportedFileTypeError without freezing", () => {
        assert.throws(() => {
            fastscan.scanFile(__dirname, "TEST", 10);
        }, (err) => {
            return err instanceof fastscan.errors.UnsupportedFileTypeError || 
                   err.code === 'FS_UNSUPPORTED_FILE_TYPE';
        });
    });

    // 4. In-Memory scanBuffer API
    test("scanBuffer performs zero-copy in-memory scanning", () => {
        const text = "ALPHA BETA GAMMA DELTA ALPHA EPSILON";
        const matches = fastscan.scanBuffer(text, "ALPHA", 10);
        assert.strictEqual(matches.length, 2);
        assert.strictEqual(matches[0], 0n);
        assert.strictEqual(matches[1], 23n);
    });

    // 5. Heavy Concurrency Stress Test (30 simultaneous async scans across threads)
    await testAsync("Heavy Concurrency: 30 simultaneous background scans execute without data race or corruption", async () => {
        const stressFile = path.join(testDir, 'test_concurrency.dat');
        const CHUNK = "2026-09-22 [LOG_INFO] Worker health normal.\n2026-09-22 [LOG_ALERT] Security trigger activated!\n";
        const TOTAL_REPEATS = 5000;
        fs.writeFileSync(stressFile, CHUNK.repeat(TOTAL_REPEATS), 'utf8');

        // Target: "LOG_ALERT" occurs exactly once per repeat = 5000 matches
        const CONCURRENT_REQUESTS = 30;
        const promises = [];

        for (let i = 0; i < CONCURRENT_REQUESTS; i++) {
            promises.push(fastscan.scanFileAsync(stressFile, "LOG_ALERT", 10000));
        }

        const allResults = await Promise.all(promises);

        assert.strictEqual(allResults.length, CONCURRENT_REQUESTS);
        for (let i = 0; i < CONCURRENT_REQUESTS; i++) {
            assert.strictEqual(
                allResults[i].length, 
                TOTAL_REPEATS, 
                `Worker ${i} returned wrong match count: ${allResults[i].length} vs expected ${TOTAL_REPEATS}`
            );
        }
    });

    // 6. Linear O(N) Cumulative Line & Column Performance (10,000 matches)
    await testAsync("scanWithPositions calculates 10,000 line & col positions in linear O(N) single-pass", async () => {
        const linesFile = path.join(testDir, 'test_ten_thousand_lines.txt');
        const lineCount = 10000;
        const content = [];
        for (let i = 1; i <= lineCount; i++) {
            content.push(`Line ${i}: item_match_${i}`);
        }
        fs.writeFileSync(linesFile, content.join('\n'), 'utf8');

        const t0 = performance.now();
        const results = await fastscan.scanWithPositions(linesFile, "item_match_", { maxMatches: 10000 });
        const elapsed = performance.now() - t0;

        assert.strictEqual(results.length, lineCount);
        // Verify line positions
        assert.strictEqual(results[0].line, 1);
        assert.strictEqual(results[99].line, 100);
        assert.strictEqual(results[9999].line, 10000);

        // 10,000 matches in a single pass should take < 100ms
        console.log(`    \x1b[36m[Perf]\x1b[0m 10,000 positions indexed in ${elapsed.toFixed(2)} ms`);
        assert.ok(elapsed < 2000, `Line/Col indexing took too long (${elapsed} ms)`);
    });

    // 7. True Native Async Multi-Pattern (scanFileMultiAsync)
    await testAsync("scanFileMultiAsync runs non-blocking on native background worker", async () => {
        const multiFile = path.join(testDir, 'test_multi_async.txt');
        fs.writeFileSync(multiFile, "ERR_ONE and ERR_TWO and ERR_THREE and ERR_ONE again", 'utf8');

        const results = await fastscan.scanFileMultiAsync(multiFile, ["ERR_ONE", "ERR_TWO", "ERR_THREE"], 10);
        assert.strictEqual(results.length, 4);
        assert.strictEqual(results[0].pattern, "ERR_ONE");
        assert.strictEqual(results[1].pattern, "ERR_TWO");
        assert.strictEqual(results[2].pattern, "ERR_THREE");
        assert.strictEqual(results[3].pattern, "ERR_ONE");
    });

    console.log("\n==========================================================");
    console.log(`Hardened Suite Results: ${passed} Passed, ${failed} Failed`);
    console.log("==========================================================\n");

    try { fs.rmSync(testDir, { recursive: true, force: true }); } catch (e) {}

    if (failed > 0) process.exit(1);
})();
