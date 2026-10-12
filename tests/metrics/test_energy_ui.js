const assert = require("node:assert/strict");
const { setup } = require("../sequenceur/test_sequenceur_ui.js");

if (process.argv[2] && process.argv[2] !== "MetricsUI") process.exit(0);

const cases = [
    ["MonitoringLabelExplainsEstimatesAndRequirements", () => {
        const header = require("node:fs").readFileSync(
            require("node:path").join(__dirname, "../../JS_Actions.h"), "utf8");
        const option = header.split("\n").find(line => line.includes("id='suiviEnergie"));
        assert.ok(option);
        assert.ok(option.includes("Estimer les pertes d&apos;&eacute;nergie potentiellement &eacute;vitables"));
        const tooltip = option.match(/<label title='([^']+)'/);
        assert.ok(tooltip, "Monitoring option must have an explanatory tooltip");
        for (const detail of ["soutirage", "injection", "hors for&ccedil;age", "Bornes",
            "pas une mesure de pertes", "Envoy", "demi-sinus", "puissances nominales", "Thermostat non mesur"])
            assert.ok(tooltip[1].includes(detail), detail);
    }],
    ["MonitoringDisabledByDefaultAndLegacyConfig", () => {
        const { context, posted } = setup();
        assert.equal(context.CreerAction(1, "Group").SuiviEnergie, false);
        delete context.F.Actions[1].SuiviEnergie;
        context.Send_Values();
        assert.equal(posted().Actions[1].SuiviEnergie, false);
    }],
    ["ExplicitOptInSavedOnlyForSequencer", () => {
        const { context, get, posted } = setup();
        for (let i = 0; i < 3; i++) get(`suiviEnergie${i}`).checked = true;
        context.Send_Values();
        assert.equal(posted().Actions[0].SuiviEnergie, false);
        assert.equal(posted().Actions[1].SuiviEnergie, true);
        assert.equal(posted().Actions[2].SuiviEnergie, false);
        assert.equal(typeof posted().Actions[1].SuiviEnergie, "boolean");
    }],
    ["LeavingSequencerModeDisablesMonitoring", () => {
        const { context, get, posted } = setup();
        get("suiviEnergie1").checked = true;
        get("radio1-6").checked = false;
        get("radio1-5").checked = true;
        get("selectPin1").value = 11;
        context.Send_Values();
        assert.equal(posted().Actions[1].Actif, 5);
        assert.equal(posted().Actions[1].SuiviEnergie, false);
    }],
    ["OptInAndNominalPowersSurviveReindexing", () => {
        const { context, get, posted } = setup([1, 5, 6, 5]);
        get("titre1").innerHTML = "";
        get("suiviEnergie2").checked = true;
        get("selectSequenceur3").value = 2;
        get("puissanceCharge3").value = 2300;
        context.Send_Values();
        assert.equal(posted().Actions[1].SuiviEnergie, true);
        assert.equal(posted().Actions[2].IdxSequenceur, 1);
        assert.equal(posted().Actions[2].PuissanceCharge, 2300);
    }]
];

let failed = 0;
for (const [name, run] of cases) {
    try {
        run();
        console.log(`[MetricsUI] ${name}: PASS`);
    } catch (error) {
        failed++;
        console.error(`[MetricsUI] ${name}: FAIL\n${error.stack}`);
    }
}
console.log(`MetricsUI: ${cases.length - failed}/${cases.length} tests passed`);
process.exitCode = failed ? 1 : 0;
