const fs = require('fs');
const path = require('path');

const FILE_SIZE_MB = 100;
const TARGET_FILE = path.join(__dirname, 'big_data.log');

console.log(`Creating ${FILE_SIZE_MB}MB test file... (This might take a few seconds)`);

const writeStream = fs.createWriteStream(TARGET_FILE);


const line = "2023-10-25 [INFO] Processing data stream...\n2023-10-25 [DEBUG] Memory check OK\n";
const errorLine = "2023-10-25 [ERROR] Critical failure detected at index ID\n";

let totalSize = FILE_SIZE_MB * 1024 * 1024; 
let written = 0;
const CHUNK_SIZE = 1024 * 1024; 

writeStream.on('finish', () => {

    console.log("\n✅ File generation completed successfully!");
    
    const stats = fs.statSync(TARGET_FILE);
    console.log(`Final Size: ${(stats.size / (1024 * 1024)).toFixed(2)} MB`);
    console.log("You can now run: node benchmark.js");
});

function writeChunk() {
    if (written >= totalSize) {
        writeStream.end(); 
        return;
    }


    let buffer = Buffer.alloc(CHUNK_SIZE);
    let offset = 0;

    while (offset < CHUNK_SIZE) {
        if (Math.random() > 0.85) {

            offset += buffer.write(errorLine, offset);
        } else {
            offset += buffer.write(line, offset);
        }
    }


    const ok = writeStream.write(buffer);
    written += CHUNK_SIZE;


    process.stdout.write(`\rProgress: ${(written / (1024 * 1024)).toFixed(1)} MB`);

    if (!ok) {

        writeStream.once('drain', writeChunk);
    } else {

        setImmediate(writeChunk);
    }
}

writeChunk();
