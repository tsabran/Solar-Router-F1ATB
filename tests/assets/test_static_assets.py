import importlib.util
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("assets", ROOT / "tools/prepare-assets.py")
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class StaticAssets(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.stage = Path(self.temp.name)
        for path in list(ROOT.glob("Page*.h")) + list(ROOT.glob("JS_*.h")):
            shutil.copy(path, self.stage)
        shutil.copy(ROOT / "StaticAssetCache.h", self.stage)

    def test_minification_reduces_actual_resources(self):
        report = assets.prepare(self.stage, gzip_enabled=False)
        for name in ("MainJS1", "MainJS2", "MainJS3", "CommunCouleurJS", "CommunCSS"):
            row = next(r for r in report["resources"] if r["name"] == name)
            limit = 1 if name == "CommunCSS" else 0.85
            self.assertLess(row["prepared"], row["original"] * limit, name)
            self.assertLess(row["gzip"], row["prepared"], name)

    def test_disabled_preserves_bodies_except_url_versions(self):
        report = assets.prepare(self.stage, False, False)
        for row in report["resources"]:
            self.assertEqual(row["original"], row["prepared"])
        for path in self.stage.glob("*.h"):
            if path.name in ("StaticAssetCache.h", "GeneratedStaticAssets.h"):
                continue
            prepared = re.sub(r"\?v=[0-9a-f]{20}", "", path.read_text())
            self.assertEqual((ROOT / path.name).read_text(), prepared)

    def test_deterministic_tag_and_changes_invalidate(self):
        first = assets.prepare(self.stage, gzip_enabled=False)["tag"]
        with tempfile.TemporaryDirectory() as other:
            stage = Path(other)
            for path in ROOT.glob("*.h"):
                shutil.copy(path, stage)
            self.assertEqual(first, assets.prepare(stage, gzip_enabled=False)["tag"])
        with tempfile.TemporaryDirectory() as other:
            stage = Path(other)
            for path in ROOT.glob("*.h"):
                shutil.copy(path, stage)
            path = stage / "JS_Accueil.h"
            path.write_text(path.read_text().replace("function", "function changed", 1))
            self.assertNotEqual(first, assets.prepare(stage, gzip_enabled=False)["tag"])

    def test_versions_all_local_static_references_and_not_dynamic_pins(self):
        report = assets.prepare(self.stage, gzip_enabled=False)
        tag = report["tag"]
        for path in self.stage.glob("*.h"):
            self.assertIsNone(assets.URL.search(path.read_text()), path.name)
        accueil = (self.stage / "PageAccueil.h").read_text()
        for name in ("MainJS1", "MainJS2", "MainJS3", "ParaCommunJS",
                     "CommunCouleurJS", "commun.css"):
            self.assertIn(f"{name}?v={tag}", accueil)
        self.assertIn('src="/PinsActionsJS"', (self.stage / "PageActions.h").read_text())
        self.assertIn(f'#define STATIC_ASSET_TAG "{tag}"',
                      (self.stage / "StaticAssetCache.h").read_text())
        self.assertIn(f"/favicon.ico?v={tag}", (self.stage / "JS_Accueil.h").read_text())

    def test_html_markup_preserved_outside_inline_code(self):
        assets.prepare(self.stage, gzip_enabled=False)
        strip_code = lambda text: re.sub(
            r"(<(?:script|style)\b[^>]*>).*?(</(?:script|style)\s*>)",
            r"\1\2", text, flags=re.S | re.I,
        )
        for path in self.stage.glob("Page*.h"):
            prepared = re.sub(r"\?v=[0-9a-f]{20}", "", path.read_text())
            # Also remove non-HTML raw strings, e.g. CSS and JS in PageCommun.
            for old, new in zip(assets.RAW.finditer((ROOT / path.name).read_text()),
                                assets.RAW.finditer(prepared)):
                if "<html" in old["body"].lower():
                    self.assertEqual(strip_code(old["body"]), strip_code(new["body"]))

    def test_runtime_dependencies_are_not_cached_and_keep_order(self):
        assets.prepare(self.stage, gzip_enabled=False)
        for path in self.stage.glob("Page*.h"):
            for match in assets.RAW.finditer(path.read_text()):
                body = match["body"]
                if "<html" in body.lower():
                    self.assertLess(body.index('href="/runtime.css"'),
                                    body.index("commun.css?v="), path.name)
                    self.assertNotIn("runtime.css?v=", body)
                    self.assertNotIn("RuntimeJS?v=", body)
        accueil = (self.stage / "PageAccueil.h").read_text()
        self.assertLess(accueil.index('src="/RuntimeJS"'), accueil.index('src="MainJS1'))

    def test_bundle_estimates_match_payloads_and_merged_scripts_parse(self):
        report = assets.prepare(self.stage, gzip_enabled=False)
        bodies = {m[1]: m["body"] for path in self.stage.glob("*.h")
                  for m in assets.RAW.finditer(path.read_text())}
        self.assertEqual(len(assets.BUNDLE_CANDIDATES), len(report["bundle_estimates"]))
        for bundle in report["bundle_estimates"]:
            merged = ";\n".join(bodies[member] for member in bundle["members"])
            self.assertEqual(len(merged.encode()), bundle["merged"])
            self.assertEqual(len(bundle["members"]) - 1, bundle["requests_saved"])
            self.assertLess(bundle["gzip_merged"], bundle["gzip_separate"])
            subprocess.run(["node", "-e",
                            'new (require("vm").Script)(require("fs").readFileSync(0,"utf8"))'],
                           input=merged, text=True, check=True)

    def test_all_javascript_parses_and_existing_ui_tests_pass_after_minification(self):
        assets.prepare(self.stage, gzip_enabled=False)
        blocks = []
        for path in self.stage.glob("*.h"):
            for match in assets.RAW.finditer(path.read_text()):
                if re.search(r"JS\d*$", match[1]):
                    blocks.append([match[1], match["body"]])
                elif "<html" in match["body"].lower():
                    blocks.extend(
                        [match[1], body] for body in re.findall(
                            r"<script\b[^>]*>(.*?)</script\s*>",
                            match["body"], re.S | re.I,
                        )
                    )
        subprocess.run(["node", "-e", """
            const vm = require('node:vm');
            const blocks = JSON.parse(require('node:fs').readFileSync(0, 'utf8'));
            for (const [name, body] of blocks) new vm.Script(body, {filename: name});
        """], input=json.dumps(blocks), text=True, check=True)
        shutil.copytree(ROOT / "tests", self.stage / "tests",
                        ignore=shutil.ignore_patterns(".build", "__pycache__"))
        for path in sorted((self.stage / "tests").rglob("test_*.js")):
            subprocess.run(["node", str(path)], check=True, capture_output=True, text=True)

    def test_minifier_preserves_asi_regex_templates_and_css_strings(self):
        for path in self.stage.glob("*.h"):
            if path.name != "StaticAssetCache.h":
                path.unlink()
        js = r'''
function newline() { return
{value: 42}; }
const regex = /https?:\/\/[^/]+/;
const template = `line one
    line two ${1 + 2}`;
const quote = "/* not a comment */ // still text";
globalThis.result = JSON.stringify([newline(), regex.test("https://router/"), template, quote]);
'''
        css = 'a::after { content: "a  b /* not a comment */"; width: calc(100% - 2px); }'
        fixture = self.stage / "JS_Fixture.h"
        fixture.write_text(f'const char * FixtureJS = R"===({js})===";\n'
                           f'const char * CommunCSS = R"===({css})===";')
        assets.prepare(self.stage, gzip_enabled=False)
        matches = list(assets.RAW.finditer(fixture.read_text()))
        run = lambda body: subprocess.check_output(
            ["node", "-e", body + "\nconsole.log(result)"], text=True,
        )
        self.assertEqual(run(js), run(matches[0]["body"]))
        self.assertIn('"a  b /* not a comment */"', matches[1]["body"])
        self.assertIn("100% - 2px", matches[1]["body"])

    def test_default_gzip_roundtrips_versioned_bodies_without_raw_duplicates(self):
        with tempfile.TemporaryDirectory() as other:
            plain = Path(other)
            for path in ROOT.glob("*.h"):
                shutil.copy(path, plain)
            plain_report = assets.prepare(plain, gzip_enabled=False)
            original_bodies = {m[1]: m["body"] for path in plain.glob("*.h")
                               for m in assets.RAW.finditer(path.read_text())}
        report = assets.prepare(self.stage)
        self.assertTrue(report["gzip_enabled"])
        self.assertNotEqual(plain_report["tag"], report["tag"])
        packed = {}
        for path in self.stage.glob("*.h"):
            for name, literals in re.findall(
                r"const char (\w+)_Gzip\[\] PROGMEM =\s*(.*?);", path.read_text(), re.S,
            ):
                data = bytes(int(byte, 16) for byte in re.findall(r"\\x([0-9a-f]{2})", literals))
                packed[name] = data
                decoded = gzip.decompress(data).decode()
                expected = original_bodies[name].replace(plain_report["tag"], report["tag"])
                self.assertEqual(expected, decoded, name)
                self.assertIn(b"\0", data, name)
            remaining = {m[1] for m in assets.RAW.finditer(path.read_text())}
            self.assertFalse(remaining & assets.SERVED_ASSETS, path.name)
        self.assertEqual(assets.SERVED_ASSETS, set(packed))
        for row in report["resources"]:
            if row["name"] in assets.SERVED_ASSETS:
                self.assertEqual("gzip", row["encoding"])
                self.assertEqual(len(packed[row["name"]]), row["stored"])
            else:
                self.assertEqual("identity", row["encoding"])
        self.assertLess(sum(row["stored"] for row in report["resources"]),
                        sum(row["stored"] for row in plain_report["resources"]) * 0.4)

        includes = "\n".join(f'#include "{path.name}"' for path in sorted(self.stage.glob("*.h"))
                             if path.name not in ("StaticAssetCache.h", "GeneratedStaticAssets.h"))
        checks = "\n".join(
            f'assert(GetStaticAssetPayload({row["name"]}, payload));'
            f'assert(payload.length == {row["stored"]});'
            f'assert(payload.gzip == {str(row["encoding"] == "gzip").lower()});'
            for row in report["resources"] if row["name"] in assets.SERVED_ASSETS
        )
        source = f'''
#include <cassert>
#define PROGMEM
{includes}
#include "StaticAssetCache.h"
int main() {{
    StaticAssetPayload payload;
    {checks}
    assert(!GetStaticAssetPayload("unknown", payload));
    assert(!GetStaticAssetPayload(nullptr, payload));
    assert(!GetStaticAssetPayload(CouleurDefaut, payload));
}}
'''
        compiler = os.environ.get("CXX", "clang++")
        if not shutil.which(compiler):
            compiler = "g++"
        executable = self.stage / "metadata-test"
        subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-I", str(self.stage),
                        "-x", "c++", "-", "-o", str(executable)],
                       input=source, text=True, check=True, capture_output=True)
        subprocess.run([str(executable)], check=True)

    def test_gzip_can_be_disabled_independently_of_minification_via_cli(self):
        subprocess.run([sys.executable, str(ROOT / "tools/prepare-assets.py"),
                        "--stage", str(self.stage), "--report", str(self.stage / "assets.json"),
                        "--no-minify", "--no-gzip"], check=True, capture_output=True, text=True)
        report = json.loads((self.stage / "assets.json").read_text())
        self.assertFalse(report["gzip_enabled"])
        self.assertFalse(report["minify"])
        for row in report["resources"]:
            self.assertEqual("identity", row["encoding"])
            self.assertEqual(row["versioned"], row["stored"])
            self.assertEqual(row["original"], row["prepared"])

    def test_default_cli_compresses_even_without_minification(self):
        subprocess.run([sys.executable, str(ROOT / "tools/prepare-assets.py"),
                        "--stage", str(self.stage), "--report", str(self.stage / "assets.json"),
                        "--no-minify"], check=True, capture_output=True, text=True)
        report = json.loads((self.stage / "assets.json").read_text())
        self.assertTrue(report["gzip_enabled"])
        self.assertFalse(report["minify"])
        for row in report["resources"]:
            self.assertEqual(row["original"], row["prepared"])
            if row["name"] in assets.SERVED_ASSETS:
                self.assertEqual("gzip", row["encoding"])

    def test_no_resources_is_an_error(self):
        with tempfile.TemporaryDirectory() as other:
            with self.assertRaisesRegex(ValueError, "No embedded resources"):
                assets.prepare(Path(other), False)


if __name__ == "__main__":
    unittest.main(verbosity=2)
