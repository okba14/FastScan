const fastscan = require('../src/index');
const fs = require('fs');
const path = require('path');
const assert = require('assert');

const testDir = path.join(__dirname, 'sandbox');
if (!fs.existsSync(testDir)) {
    fs.mkdirSync(testDir, { recursive: true });
}

function runTests() {
    console.log("=========================================");
    console.log("🧪 Running FastScan Ultra Test Suite");
    console.log("=========================================\n");

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
        // 1. Basic Synchronous Scan
        test("Basic sync scan finds correct offsets", () => {
            const file = path.join(testDir, 'test_basic.txt');
            fs.writeFileSync(file, "ALPHA BRAVO CHARLIE ALPHA DELTA", 'utf8');
            const res = fastscan.scanFile(file, "ALPHA", 10);
            assert.strictEqual(res.length, 2);
            assert.strictEqual(res[0], 0n);
            assert.strictEqual(res[1], 20n);
        });

        // 2. Pattern at the absolute end of the file
        test("Pattern at the exact end of file", () => {
            const file = path.join(testDir, 'test_end.txt');
            fs.writeFileSync(file, "1234567890TAIL", 'utf8');
            const res = fastscan.scanFile(file, "TAIL", 10);
            assert.strictEqual(res.length, 1);
            assert.strictEqual(res[0], 10n);
        });

        // 3. Single-byte search
        test("Single-byte search (worst-case scalar/vector)", () => {
            const file = path.join(testDir, 'test_single.txt');
            fs.writeFileSync(file, "a:b:c:d:", 'utf8');
            const res = fastscan.scanFile(file, ":", 10);
            assert.strictEqual(res.length, 4);
            assert.deepStrictEqual(Array.from(res), [1n, 3n, 5n, 7n]);
        });

        // 4. Pattern longer than file
        test("Pattern longer than file returns 0 matches gracefully", () => {
            const file = path.join(testDir, 'test_short.txt');
            fs.writeFileSync(file, "ABC", 'utf8');
            const res = fastscan.scanFile(file, "LONGPATTERNTHATCANNOTFIT", 10);
            assert.strictEqual(res.length, 0);
        });

        // 5. Empty file handling
        test("Empty file returns 0 matches without crashing", () => {
            const file = path.join(testDir, 'test_empty.txt');
            fs.writeFileSync(file, "", 'utf8');
            const res = fastscan.scanFile(file, "TEST", 10);
            assert.strictEqual(res.length, 0);
        });

        // 6. Native Context Extraction (Zero-Syscall)
        await testAsync("scanWithContext extracts accurate text snippets", async () => {
            const file = path.join(testDir, 'test_ctx.txt');
            fs.writeFileSync(file, "Line 1: OK\nLine 2: ERROR in module\nLine 3: OK", 'utf8');
            const results = await fastscan.scanWithContext(file, "ERROR", { contextBefore: 5, contextAfter: 10 });
            assert.strictEqual(results.length, 1);
            assert.ok(results[0].snippet.includes("ERROR in module"));
        });

        // 7. Async Non-Blocking Scan
        await testAsync("scanFileAsync resolves with matches in background", async () => {
            const file = path.join(testDir, 'test_async.txt');
            fs.writeFileSync(file, "ASYNC_TEST_DATA_MATCH_HERE", 'utf8');
            const res = await fastscan.scanFileAsync(file, "MATCH", 10);
            assert.strictEqual(res.length, 1);
            assert.strictEqual(res[0], 16n);
        });

        // 8. Stream / Iterator API
        await testAsync("scanIterator yields items sequentially", async () => {
            const file = path.join(testDir, 'test_iter.txt');
            fs.writeFileSync(file, "KEY1 and KEY2 and KEY3", 'utf8');
            const matches = [];
            for await (const item of fastscan.scanIterator(file, "KEY", 10)) {
                matches.push(item.offset);
            }
            assert.strictEqual(matches.length, 3);
        });

        // 9. Error Handling: Non-existent file
        test("Non-existent file throws FileNotFoundError", () => {
            assert.throws(() => {
                fastscan.scanFile("non_existent_file_12345.log", "TEST", 10);
            }, fastscan.errors.FileNotFoundError);
        });

        // 10. Error Handling: Invalid arguments
        test("Invalid arguments throw InvalidArgumentError", () => {
            assert.throws(() => {
                fastscan.scanFile(123, "TEST", 10);
            }, fastscan.errors.InvalidArgumentError);
            assert.throws(() => {
                fastscan.scanFile("path", "", 10);
            }, fastscan.errors.InvalidArgumentError);
            assert.throws(() => {
                fastscan.scanFile("path", "TEST", -5);
            }, fastscan.errors.InvalidArgumentError);
        });

        // 11. Hardware Feature Introspection
        test("getCpuFeatures reports detected SIMD capabilities", () => {
            const cpu = fastscan.getCpuFeatures();
            assert.ok(typeof cpu.sse2 === 'boolean');
            assert.ok(typeof cpu.avx2 === 'boolean');
            assert.ok(typeof cpu.avx512 === 'boolean');
        });

        // 12. Pathological Repetition Stress Test (Algorithmic DoS Resistance)
        test("Pathological repeated character string runs without freeze", () => {
            const file = path.join(testDir, 'test_stress.txt');
            const data = "a".repeat(200000) + "TARGET";
            fs.writeFileSync(file, data, 'utf8');
            const res = fastscan.scanFile(file, "TARGET", 10);
            assert.strictEqual(res.length, 1);
            assert.strictEqual(res[0], 200000n);
        });

        // 13. Multi-Pattern Single-Pass Scanner
        test("scanFileMulti finds multiple signatures in single pass", () => {
            const file = path.join(testDir, 'test_multi.txt');
            fs.writeFileSync(file, "ALPHA 123 BETA 456 GAMMA 789 ALPHA", 'utf8');
            const res = fastscan.scanFileMulti(file, ["ALPHA", "BETA", "GAMMA"], 10);
            assert.strictEqual(res.length, 4);
            assert.strictEqual(res[0].pattern, "ALPHA");
            assert.strictEqual(res[1].pattern, "BETA");
            assert.strictEqual(res[2].pattern, "GAMMA");
            assert.strictEqual(res[3].pattern, "ALPHA");
        });

        // 14. Vectorized Line & Column Indexer
        await testAsync("scanWithPositions accurately calculates line and column numbers", async () => {
            const file = path.join(testDir, 'test_lines.txt');
            fs.writeFileSync(file, "Line1\nLine2\nFINDME here\nLine4", 'utf8');
            const res = await fastscan.scanWithPositions(file, "FINDME", { contextBefore: 0, contextAfter: 5 });
            assert.strictEqual(res.length, 1);
            assert.strictEqual(res[0].line, 3);
            assert.strictEqual(res[0].column, 1);
            assert.strictEqual(res[0].snippet, "FINDME here");
        });

        console.log("\n=========================================");
        console.log(`Results: ${passed} Passed, ${failed} Failed`);
        console.log("=========================================");

        try { fs.rmSync(testDir, { recursive: true, force: true }); } catch (e) {}

        if (failed > 0) process.exit(1);
    })();
}

runTests();
