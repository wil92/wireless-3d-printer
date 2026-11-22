"use strict";

const {test} = require("node:test");
const assert = require("node:assert/strict");
const {spawn} = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const {once} = require("node:events");

async function start(directory) {
    const child = spawn(process.execPath, [path.join(__dirname, "server.js")], {
        env: {...process.env, PORT: "0", LOG_DIR: directory},
        stdio: ["ignore", "pipe", "pipe"],
    });
    const [data] = await once(child.stdout, "data");
    const match = data.toString().match(/Listening on port (\d+); logging to (.+)\n/);
    assert.ok(match, data.toString());
    return {child, port: Number(match[1]), filename: match[2]};
}

async function stop(child) {
    child.kill("SIGTERM");
    await once(child, "exit");
}

test("writes POST messages and creates a new file on restart", async t => {
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), "wireless-3d-printer-logs-"));
    t.after(() => fs.rmSync(directory, {recursive: true, force: true}));
    const first = await start(directory);
    try {
        const url = `http://127.0.0.1:${first.port}/`;
        assert.equal((await fetch(url, {method: "POST", body: "printer connected"})).status, 204);
        assert.equal((await fetch(url, {method: "POST", body: "upload failed"})).status, 204);
        assert.equal((await fetch(url)).status, 404);
        assert.equal((await fetch(url, {method: "POST", body: "x".repeat(65537)})).status, 413);
        assert.equal(fs.readFileSync(first.filename, "utf8"), "printer connected\nupload failed\n");
    } finally {
        await stop(first.child);
    }
    const second = await start(directory);
    try {
        assert.notEqual(first.filename, second.filename);
        assert.equal(fs.readFileSync(second.filename, "utf8"), "");
    } finally {
        await stop(second.child);
    }
});
