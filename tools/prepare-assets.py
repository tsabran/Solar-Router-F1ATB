#!/usr/bin/env python3
"""Transform embedded web resources in an expendable build staging directory only."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re

RAW = re.compile(
    r'\bconst\s+char\s*\*\s*(\w+)\s*(?:PROGMEM\s*)?=\s*'
    r'R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\((?P<body>.*?)\)(?P=delimiter)"',
    re.S,
)
ASSET_PATHS = (
    "MainJS1", "MainJS2", "MainJS3", "BruteJS1", "BruteJS2",
    "ActionsJS1", "ActionsJS2", "ActionsJS3", "ActionsJS4",
    "ParaJS1", "ParaJS2", "ParaCommunJS", "CommunCouleurJS",
    "commun.css", "favicon.ico", "favicon192.ico", "manifest.json",
)
URL = re.compile(
    r"""(?P<quote>["'])(?P<path>/?(?:"""
    + "|".join(re.escape(path) for path in ASSET_PATHS)
    + r"""))(?P=quote)"""
)
BUNDLE_CANDIDATES = {
    "main": ("MainJS1", "MainJS2", "MainJS3"),
    "home-all-js": ("ParaCommunJS", "MainJS1", "MainJS2", "MainJS3", "CommunCouleurJS"),
    "shared-js": ("ParaCommunJS", "CommunCouleurJS"),
    "actions": ("ActionsJS1", "ActionsJS2", "ActionsJS3", "ActionsJS4"),
    "parameters": ("ParaJS1", "ParaJS2"),
    "raw-data": ("PageBruteJS1", "PageBruteJS2"),
}
SERVED_ASSETS = {
    "MainHtml", "MainJS1", "MainJS2", "MainJS3", "PageBrute", "PageBruteJS1",
    "PageBruteJS2", "ActionsHtml", "ActionsJS1", "ActionsJS2", "ActionsJS3",
    "ActionsJS4", "ParaHtml", "ParaJS1", "ParaJS2", "ParaCommunJS", "OtaHtml",
    "HeureHtml", "CouleursHtml", "ExportHtml", "ConnectAP_Html", "CommunCSS",
    "ParaCleHtml", "CommunCouleurJS", "Favicon", "Favicon192", "Manifest",
}


def estimate_bundles(resources):
    bodies = {row["name"]: row["_body"].encode() for row in resources}
    estimates = []
    for name, members in BUNDLE_CANDIDATES.items():
        if not all(member in bodies for member in members):
            continue
        parts = [bodies[member] for member in members]
        merged = b";\n".join(parts)
        estimates.append({
            "name": name, "members": members, "requests_saved": len(parts) - 1,
            "largest_separate": max(map(len, parts)), "merged": len(merged),
            "gzip_separate": sum(len(gzip.compress(part, mtime=0)) for part in parts),
            "gzip_merged": len(gzip.compress(merged, mtime=0)),
        })
    return estimates


def prepare(stage, minify=True, gzip_enabled=True):
    resources = []
    transformed = {}
    if minify:
        try:
            from rjsmin import jsmin
            from rcssmin import cssmin
        except ImportError as error:
            raise SystemExit(
                "Missing asset minifier: install tools/requirements-assets.txt "
                "in .venv, or use --no-minify"
            ) from error

    def transform(match, filename):
        name, body = match.group(1), match.group("body")
        result = body
        kind = "other"
        if re.search(r"JS\d*$", name):
            kind = "js"
            if minify:
                result = jsmin(body)
        elif name == "CommunCSS":
            kind = "css"
            if minify:
                result = cssmin(body)
        elif "<html" in body.lower():
            kind = "html"
            if minify:
                # Preserve markup whitespace, attributes, pre/textarea and script order.
                result = re.sub(
                    r"(<script\b[^>]*>)(.*?)(</script\s*>)",
                    lambda m: m[1] + jsmin(m[2]) + m[3], result, flags=re.S | re.I,
                )
                result = re.sub(
                    r"(<style\b[^>]*>)(.*?)(</style\s*>)",
                    lambda m: m[1] + cssmin(m[2]) + m[3], result, flags=re.S | re.I,
                )
        if f'){match.group("delimiter")}"' in result:
            raise ValueError(f"Raw string delimiter collision in {filename}:{name}")
        resources.append({
            "file": filename, "name": name, "kind": kind,
            "original": len(body.encode()), "prepared": len(result.encode()),
            "gzip": len(gzip.compress(result.encode(), mtime=0)),
            "_body": result,
        })
        start, end = match.span("body")
        return match[0][:start - match.start()] + result + match[0][end - match.start():]

    paths = sorted(set(stage.glob("Page*.h")) | set(stage.glob("JS_*.h")))
    for path in paths:
        text = path.read_text()
        transformed[path] = RAW.sub(lambda m: transform(m, path.name), text)
    if not resources:
        raise ValueError(f"No embedded resources found in {stage}")
    digest = hashlib.sha256(b"gzip" if gzip_enabled else b"identity")
    for resource in resources:
        digest.update(json.dumps(
            [resource["file"], resource["name"], resource["_body"]],
            ensure_ascii=True,
        ).encode())
    tag = digest.hexdigest()[:20]
    metadata = ["#pragma once", "",
                "inline bool GetStaticAssetPayload(const char *data, StaticAssetPayload &payload) {"]
    rows = {row["name"]: row for row in resources}
    if len(rows) != len(resources):
        raise ValueError("Duplicate embedded resource names")
    bundles = estimate_bundles(resources)

    def encode(match):
        name = match[1]
        row = rows[name]
        versioned = URL.sub(
            lambda m: f'{m["quote"]}{m["path"]}?v={tag}{m["quote"]}', match["body"],
        )
        data = versioned.encode()
        packed = gzip.compress(data, mtime=0)
        encoded = gzip_enabled and name in SERVED_ASSETS
        row.update(versioned=len(data), gzip=len(packed),
                   stored=len(packed) if encoded else len(data),
                   encoding="gzip" if encoded else "identity")
        if name in SERVED_ASSETS:
            metadata.extend([
                f"  if (data == {name}) {{",
                f"    payload = {{{row['stored']}, {'true' if encoded else 'false'}}};",
                "    return true;",
                "  }",
            ])
        if encoded:
            literals = ['"' + "".join(f"\\x{byte:02x}" for byte in packed[i:i + 32]) + '"'
                        for i in range(0, len(packed), 32)]
            return (f"const char {name}_Gzip[] PROGMEM =\n  "
                    + "\n  ".join(literals)
                    + f";\nconst char *{name} = {name}_Gzip")
        start, end = match.span("body")
        return match[0][:start - match.start()] + versioned + match[0][end - match.start():]

    for path, text in transformed.items():
        path.write_text(RAW.sub(encode, text))
    metadata.extend(["  return false;", "}", ""])
    (stage / "GeneratedStaticAssets.h").write_text("\n".join(metadata))
    # The fallback header deliberately disables immutable caching for IDE builds.
    cache_header = stage / "StaticAssetCache.h"
    header = cache_header.read_text()
    header, count = re.subn(
        r'^#define STATIC_ASSET_TAG ".*"$',
        f'#define STATIC_ASSET_TAG "{tag}"', header, flags=re.M,
    )
    if count != 1:
        raise ValueError("Expected exactly one STATIC_ASSET_TAG definition")
    cache_header.write_text(header)
    for resource in resources:
        del resource["_body"]
    report = {
        "tag": tag, "minify": minify, "gzip_enabled": gzip_enabled, "resources": resources,
        "bundle_estimates": bundles,
        "note": "original/prepared exclude URL suffixes; versioned/stored include them. "
                "gzip is the compressed versioned size (estimate if encoding is identity). "
                "stored excludes the C string terminator, alignment and dynamic endpoints.",
    }
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--no-minify", action="store_true")
    parser.add_argument("--no-gzip", action="store_true")
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    report = prepare(args.stage, not args.no_minify, not args.no_gzip)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    totals = {key: sum(r[key] for r in report["resources"])
              for key in ("original", "prepared", "stored")}
    print(f"Assets {report['tag']}: {totals} bytes (gzip={report['gzip_enabled']})")


if __name__ == "__main__":
    main()
