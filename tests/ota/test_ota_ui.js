const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

if (process.argv[2] && process.argv[2] !== "OTAUI") process.exit(0);

function setup(cache) {
    const source = fs.readFileSync(path.join(__dirname, "../../PageHtmlJS_OTA.h"), "utf8");
    const script = source.match(/<script>([\s\S]*?)<\/script>/);
    assert.ok(script, "Missing embedded OTA script");
    const nodes = new Map(), uploads = [], requests = [], logs = [], timers = new Map();
    const outcomes = [];
    let now = 1700000000000, nextTimer = 1, submit;
    const get = id => {
        if (!nodes.has(id)) nodes.set(id, { disabled: false, innerHTML: "", style: {} });
        return nodes.get(id);
    };
    get("fichier").files = [{ name: "firmware.bin", size: 4096 }];
    const $ = selector => {
        const ids = selector.split(",").map(id => id.trim().replace(/^#/, ""));
        return {
            0: get(ids[0]),
            submit: callback => { submit = callback; },
            prop: (key, value) => ids.forEach(id => { get(id)[key] = value; }),
            html: value => ids.forEach(id => { get(id).innerHTML = value; })
        };
    };
    $.ajax = options => uploads.push(options);
    const location = { origin: "http://router.invalid", href: "" };
    const window = { location, XMLHttpRequest: class {
        constructor() {
            this.handlers = {};
            this.upload = { addEventListener: (event, handler) => { this.handlers[event] = handler; } };
        }
    } };
    if (cache) window.caches = cache;
    const timer = (callback, delay, interval) => {
        const id = nextTimer++;
        timers.set(id, { callback, at: now + delay, interval: interval ? delay : 0 });
        return id;
    };
    let initialized = 0, loaded = 0, colours = 0;
    const context = vm.createContext({
        $, window, caches: cache, AbortController,
        Date: { now: () => now },
        FormData: class {
            constructor(form) {
                assert.equal(form, get("upload_form"));
                assert.equal(get("fichier").disabled, false);
            }
        },
        F: { ESP32_Type: 0, VersionTexte: "17.26-test", VersionStocke: "17.26" },
        GID: get, GH: (id, value) => { get(id).innerHTML = value; },
        SetHautBas: () => initialized++, LoadParaFixe: () => loaded++, Set_Couleurs: () => colours++,
        console: Object.fromEntries(["log", "warn", "error"].map(level =>
            [level, (...args) => logs.push({ level, args })])),
        setInterval: (callback, delay) => timer(callback, delay, true),
        setTimeout: (callback, delay) => timer(callback, delay, false),
        clearInterval: id => timers.delete(id), clearTimeout: id => timers.delete(id),
        fetch: (url, options) => {
            requests.push({ url, options });
            const result = outcomes.shift();
            if (result instanceof Error || result === undefined)
                return Promise.reject(result || new Error("Offline"));
            if (result && result.promise) {
                if (options.signal && !result.ignoreAbort) {
                    options.signal.addEventListener("abort", () => result.reject(new Error("Aborted")));
                }
                return result.promise;
            }
            return Promise.resolve(result);
        }
    });
    vm.runInContext(script[1], context);
    const flush = async () => { for (let i = 0; i < 15; i++) await Promise.resolve(); };
    return { context, get, uploads, requests, outcomes, logs, timers, location,
        initialized: () => [initialized, loaded, colours],
        flush,
        submit: () => {
            let prevented = false;
            submit({ preventDefault: () => { prevented = true; } });
            assert.equal(prevented, true);
        },
        advance: async duration => {
            const target = now + duration;
            while (true) {
                const next = [...timers].filter(([, task]) => task.at <= target)
                    .sort((a, b) => a[1].at - b[1].at || a[0] - b[0])[0];
                if (!next) break;
                const [id, task] = next;
                now = task.at;
                if (task.interval) task.at += task.interval;
                else timers.delete(id);
                task.callback();
                await flush();
            }
            now = target;
            await flush();
        }
    };
}

function deferred(ignoreAbort = false) {
    const result = { ignoreAbort };
    result.promise = new Promise((resolve, reject) => Object.assign(result, { resolve, reject }));
    return result;
}

const cases = [
    ["InitialisationAndVersionFallback", async () => {
        const h = setup();
        h.context.Init();
        h.context.SetParaFixe();
        assert.deepEqual(h.initialized(), [1, 1, 1]);
        assert.equal(h.get("Version_actu").innerHTML, "17.26-test");
        assert.equal(h.get("Bwifi").style.display, "inline-block");
        h.context.F.VersionTexte = "";
        h.context.F.ESP32_Type = 10;
        h.context.SetParaFixe();
        assert.equal(h.get("Version_actu").innerHTML, "17.26");
        assert.equal(h.get("Bwifi").style.display, "none");
    }],
    ["MultipartUploadAndProgressIsNotValidation", async () => {
        const h = setup();
        h.submit();
        const upload = h.uploads[0];
        assert.equal(upload.url, "/update");
        assert.equal(upload.type, "POST");
        assert.equal(upload.contentType, false);
        assert.equal(upload.processData, false);
        assert.equal(h.get("fichier").disabled, true);
        assert.equal(h.get("submit_btn").disabled, true);
        const xhr = upload.xhr();
        xhr.handlers.progress({ lengthComputable: true, loaded: 1, total: 4 });
        assert.equal(h.get("prg").innerHTML, "Envoi: 25%");
        xhr.handlers.progress({ lengthComputable: true, loaded: 4, total: 4 });
        assert.ok(h.get("prg").innerHTML.includes("validation en cours"));
        xhr.handlers.progress({ lengthComputable: false });
        assert.ok(h.get("prg").innerHTML.includes("validation en cours"));
        assert.equal(h.requests.length, 0);
    }],
    ["ExplicitOKStartsPollingNotImmediateRedirect", async () => {
        const h = setup();
        h.submit();
        h.uploads[0].success(" OK\r\n");
        assert.ok(h.get("prg").innerHTML.includes("validée"));
        assert.equal(h.location.href, "");
        await h.advance(2000);
        assert.equal(h.requests.length, 1);
        assert.equal(h.requests[0].url, "http://router.invalid/");
        assert.equal(h.requests[0].options.cache, "no-store");
    }],
    ["FailureAndEmptyResponseAllowRetry", async () => {
        for (const response of ["FAIL", ""]) {
            const h = setup();
            h.submit();
            h.uploads[0].success(response);
            assert.equal(h.get("submit_btn").disabled, false);
            assert.ok(h.get("prg").innerHTML.includes("Échec"));
            assert.equal(h.timers.size, 0);
            assert.ok(h.logs.some(log => log.level === "error"));
            h.submit();
            assert.equal(h.uploads.length, 2);
        }
    }],
    ["HTTPErrorAllowsRetryButDisconnectStartsPolling", async () => {
        const h = setup();
        h.submit();
        h.uploads[0].error({ status: 500, statusText: "Error" });
        assert.equal(h.get("submit_btn").disabled, false);
        assert.ok(h.get("prg").innerHTML.includes("HTTP 500"));
        assert.equal(h.timers.size, 0);
        h.submit();
        h.uploads[1].error({ status: 0 });
        assert.ok(h.get("prg").innerHTML.includes("vérification"));
        assert.equal(h.get("submit_btn").disabled, true);
        await h.advance(2000);
        assert.equal(h.requests.length, 1);
    }],
    ["OfflineAndRejectedProbesDoNotReload", async () => {
        const h = setup();
        h.outcomes.push(new Error("Offline"), { ok: false }, { ok: true });
        h.context.startPolling();
        await h.advance(4000);
        assert.equal(h.location.href, "");
        await h.advance(2000);
        assert.match(h.location.href, /^http:\/\/router.invalid\/OTA\?v=\d+$/);
        assert.equal(h.timers.size, 0);
    }],
    ["CachesDeletedBeforeCacheBustedRedirect", async () => {
        const deletion = deferred();
        const deleted = [];
        const h = setup({
            keys: async () => ["old-assets", "other-assets"],
            delete: key => { deleted.push(key); return deletion.promise; }
        });
        h.context.clearCachesAndReload("http://router.invalid/OTA");
        await h.flush();
        assert.deepEqual(deleted, ["old-assets", "other-assets"]);
        assert.equal(h.location.href, "");
        deletion.resolve(true);
        await h.flush();
        assert.match(h.location.href, /^http:\/\/router.invalid\/OTA\?v=\d+$/);
    }],
    ["CacheErrorIsLoggedAndStillReloads", async () => {
        for (const failing of ["keys", "delete"]) {
            const h = setup({
                keys: async () => {
                    if (failing === "keys") throw new Error("Cache storage disabled");
                    return ["old"];
                },
                delete: async () => { throw new Error("Deletion rejected"); }
            });
            h.context.clearCachesAndReload("http://router.invalid/OTA");
            await h.flush();
            assert.ok(h.logs.some(log => log.level === "warn"));
            assert.match(h.location.href, /^http:\/\/router.invalid\/OTA\?v=\d+$/);
        }
    }],
    ["EmptyCacheAndUnavailableAPIBothReload", async () => {
        for (const cache of [undefined, { keys: async () => [] }]) {
            const h = setup(cache);
            h.context.clearCachesAndReload("http://router.invalid/OTA");
            await h.flush();
            assert.match(h.location.href, /^http:\/\/router.invalid\/OTA\?v=\d+$/);
        }
    }],
    ["NoFileAndEmptyFileDoNotSubmitFirmware", async () => {
        for (const files of [[], [{ name: "empty.bin", size: 0 }]]) {
            const h = setup();
            h.get("fichier").files = files;
            h.submit();
            assert.equal(h.uploads.length, 0);
            assert.equal(h.get("submit_btn").disabled, false);
            assert.ok(h.get("prg").innerHTML.includes("fichier"));
        }
    }],
    ["RepeatedSubmissionCannotStartConcurrentUploads", async () => {
        const h = setup();
        h.submit();
        h.submit();
        assert.equal(h.uploads.length, 1);
    }],
    ["SlowProbeCannotOverlapAndIsAborted", async () => {
        const h = setup(), pending = deferred();
        h.outcomes.push(pending);
        h.context.startPolling();
        await h.advance(4000);
        assert.equal(h.requests.length, 1);
        await h.advance(1000);
        assert.equal(h.requests[0].options.signal.aborted, true);
        await h.advance(1000);
        assert.equal(h.requests.length, 2);
    }],
    ["TimeoutIsExactly80SecondsAndAllowsRetry", async () => {
        const h = setup();
        h.submit();
        h.uploads[0].success("OK");
        await h.advance(79999);
        assert.ok(!h.get("prg").innerHTML.includes("Délai dépassé"));
        await h.advance(1);
        assert.ok(h.get("prg").innerHTML.includes("Délai dépassé"));
        assert.equal(h.timers.size, 0);
        assert.equal(h.get("submit_btn").disabled, false);
        h.submit();
        assert.equal(h.uploads.length, 2);
    }],
    ["LateResponseAfterTimeoutCannotRedirect", async () => {
        const h = setup(), pending = deferred(true);
        h.outcomes.push(pending);
        h.context.startPolling();
        await h.advance(80000);
        pending.resolve({ ok: true });
        await h.flush();
        assert.equal(h.location.href, "");
        assert.ok(h.get("prg").innerHTML.includes("Délai dépassé"));
    }]
];

(async () => {
    let failed = 0;
    for (const [name, run] of cases) {
        try {
            await run();
            console.log(`[OTAUI] ${name}: PASS`);
        } catch (error) {
            failed++;
            console.error(`[OTAUI] ${name}: FAIL\n${error.stack}`);
        }
    }
    console.log(`OTAUI: ${cases.length - failed}/${cases.length} tests passed`);
    process.exitCode = failed ? 1 : 0;
})();
