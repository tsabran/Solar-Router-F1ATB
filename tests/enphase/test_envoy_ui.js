const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

if (process.argv[2] && process.argv[2] !== "EnvoyUI") process.exit(0);

function setup() {
    const source = fs.readFileSync(path.join(__dirname, "../../JS_Brute.h"), "utf8");
    const nodes = new Map(), errors = [], timers = [];
    const get = id => {
        if (!nodes.has(id)) nodes.set(id, { innerHTML: "", style: {} });
        return nodes.get(id);
    };
    let payload = "", fail = false;
    const context = vm.createContext({
        GID: get, GH: (id, value) => { get(id).innerHTML = value; },
        F: { nomSondeMobile: "Maison" }, GS: "|", RS: ";",
        LaVal: value => `energy:${value}`,
        console: { log() {}, error: (...args) => errors.push(args) },
        setTimeout: (_, delay) => timers.push(delay),
        fetch: async url => {
            assert.equal(url, "/ajax_dataRMS?idx=0");
            return { ok: !fail, status: fail ? 503 : 200,
                statusText: fail ? "Offline" : "OK", text: async () => payload };
        }
    });
    for (const name of ["PageBruteJS1", "PageBruteJS2"]) {
        const match = source.match(new RegExp(`const char \\*${name} = R"====\\(([\\s\\S]*?)\\)====";`));
        assert.ok(match, `Missing ${name}`);
        vm.runInContext(match[1], context);
    }
    const columns = JSON.parse(vm.runInContext("JSON.stringify(E)", context));
    return { context, columns, get, errors, timers,
        response: (body, offline = false) => { payload = body; fail = offline; } };
}

const cases = [
    ["StatisticsSchemaAndRenderedTable", async () => {
        const { context, columns, get } = setup();
        assert.equal(columns.length, 27);
        assert.equal(new Set(columns.map(row => row[0])).size, 27);
        assert.equal(columns[8][0], "Token_Enphase");
        assert.equal(columns[9][0], "EnvoySessionCookie");
        assert.equal(columns[26][0], "EnvoyRatioReconnexion");
        vm.runInContext("creerTableauEnphase()", context);
        for (const [id] of columns) assert.ok(get("tableauEnphase").innerHTML.includes(`id="${id}"`));
    }],
    ["AjaxColumnsMapToActualUIAndScheduleRefresh", async () => {
        const { context, columns, get, response, timers, errors } = setup();
        response("2026-10-12;Enphase|" + columns.map((_, i) => `value${i}`).join(";"));
        await context.LoadData();
        columns.forEach(([id, , , format], i) => {
            assert.equal(get(id).innerHTML, `${format === "Wh" ? "energy:" : ""}value${i}`);
        });
        assert.equal(get("LED").style.display, "none");
        assert.equal(get("infoEnphase").style.display, "block");
        assert.deepEqual(timers, [2000]);
        assert.equal(errors.length, 0);
    }],
    ["HTTPFailureIsVisibleAndPreservesDataUntilNextRefresh", async () => {
        const { context, get, response, timers, errors } = setup();
        get("PuissanceS_M").innerHTML = "previous";
        response("", true);
        await context.LoadData();
        assert.equal(get("PuissanceS_M").innerHTML, "previous");
        assert.equal(get("LED").style.display, "none");
        assert.equal(errors.length, 1);
        assert.deepEqual(timers, [2000]);
    }]
];

(async () => {
    let failed = 0;
    for (const [name, run] of cases) {
        try {
            await run();
            console.log(`[EnvoyUI] ${name}: PASS`);
        } catch (error) {
            failed++;
            console.error(`[EnvoyUI] ${name}: FAIL\n${error.stack}`);
        }
    }
    console.log(`EnvoyUI: ${cases.length - failed}/${cases.length} tests passed`);
    process.exitCode = failed ? 1 : 0;
})();
