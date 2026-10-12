#!/usr/bin/env python3
"""Write the build manifest (YAML-compatible text) next to an archived firmware.

Called by build-binary.sh; not meant to be run by hand.
"""
import argparse
import datetime
import hashlib
import json
import os
import socket
import subprocess
import sys


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def q(value):
    return json.dumps("" if value is None else str(value), ensure_ascii=False)


def cmd(*args):
    try:
        return subprocess.run(args, capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return ""


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--out", required=True)
    p.add_argument("--compile-json", required=True)
    p.add_argument("--stage-dir", required=True)
    p.add_argument("--release-dir", required=True)
    p.add_argument("--sketch", required=True)
    p.add_argument("--version", default="")
    p.add_argument("--build-tag", required=True)
    p.add_argument("--git-sha", default="")
    p.add_argument("--fqbn", required=True)
    p.add_argument("--profile", default="")
    p.add_argument("--app-partition-size", type=int, default=0)
    p.add_argument("--source-section", default="")
    p.add_argument("--artifact", action="append", default=[])
    a = p.parse_args()

    with open(a.compile_json, encoding="utf-8") as f:
        cj = json.load(f)
    br = cj.get("builder_result") or cj.get("result") or {}

    lines = [
        "# Solar Router firmware build manifest",
        "manifest_version: 1",
        f"build_tag: {q(a.build_tag)}",
        f"build_date: {q(datetime.datetime.now().astimezone().isoformat(timespec='seconds'))}",
        f"build_host: {q(socket.gethostname())}",
        f"build_user: {q(os.environ.get('USER', ''))}",
        "firmware:",
        f"  sketch: {q(a.sketch)}",
        f"  version: {q(a.version)}",
        f"  build_tag: {q(a.build_tag)}",
        f"  git_sha: {q(a.git_sha)}",
    ]

    if a.source_section and os.path.isfile(a.source_section):
        with open(a.source_section, encoding="utf-8") as f:
            lines += [l.rstrip("\n") for l in f]
    else:
        lines.append("source: {}")

    lines += [
        "toolchain:",
        f"  arduino_cli: {q(cmd('arduino-cli', 'version'))}",
        f"  fqbn: {q(a.fqbn)}",
        f"  profile: {q(a.profile or '(none, global install)')}",
        f"  network_connection_timeout: {q(os.environ.get('ARDUINO_NETWORK_CONNECTION_TIMEOUT') or cmd('arduino-cli', 'config', 'get', 'network.connection_timeout'))}",
    ]
    for key in ("board_platform", "build_platform"):
        plat = br.get(key) or {}
        if plat:
            lines += [
                f"  {key}:",
                f"    id: {q(plat.get('id'))}",
                f"    version: {q(plat.get('version'))}",
                f"    install_dir: {q(plat.get('install_dir'))}",
            ]
    libs = br.get("used_libraries") or []
    lines.append("  libraries:" + ("" if libs else " []"))
    for lib in sorted(libs, key=lambda l: l.get("name", "")):
        lines += [
            f"    - name: {q(lib.get('name'))}",
            f"      version: {q(lib.get('version'))}",
            f"      location: {q(lib.get('location'))}",
            f"      install_dir: {q(lib.get('install_dir'))}",
        ]

    stage_files = []
    for name in ("sketch.yaml", "build_opt.h", "partitions.csv"):
        path = os.path.join(a.stage_dir, name)
        if os.path.isfile(path):
            stage_files.append((name, path))
    lines.append("build_inputs:")
    for name, path in stage_files:
        lines.append(f"  {name}:")
        lines.append(f"    sha256: {q(sha256(path))}")
        if name == "build_opt.h":
            with open(path, encoding="utf-8", errors="replace") as f:
                lines.append("    content:")
                lines += [f"      - {q(l.rstrip())}" for l in f if l.strip()]

    lines.append("artifacts:")
    for art in a.artifact:
        path = os.path.join(a.release_dir, art)
        if os.path.isfile(path):
            lines += [
                f"  - file: {q(art)}",
                f"    size: {os.path.getsize(path)}",
                f"    sha256: {q(sha256(path))}",
            ]

    sizes = br.get("executable_sections_size") or []
    if sizes:
        lines.append("sections:")
        for s in sizes:
            lines.append(f"  - name: {q(s.get('name'))}")
            lines.append(f"    size: {s.get('size', 0)}")
            lines.append(f"    max_size: {s.get('max_size', 0)}")
    if a.app_partition_size:
        lines.append(f"app_partition_size: {a.app_partition_size}")

    with open(a.out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
