const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

if (require.main === module && process.argv[2] && process.argv[2] !== "SequenceurUI") process.exit(0);

const header = fs.readFileSync(path.join(__dirname, "../../JS_Actions.h"), "utf8");
const chunks = [...header.matchAll(/R"====\(([\s\S]*?)\)===="/g)];
assert.equal(chunks.length, 4, "Expected the four firmware ActionsJS chunks");
const script = chunks.map(match => match[1]).join("\n");

class Element {
    constructor() {
        this.style = {};
        this.checked = false;
        this.disabled = false;
        this._value = "";
        this._html = "";
        this.options = null;
    }
    get value() { return this._value; }
    set value(value) {
        value = String(value);
        this._value = this.options && !this.options.includes(value) ? "" : value;
    }
    get innerHTML() { return this._html; }
    set innerHTML(html) {
        this._html = html;
        if (html.includes("<option")) {
            this.options = [...html.matchAll(/value='([^']+)'/g)].map(match => match[1]);
            this.value = this.options[0];
        }
    }
}

function setup(modes = [1, 6, 5]) {
    const elements = new Map();
    const get = id => {
        if (!elements.has(id)) elements.set(id, new Element());
        return elements.get(id);
    };
    let posted;
    const context = vm.createContext({
        console, GID: get,
        document: { querySelector: () => ({ value: "1000" }) },
        location: { reload() {} },
        fetch(url, options) {
            assert.equal(url, "/ParaNew");
            posted = JSON.parse(options.body);
            return Promise.resolve({ ok: true, json: () => Promise.resolve({}) });
        }
    });
    vm.runInContext(script, context, { filename: "JS_Actions.h" });
    context.TracePeriodes = () => {};
    context.F = { ModePara: 1, ReacCACSI: 1, pTriac: 1, Actions: [] };
    modes.forEach((mode, i) => {
        const action = context.CreerAction(i, `Action ${i}`);
        action.Actif = mode;
        context.F.Actions.push(action);
        get(`radio${i}-${mode}`).checked = true;
        get(`titre${i}`).innerHTML = action.Titre;
        get(`selectPin${i}`).value = mode === 6 ? 0 : 10 + i;
        get(`selectOut${i}`).value = 1;
        get(`selectSequenceur${i}`).value = -1;
        get(`ForceOuvre${i}`).value = 100;
        get(`puissanceCharge${i}`).value = 1000;
    });
    return { context, get, posted: () => posted };
}

const cases = [
    ["ValeursParDefautCompatibles", () => {
        const { context } = setup();
        const action = context.CreerAction(2, "Relay");
        assert.equal(action.IdxSequenceur, -1);
        assert.equal(action.PuissanceCharge, 0);
    }],
    ["PlanificationParentEtMasquageGPIO", () => {
        const { context, get } = setup();
        get("selectSequenceur2").value = 1;
        context.checkDisabled();
        assert.equal(get("SelectPin1").style.display, "none");
        assert.equal(get("SelectOut1").style.display, "none");
        assert.equal(get("planningPeriodes2").style.display, "none");
        assert.equal(get("planningControles2").style.display, "none");
        assert.equal(get("visu2").style.display, "none");
        assert.equal(get("Sequenceur0").style.display, "none");
        assert.equal(context.F.Actions[2].IdxSequenceur, 1);
    }],
    ["ParentDisparuRestaurePlanning", () => {
        const { context, get } = setup();
        get("selectSequenceur2").value = 1;
        context.checkDisabled();
        get("radio1-6").checked = false;
        get("radio1-5").checked = true;
        context.checkDisabled();
        assert.equal(get("selectSequenceur2").value, "-1");
        assert.equal(get("planningPeriodes2").style.display, "block");
        assert.equal(context.F.Actions[2].IdxSequenceur, -1);
    }],
    ["RelaisInactifEtSequenceurNePeuventPasEtreEnfants", () => {
        const { context, get } = setup();
        get("selectSequenceur1").value = 1;
        get("selectSequenceur2").value = 1;
        get("radio2-5").checked = false;
        get("radio2-0").checked = true;
        context.checkDisabled();
        assert.equal(get("selectSequenceur1").value, "-1");
        assert.equal(get("selectSequenceur2").value, "-1");
    }],
    ["ParentDisparuAvecAutreGroupeRestaurePlanning", () => {
        const { context, get } = setup([1, 6, 5, 6]);
        get("selectSequenceur2").value = 1;
        context.checkDisabled();
        get("radio1-6").checked = false;
        get("radio1-5").checked = true;
        context.checkDisabled();
        assert.equal(get("selectSequenceur2").value, "-1");
        assert.equal(get("planningPeriodes2").style.display, "block");
        assert.equal(context.F.Actions[2].IdxSequenceur, -1);
    }],
    ["SauvegardeSequenceurSansGPIO", () => {
        const { context, get, posted } = setup();
        get("selectSequenceur2").value = 1;
        get("puissanceCharge2").value = 2500;
        context.Send_Values();
        assert.equal(posted().NbActions, 3);
        assert.equal(posted().Actions[1].Actif, 6);
        assert.equal(posted().Actions[2].IdxSequenceur, 1);
        assert.equal(posted().Actions[2].PuissanceCharge, 2500);
    }],
    ["SuppressionActionAvantParentReindexeLeLien", () => {
        const { context, get, posted } = setup([1, 5, 6, 5]);
        get("titre1").innerHTML = "";
        get("selectSequenceur3").value = 2;
        context.Send_Values();
        assert.equal(posted().NbActions, 3);
        assert.equal(posted().Actions[1].Actif, 6);
        assert.equal(posted().Actions[2].IdxSequenceur, 1);
        assert.equal(posted().Actions[2].Action, 2);
    }],
    ["SuppressionParentNeReaffectePasSesEnfants", () => {
        const { context, get, posted } = setup([1, 6, 5, 6, 5]);
        get("titre1").innerHTML = "";
        get("selectSequenceur2").value = 1;
        get("selectSequenceur4").value = 3;
        context.Send_Values();
        assert.equal(posted().Actions[1].IdxSequenceur, -1);
        assert.equal(posted().Actions[2].Actif, 6);
        assert.equal(posted().Actions[3].IdxSequenceur, 2);
    }]
];

module.exports = { setup };

if (require.main === module) {
    let failed = 0;
    for (const [name, run] of cases) {
        try {
            run();
            console.log(`[SequenceurUI] ${name}: PASS`);
        } catch (error) {
            failed++;
            console.error(`[SequenceurUI] ${name}: FAIL\n${error.stack}`);
        }
    }
    console.log(`SequenceurUI: ${cases.length - failed}/${cases.length} tests passed`);
    process.exitCode = failed ? 1 : 0;
}
