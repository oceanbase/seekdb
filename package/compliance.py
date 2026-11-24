#!/usr/bin/env python3
"""Generate and stage the redistribution compliance bundle.

Each directory under ``licenses/`` is one third-party component.  It contains
``METADATA.json`` and the original license/notice text files.  This module
walks those directories, validates the metadata, and renders the release
bundle into native package payloads and the portable binary distribution.
"""

import argparse
import hashlib
import json
import re
import shutil
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LICENSES = ROOT / "licenses"
VERSION_FILE = ROOT / "bazel" / "compile_config.bzl"
PROJECT = {
    "name": "seekdb",
    "source": "https://github.com/oceanbase/seekdb",
    "license": "Apache-2.0",
    "license_files": ["LICENSE"],
    "notice_files": ["NOTICE"],
}
REQUIRED_COMPONENT_FIELDS = (
    "name",
    "version",
    "source",
    "license",
    "modified",
    "license_files",
    "redistribution",
)


def project_version():
    match = re.search(r'^VERSION\s*=\s*"([^"]+)"', VERSION_FILE.read_text(encoding="utf-8"), re.MULTILINE)
    if not match:
        raise ValueError("cannot determine project version from %s" % VERSION_FILE)
    return match.group(1)


def load_manifest():
    components = []
    names = set()
    for directory in sorted(LICENSES.iterdir()):
        if not directory.is_dir():
            continue
        metadata_path = directory / "METADATA.json"
        if not metadata_path.is_file():
            raise ValueError("component directory %s is missing METADATA.json" % directory)
        component = json.loads(metadata_path.read_text(encoding="utf-8"))
        component["directory"] = directory.relative_to(ROOT).as_posix()
        for key in REQUIRED_COMPONENT_FIELDS:
            if key not in component:
                raise ValueError("component %r is missing %s" % (component.get("name"), key))
        key = component["name"].lower()
        if key in names:
            raise ValueError("duplicate component name: %s" % component["name"])
        names.add(key)
        if not component["license_files"]:
            raise ValueError("component %s has no license files" % component["name"])
        for relative in component["license_files"]:
            path = directory / relative
            if not path.is_file():
                raise ValueError("component %s references missing license file %s" % (
                    component["name"], path.relative_to(ROOT)))
        components.append(component)
    if not components:
        raise ValueError("no component directories found under %s" % LICENSES)
    return {
        "schema_version": 1,
        "project": PROJECT,
        "components": components,
    }


def markdown(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


def component_license_path(component, relative):
    return "%s/%s" % (Path(component["directory"]).name, relative)


def license_cell(component):
    values = ["`%s`" % component_license_path(component, relative)
              for relative in component["license_files"]]
    return "<br>".join(values)


def render_components(data, version, source_commit):
    lines = [
        "# Third-party components",
        "",
        "This inventory is generated from the per-component `METADATA.json`",
        "files under `licenses/<component>/`.  Entries marked `unspecified` still",
        "require provenance work before a release can claim complete third-party",
        "coverage.",
        "",
        "- Project: `%s`" % data["project"]["name"],
        "- Version: `%s`" % version,
        "- Source commit: `%s`" % (source_commit or "not recorded"),
        "",
        "| Component | Version | Upstream source | Packaged as | License | Modified | License text | Redistribution |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for component in sorted(data["components"], key=lambda item: item["name"].lower()):
        lines.append("| %s | %s | %s | %s | %s | %s | %s | %s |" % (
            markdown(component["name"]),
            markdown(component["version"]),
            markdown(component["source"]),
            markdown(component["packaged_as"]),
            markdown(component["license"]),
            markdown(component["modified"]),
            license_cell(component),
            markdown(component["redistribution"]),
        ))
    lines.extend([
        "",
        "The original license and notice texts are under `<component>/` in the",
        "installed compliance directory.  `REDISTRIBUTION.md` describes the",
        "per-component retention and source/relink requirements.",
        "",
    ])
    return "\n".join(lines)


def render_redistribution(data):
    lines = [
        "# Redistribution requirements",
        "",
        "This file is generated from the per-component `METADATA.json` files.",
        "Preserve the complete compliance directory when redistributing a package",
        "that contains these components.  License and notice texts must remain",
        "verbatim.",
        "",
        "| Component | Files to retain | Redistribution requirement | Source |",
        "| --- | --- | --- | --- |",
    ]
    for component in sorted(data["components"], key=lambda item: item["name"].lower()):
        if component.get("source_offer"):
            requirement = component["redistribution"] + " See `SOURCE-OFFER.txt`."
        else:
            requirement = component["redistribution"]
        lines.append("| %s | %s | %s | %s |" % (
            markdown(component["name"]),
            license_cell(component),
            markdown(requirement),
            markdown(component["source"]),
        ))
    lines.extend([
        "",
        "## General rules",
        "",
        "- Keep `LICENSE`, `NOTICE`, this file, `THIRD_PARTY_COMPONENTS.md`, and",
        "  the complete `licenses/` directory together.",
        "- Do not rewrite upstream license or notice text.  If a component is",
        "  modified, retain its notice and record the modification in the source",
        "  repository.",
        "- If a component is covered by a copyleft license and is bundled, follow",
        "  its source and relink/replacement requirements.  `SOURCE-OFFER.txt`",
        "  records the known offers.",
        "- Separately installed dependencies are not bundled by this package.  A",
        "  downstream application that redistributes them must preserve the license",
        "  materials from the exact resolved versions.",
        "",
    ])
    return "\n".join(lines)


def render_source_offer(data):
    lines = [
        "Corresponding source offers",
        "============================",
        "",
        "This file is generated from the per-component `METADATA.json` files.",
        "",
    ]
    offers = [component for component in data["components"] if component.get("source_offer")]
    if not offers:
        lines.append("No component in this bundle requires a corresponding-source offer.")
    for component in sorted(offers, key=lambda item: item["name"].lower()):
        lines.extend([
            "%s %s" % (component["name"], component["version"]),
            "-" * len("%s %s" % (component["name"], component["version"])),
            component["source_offer"],
            "",
        ])
    return "\n".join(lines)


def copy_file(source, destination):
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, destination)
    destination.chmod(0o644)


def write_manifest(target):
    lines = []
    for path in sorted(target.rglob("*")):
        if path.is_file() and path.name != "MANIFEST.sha256":
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            lines.append("%s  %s" % (digest, path.relative_to(target).as_posix()))
    (target / "MANIFEST.sha256").write_text("\n".join(lines) + "\n", encoding="utf-8")


def stage(target, version=None, source_commit=None):
    data = load_manifest()
    version = version or project_version()
    target = Path(target)
    target.mkdir(parents=True, exist_ok=True)

    for name in data["project"]["license_files"]:
        copy_file(ROOT / name, target / Path(name).name)
    for name in data["project"]["notice_files"]:
        copy_file(ROOT / name, target / Path(name).name)
    shutil.copytree(
        LICENSES,
        target,
        dirs_exist_ok=True,
        ignore=shutil.ignore_patterns("METADATA.json", "README.md"),
    )

    artifact = {
        "name": data["project"]["name"],
        "version": version,
        "source_commit": source_commit,
        "license": data["project"]["license"],
    }
    inventory = {
        "schema_version": data["schema_version"],
        "artifact": artifact,
        "components": data["components"],
    }
    (target / "THIRD_PARTY_COMPONENTS.json").write_text(
        json.dumps(inventory, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (target / "THIRD_PARTY_COMPONENTS.md").write_text(
        render_components(data, version, source_commit), encoding="utf-8")
    (target / "REDISTRIBUTION.md").write_text(render_redistribution(data), encoding="utf-8")
    (target / "SOURCE-OFFER.txt").write_text(render_source_offer(data), encoding="utf-8")
    write_manifest(target)
    return target


def _license_name(component):
    return "seekdb-" + re.sub(r"[^a-z0-9]+", "-", component["name"].lower()).strip("-")


def _license_text(component):
    parts = []
    for relative in component["license_files"]:
        path = ROOT / component["directory"] / relative
        parts.append(path.read_text(encoding="utf-8", errors="replace").rstrip())
    return "\n\n".join(parts)


def render_debian_copyright(data):
    lines = [
        "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/",
        "Upstream-Name: seekdb",
        "Source: https://github.com/oceanbase/seekdb",
        "",
        "Files: *",
        "Copyright: 2026 OceanBase and contributors",
        "License: Apache-2.0",
        "",
    ]
    for component in sorted(data["components"], key=lambda item: item["name"].lower()):
        files = ["%s/%s" % (component["directory"], relative)
                 for relative in component["license_files"]]
        lines.append("Files: %s" % " ".join(files))
        lines.extend([
            "Copyright: See the corresponding upstream license and notice files.",
            "License: %s" % _license_name(component),
            "Comment: %s %s; %s" % (
                component["name"], component["version"], component["redistribution"]),
            "",
        ])
    lines.extend([
        "Files: NOTICE",
        "Copyright: See the NOTICE file.",
        "License: seekdb-notice",
        "Comment: Project NOTICE with third-party notices.",
        "",
        "License: Apache-2.0",
    ])
    apache = (ROOT / "LICENSE").read_text(encoding="utf-8").rstrip()
    lines.extend(_debian_text_lines(apache))
    lines.append("")
    lines.extend([
        "License: seekdb-notice",
    ])
    lines.extend(_debian_text_lines((ROOT / "NOTICE").read_text(encoding="utf-8").rstrip()))
    lines.append("")
    for component in sorted(data["components"], key=lambda item: item["name"].lower()):
        lines.extend([
            "License: %s" % _license_name(component),
            " " + component["name"] + " " + component["version"] + " license and notice text.",
        ])
        text = _license_text(component)
        lines.extend(_debian_text_lines(text))
        lines.append("")
    return "\n".join(lines)


def _debian_text_lines(text):
    for line in text.splitlines():
        yield " ." if not line.strip() else " " + line


def write_debian_copyright(path):
    data = load_manifest()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(render_debian_copyright(data), encoding="utf-8")


def check():
    data = load_manifest()
    unspecified = [component["name"] for component in data["components"] if component["version"] == "unspecified"]
    if unspecified:
        print("WARNING: %d component(s) have unspecified versions: %s" % (
            len(unspecified), ", ".join(sorted(unspecified))))
    with tempfile.TemporaryDirectory() as directory:
        target = stage(directory, project_version(), None)
        files = [path for path in target.rglob("*") if path.is_file()]
    print("compliance bundle OK: %d components, %d files" % (len(data["components"]), len(files)))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subcommands = parser.add_subparsers(dest="command", required=True)
    subcommands.add_parser("check", help="validate component metadata and render the bundle")
    stage_parser = subcommands.add_parser("stage", help="stage the bundle into a package payload")
    stage_parser.add_argument("--target", type=Path, required=True)
    stage_parser.add_argument("--version", default=None)
    stage_parser.add_argument("--source-commit", default=None)
    arguments = parser.parse_args()
    if arguments.command == "check":
        return check()
    stage(arguments.target, arguments.version, arguments.source_commit)
    print("staged compliance bundle in %s" % arguments.target)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(str(error))
