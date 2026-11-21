"use strict";

const http = require("node:http");
const fs = require("node:fs");
const path = require("node:path");

const directory = process.env.LOG_DIR || path.join(__dirname, "logs");
const port = Number(process.env.PORT || 8080);
const maxBytes = 65536;
fs.mkdirSync(directory, {recursive: true});
const filename = path.join(directory, `${new Date().toISOString().replace(/[:.]/g, "-")}-${process.pid}.log`);
fs.writeFileSync(filename, "", {flag: "wx"});

const server = http.createServer((request, response) => {
    if (request.method !== "POST" || request.url !== "/") {
        response.writeHead(404).end("Not found");
        return;
    }
    let size = 0;
    const chunks = [];
    request.on("data", chunk => {
        size += chunk.length;
        if (size <= maxBytes) chunks.push(chunk);
    });
    request.on("end", () => {
        if (size > maxBytes) {
            response.writeHead(413).end("Log message too large");
            return;
        }
        fs.appendFile(filename, Buffer.concat([...chunks, Buffer.from("\n")]), error => {
            if (error) {
                console.error("Could not write log:", error);
                response.writeHead(500).end("Could not write log");
                return;
            }
            response.writeHead(204).end();
        });
    });
});
server.on("error", error => {
    console.error("Log server failed:", error);
    process.exitCode = 1;
});
server.listen(port, "0.0.0.0", () => {
    console.log(`Listening on port ${server.address().port}; logging to ${filename}`);
});
