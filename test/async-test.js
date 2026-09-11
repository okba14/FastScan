const fastscan = require('../src/index');
const path = require('path');
const fs = require('fs');

const testFile = path.join(__dirname, 'async_test_temp.log');
const pattern = "ERROR";

// Create 2MB test file on the fly
fs.writeFileSync(testFile, "2023-10-25 [INFO] Processing data stream...\n2023-10-25 [ERROR] Critical failure detected\n".repeat(25000), 'utf8');

console.log(`\n--- Async Test Started (Checking Non-Blocking behavior) ---\n`);
console.log(`[Main Thread] I am alive! Starting search...`);

const startTime = Date.now();

fastscan.scanFileAsync(testFile, pattern, 100000)
    .then((matches) => {
        const duration = Date.now() - startTime;
        console.log(`\n[Worker Thread] Scan finished!`);
        console.log(`[Worker Thread] Found ${matches.length} matches in ${duration} ms`);
        console.log(`[Main Thread] Promise resolved. Everything is smooth.`);
        try { fs.unlinkSync(testFile); } catch (e) {}
    })
    .catch((err) => {
        console.error("Error:", err.message);
        try { fs.unlinkSync(testFile); } catch (e) {}
    });


console.log(`[Main Thread] I am NOT blocked! Search is running in background.`);
console.log(`[Main Thread] I can do other tasks now...`);


let busyCounter = 0;
const checkInterval = setInterval(() => {
    busyCounter++;
    console.log(`[Main Thread] Doing other work... (${busyCounter})`);
    
    if (busyCounter > 5) {
        clearInterval(checkInterval);
        console.log(`[Main Thread] Waiting for search to finish...\n`);
    }
}, 20); 
