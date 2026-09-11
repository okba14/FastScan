const fs = require('fs');
const path = require('path');

function generateData(targetFile, sizeMB = 100) {
    return new Promise((resolve, reject) => {
        const line = "2023-10-25 [INFO] Processing data stream...\n2023-10-25 [DEBUG] Memory check OK\n";
        const errorLine = "2023-10-25 [ERROR] Critical failure detected at index ID\n";

        const totalSize = sizeMB * 1024 * 1024;
        let written = 0;
        const CHUNK_SIZE = 1024 * 1024;

        const writeStream = fs.createWriteStream(targetFile);

        writeStream.on('error', reject);
        writeStream.on('finish', () => {
            resolve(targetFile);
        });

        function writeChunk() {
            if (written >= totalSize) {
                writeStream.end();
                return;
            }

            const buffer = Buffer.alloc(CHUNK_SIZE);
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

            if (process.stdout.isTTY) {
                process.stdout.write(`\r[DataGen] Progress: ${(written / (1024 * 1024)).toFixed(1)} / ${sizeMB} MB`);
            }

            if (!ok) {
                writeStream.once('drain', writeChunk);
            } else {
                setImmediate(writeChunk);
            }
        }

        writeChunk();
    });
}

if (require.main === module) {
    const sizeMB = parseInt(process.argv[2], 10) || 100;
    const target = path.join(__dirname, 'big_data.log');
    console.log(`Generating ${sizeMB} MB test dataset at ${target}...`);
    generateData(target, sizeMB).then(() => {
        console.log(`\n✅ Generated successfully: ${target}`);
    }).catch(err => {
        console.error("Failed to generate data:", err);
        process.exit(1);
    });
}

module.exports = { generateData };
