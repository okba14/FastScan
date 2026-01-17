const fastscan = require('./src/index');
const path = require('path');


const fs = require('fs');
const testFile = path.join(__dirname, 'test_data.log');
const content = "Hello World\nERROR: Something broke\nINFO: All good\nERROR: Another error\n";

fs.writeFileSync(testFile, content, 'utf8');

console.log("Starting FastScan...");

try {
    const matches = fastscan.scanFile(testFile, "ERROR", 100);
    
    console.log(`Found ${matches.length} matches.`);
    console.log("Offsets:", matches);


    if (matches.length === 2) {
        console.log("✅ Success! Project is working.");
    } else {
        console.log("❌ Wrong number of matches.");
    }
} catch (err) {
    console.error("Error:", err.message);
} finally {
    
}
