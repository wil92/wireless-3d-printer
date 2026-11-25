const assert = require("node:assert/strict");
const fs = require("node:fs");
const vm = require("node:vm");
const { Blob } = require("node:buffer");

function response(status, body = "") {
    return {
        ok: status >= 200 && status < 300,
        status,
        text: async () => body,
        json: async () => JSON.parse(body),
    };
}

function element(value = "") {
    return {
        value,
        textContent: "",
        disabled: false,
        dataset: {},
        children: [],
        classList: { toggle(name, enabled) { this[name] = enabled; } },
        replaceChildren(...children) { this.children = children; },
        appendChild(child) { this.children.push(child); },
        addEventListener(name, callback) { this[name] = callback; },
        querySelectorAll(selector) {
            return this.children.flatMap(child =>
                child.tag === selector ? [child] : child.querySelectorAll(selector));
        },
        removeAttribute(name) { delete this[name]; },
        focus() {},
    };
}

async function exercise(path, debug = false) {
    const html = fs.readFileSync(path, "utf8");
    assert.match(html, /<section[^>]*\bhidden\b[^>]*>/);
    assert.match(html, /id=["]?debug_address["]?/);
    assert.doesNotMatch(html, /\$\$DEBUG_CONFIG\$\$/);
    const script = html.match(/<script>([\s\S]*?)<\/script>/);
    assert.ok(script, `Missing script in ${path}`);
    const elements = {
        summit: element(),
        refresh: element(),
        networks: element(),
        scan_status: element(),
        ssid: element("my network"),
        password: element("p&ss"),
        name: element("wireless-3d-printer"),
        status: element(),
        debug_config: element(),
        debug_address: element("logs.local:9000"),
    };
    elements.debug_config.hidden = true;
    elements.debug_config.dataset.debugEnabled = debug ? "true" : "false";
    const requests = [];
    let startResult = response(202, "Scanning");
    let scanResults = [
        response(202, "Scanning"),
        response(200, JSON.stringify(['Home "Wi-Fi"', "<test>"])),
    ];
    let saveResult = response(204);
    const context = {
        Blob,
        document: {
            getElementById: id => elements[id],
            createElement: () => element(),
        },
        fetch: async (url, options) => {
            requests.push({ url, options });
            if (url === "/scan") return startResult;
            if (url === "/refresh-ssid") return scanResults.shift() || response(202);
            return saveResult;
        },
        setTimeout: resolve => resolve(),
        encodeURIComponent,
    };
    vm.runInNewContext(script[1], context);
    assert.equal(elements.debug_config.hidden, !debug);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(requests.filter(request => request.url === "/scan").length, 1);
    assert.equal(requests.filter(request => request.url === "/refresh-ssid").length, 2);
    assert.deepEqual(elements.networks.children.map(child => child.value), ['Home "Wi-Fi"', "<test>"]);
    assert.match(elements.scan_status.textContent, /Choose/);
    assert.equal(elements.refresh.disabled, false);

    scanResults = [response(200, "[]")];
    await elements.refresh.onclick();
    assert.equal(elements.networks.children.length, 0);
    assert.match(elements.scan_status.textContent, /manually/);

    startResult = response(500, "Radio error");
    await elements.refresh.onclick();
    assert.match(elements.scan_status.textContent, /Radio error/);
    assert.equal(elements.scan_status.classList.error, true);
    assert.equal(elements.refresh.disabled, false);

    startResult = response(202);
    scanResults = [response(500, "Scan failed")];
    await elements.refresh.onclick();
    assert.match(elements.scan_status.textContent, /Scan failed/);
    scanResults = [];
    await elements.refresh.onclick();
    assert.match(elements.scan_status.textContent, /timed out/);
    assert.equal(elements.refresh.disabled, false);

    const savesBefore = () => requests.filter(request => request.url.startsWith("/?")).length;
    for (const invalid of ["", "é".repeat(33), "bad\nname"]) {
        elements.name.value = invalid;
        await elements.summit.onclick();
        assert.equal(savesBefore(), 0);
        assert.match(elements.status.textContent, /Device name/);
    }
    elements.name.value = "Printer";
    elements.ssid.value = "";
    await elements.summit.onclick();
    assert.match(elements.status.textContent, /SSID/);
    assert.equal(savesBefore(), 0);

    elements.ssid.value = "my network";
    saveResult = response(400, "Invalid SSID");
    await elements.summit.onclick();
    assert.match(elements.status.textContent, /Invalid SSID/);
    assert.equal(elements.summit.disabled, false);
    saveResult = response(500);
    await elements.summit.onclick();
    assert.match(elements.status.textContent, /HTTP 500/);
    assert.equal(elements.summit.disabled, false);

    saveResult = response(204);
    elements.name.value = 'Printer "A" & é';
    await elements.summit.onclick();
    const save = requests.filter(request => request.url.startsWith("/?")).pop();
    const query = new URLSearchParams(save.url.slice(2));
    assert.equal(query.get("name"), 'Printer "A" & é');
    assert.equal(query.get("ssid"), "my network");
    assert.equal(query.get("password"), "p&ss");
    assert.equal(query.get("debug_address"), debug ? "logs.local:9000" : null);
    assert.equal(save.options.method, "POST");
    assert.match(elements.status.textContent, /Device configured/);
    assert.equal(elements.refresh.disabled, true);
    if (debug) {
        saveResult = response(400, "Invalid debug receiver");
        await elements.summit.onclick();
        assert.match(elements.status.textContent, /Invalid debug receiver/);
        assert.equal(elements.summit.disabled, false);
        saveResult = response(204);
        elements.debug_address.value = "";
        await elements.summit.onclick();
        const request = requests[requests.length - 1];
        assert.equal(new URLSearchParams(request.url.slice(2)).get("debug_address"), "");
        assert.match(elements.status.textContent, /restarting/);
        assert.equal(elements.summit.disabled, true);
    }
}

async function exerciseConfiguredPage(path) {
    const html = fs.readFileSync(path, "utf8");
    const script = html.match(/<script>([\s\S]*?)<\/script>/);
    const elements = {};
    for (const id of ["ssid", "password", "name", "networks", "scan_status", "status", "refresh", "summit", "debug_config", "debug_address"]) {
        elements[id] = element();
    }
    elements.refresh.dataset.scanAvailable = "false";
    let requests = 0;
    vm.runInNewContext(script[1], {
        Blob,
        document: {getElementById: id => elements[id]},
        fetch: async () => { requests++; return response(200); },
        encodeURIComponent,
    });
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(requests, 0);
    assert.equal(elements.refresh.disabled, true);
    assert.match(elements.scan_status.textContent, /only available during setup/);
}

async function exercisePrinter(path) {
    const html = fs.readFileSync(path, "utf8");
    const script = html.match(/<script>([\s\S]*?)<\/script>/);
    assert.ok(script, `Missing printer script in ${path}`);
    const elements = {};
    for (const id of ["message", "files", "refresh", "upload", "gcode", "send"]) {
        elements[id] = element();
        elements[id].addEventListener = (name, callback) => {
            elements[id][name] = callback;
        };
    }
    const requests = [];
    let listing = response(200, JSON.stringify([{name: "<unsafe>.GCO", size: 12, path: "FILE~1.GCO"}]));
    let reply = response(200);
    let confirmed = true;
    const context = {
        document: {
            getElementById: id => elements[id],
            createElement: tag => Object.assign(element(), {tag}),
        },
        fetch: async (url, options) => {
            requests.push({url, options});
            return url === "/api/files" ? listing : reply;
        },
        confirm: () => confirmed,
        encodeURIComponent,
    };
    vm.runInNewContext(script[1], context);
    await new Promise(resolve => setImmediate(resolve));
    assert.equal(requests[0].url, "/api/files");
    assert.equal(elements.files.children[0].children[0].textContent, "<unsafe>.GCO (12 bytes)");
    assert.equal(elements.refresh.disabled, false);
    const file = {name: "my print.gcode"};
    elements.gcode.files = [file];
    confirmed = false;
    await elements.upload.submit({preventDefault() {}});
    assert.equal(requests.length, 1);
    confirmed = true;
    reply = response(200, "Saved as MY_PRINT.GCO");
    await elements.upload.submit({preventDefault() {}});
    assert.equal(requests[1].url, "/upload?name=my%20print.gcode");
    assert.equal(requests[1].options.body, file);
    assert.equal(elements.message.textContent, "Saved as MY_PRINT.GCO");
    listing = response(200, JSON.stringify([{name: "Print", size: 12, path: "FILE~1.GCO"}]));
    await elements.refresh.click();
    reply = response(409, "Printer busy");
    await elements.files.children[0].children[1].click();
    assert.equal(requests[4].url, "/api/files?path=FILE~1.GCO");
    assert.equal(requests[4].options.method, "DELETE");
    assert.match(elements.message.textContent, /Printer busy/);
    confirmed = false;
    await elements.files.children[0].children[1].click();
    assert.equal(requests.length, 5);
    confirmed = true;
    reply = response(200, "Printer file deleted.");
    listing = response(200, "[]");
    await elements.files.children[0].children[1].click();
    assert.equal(elements.files.children.length, 0);
    assert.equal(elements.message.textContent, "Printer file deleted.");
    reply = response(409, "Printer busy");
    await elements.upload.submit({preventDefault() {}});
    assert.match(elements.message.textContent, /Printer busy/);
    assert.equal(elements.send.disabled, false);
}

(async () => {
    await exercise("frontend/server-config.index.html");
    await exercise("lib/webserver/views/server-config.index.h");
    await exercise("frontend/server-config.index.html", true);
    await exercise("lib/webserver/views/server-config.index.h", true);
    await exerciseConfiguredPage("frontend/server-config.index.html");
    await exerciseConfiguredPage("lib/webserver/views/server-config.index.h");
    await exercisePrinter("frontend/status.index.html");
    await exercisePrinter("lib/webserver/views/status.index.h");
    const setup = fs.readFileSync("lib/webserver/views/server-config.index.h", "utf8");
    const status = fs.readFileSync("lib/webserver/views/status.index.h", "utf8");
    assert.match(setup, /<title>wireless-3d-printer configuration<\/title>/);
    assert.match(status, /<title>wireless-3d-printer<\/title>/);
    assert.match(status, /<h1>wireless-3d-printer<\/h1>/);
    assert.equal(setup.match(/<style>([\s\S]*?)<\/style>/)[1],
                 status.match(/<style>([\s\S]*?)<\/style>/)[1]);
    console.log("Frontend tests passed");
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
