#!/usr/bin/env python3
"""Report declared versus observed C++ compile inputs for a Bazel target.

The declared side comes from ``bazel aquery``.  The observed side comes from
Clang dependency files left by completed compile actions.  The script never
builds its target: a missing dependency file is reported explicitly so callers
can decide which local target to compile.
"""

import argparse
import json
import math
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from typing import Dict, List, Mapping, MutableMapping, Optional, Sequence, Set


HEADER_SUFFIXES = {
    ".def",
    ".h",
    ".hh",
    ".hpp",
    ".hxx",
    ".inc",
    ".inl",
    ".ipp",
    ".map",
    ".tcc",
}

PROJECT_PREFIXES = (
    "src/",
    "src/oblib/easy/",
    "src/oblib/",
    "external/+seekdb_third_party_headers_repository+seekdb_3rd_headers/",
    "bazel-out/",
)

THIRD_PARTY_PREFIX = (
    "external/+seekdb_third_party_headers_repository+seekdb_3rd_headers/"
)

ROOT_THIRD_PARTY_COMPONENTS = {
    "libaio.h": "libaio",
    "zconf.h": "zlib",
    "zlib.h": "zlib",
}


def _id(value: object) -> str:
    return str(value)


def _run_stdout(argv: Sequence[str], cwd: Path) -> str:
    completed = subprocess.run(
        argv,
        cwd=str(cwd),
        check=True,
        stdout=subprocess.PIPE,
        stderr=None,
        universal_newlines=True,
    )
    return completed.stdout.strip()


def _path_fragment_resolver(
    fragments: Sequence[Mapping[str, object]],
):
    by_id = {_id(fragment["id"]): fragment for fragment in fragments}
    cache: Dict[str, str] = {}

    def resolve(fragment_id: object) -> str:
        key = _id(fragment_id)
        if key in cache:
            return cache[key]
        fragment = by_id[key]
        label = str(fragment.get("label", ""))
        parent_id = fragment.get("parentId")
        if parent_id is None:
            result = label
        else:
            parent = resolve(parent_id)
            result = f"{parent}/{label}" if parent else label
        cache[key] = result
        return result

    return resolve


def _expand_dep_set(
    dep_set_id: object,
    dep_sets: Mapping[str, Mapping[str, object]],
    cache: MutableMapping[str, Set[str]],
) -> Set[str]:
    key = _id(dep_set_id)
    if key in cache:
        return cache[key]
    dep_set = dep_sets[key]
    result = {_id(value) for value in dep_set.get("directArtifactIds", [])}
    for child in dep_set.get("transitiveDepSetIds", []):
        result.update(_expand_dep_set(child, dep_sets, cache))
    cache[key] = result
    return result


def _depfile_from_action(
    action: Mapping[str, object], artifact_paths: Mapping[str, str]
) -> Optional[str]:
    arguments = [str(value) for value in action.get("arguments", [])]
    try:
        marker = arguments.index("-MF")
        return arguments[marker + 1]
    except (ValueError, IndexError):
        pass
    for output_id in action.get("outputIds", []):
        path = artifact_paths[_id(output_id)]
        if path.endswith(".d"):
            return path
    return None


def _read_depfile(path: Path) -> Set[str]:
    content = path.read_text(encoding="utf-8", errors="surrogateescape")
    content = content.replace("\\\n", " ")
    delimiter = content.find(":")
    if delimiter < 0:
        raise ValueError(f"dependency file has no target delimiter: {path}")
    return set(shlex.split(content[delimiter + 1 :], posix=True))


def _normalize_observed_path(
    value: str, workspace: Path, execution_root: Path
) -> Optional[str]:
    path = Path(value)
    if not path.is_absolute():
        return value[2:] if value.startswith("./") else value
    try:
        return str(path.relative_to(execution_root))
    except ValueError:
        pass
    try:
        relative = path.relative_to(workspace)
    except ValueError:
        return None
    # Clang's builtin headers are inside the checked-out SDK but are toolchain
    # inputs, not dependencies that a seekdb cc_library should own.
    if str(relative).startswith(
        "deps/3rd/usr/local/oceanbase/devtools/lib/clang/"
    ):
        return None
    return str(relative)


def _is_project_input(path: str) -> bool:
    return path.startswith(PROJECT_PREFIXES)


def _is_header(path: str) -> bool:
    return Path(path).suffix.lower() in HEADER_SUFFIXES


def _third_party_component(path: str) -> Optional[str]:
    if not path.startswith(THIRD_PARTY_PREFIX):
        return None
    relative = path[len(THIRD_PARTY_PREFIX) :]
    if relative.startswith("include/"):
        relative = relative[len("include/") :]
    elif relative.startswith("devtools/include/"):
        relative = relative[len("devtools/include/") :]
    else:
        return None
    first = relative.split("/", 1)[0]
    if first.startswith("libunwind") or first == "unwind.h":
        return "libunwind"
    return ROOT_THIRD_PARTY_COMPONENTS.get(first, first)


def _percentile(values: Sequence[float], percentile: float) -> Optional[float]:
    if not values:
        return None
    ordered = sorted(values)
    index = max(0, math.ceil(percentile * len(ordered)) - 1)
    return ordered[index]


def _display_number(value: Optional[float]) -> str:
    return "n/a" if value is None else f"{value:.2f}"


def _load_aquery(
    workspace: Path,
    target: str,
    aquery_json: Optional[Path],
    include_dependencies: bool,
) -> Mapping[str, object]:
    if aquery_json is not None:
        return json.loads(aquery_json.read_text(encoding="utf-8"))
    with tempfile.NamedTemporaryFile(prefix="seekdb-aquery-", suffix=".json") as output:
        subprocess.run(
            [
                str(workspace / "bazel.py"),
                "aquery",
                "mnemonic(CppCompile, %s)"
                % (f"deps({target})" if include_dependencies else target),
                "--output=jsonproto",
                "--include_artifacts=true",
            ],
            cwd=str(workspace),
            check=True,
            stdout=output,
        )
        output.flush()
        output.seek(0)
        return json.load(output)


def _collect_report(
    graph: Mapping[str, object],
    workspace: Path,
    execution_root: Path,
    include_paths: bool,
) -> Mapping[str, object]:
    resolve_fragment = _path_fragment_resolver(graph.get("pathFragments", []))
    artifact_paths = {
        _id(artifact["id"]): resolve_fragment(artifact["pathFragmentId"])
        for artifact in graph.get("artifacts", [])
    }
    dep_sets = {
        _id(dep_set["id"]): dep_set for dep_set in graph.get("depSetOfFiles", [])
    }
    targets = {
        _id(target["id"]): str(target["label"])
        for target in graph.get("targets", [])
    }
    dep_set_cache: Dict[str, Set[str]] = {}
    rows: List[Mapping[str, object]] = []

    for action in graph.get("actions", []):
        if action.get("mnemonic") != "CppCompile":
            continue
        input_ids: Set[str] = set()
        for dep_set_id in action.get("inputDepSetIds", []):
            input_ids.update(_expand_dep_set(dep_set_id, dep_sets, dep_set_cache))
        declared = {artifact_paths[value] for value in input_ids}
        declared_project = {value for value in declared if _is_project_input(value)}
        declared_headers = {value for value in declared_project if _is_header(value)}

        primary_output = artifact_paths.get(
            _id(action.get("primaryOutputId", "")), ""
        )
        output = execution_root / primary_output if primary_output else None
        output_present = bool(output and output.is_file())
        depfile_relative = _depfile_from_action(action, artifact_paths)
        depfile = execution_root / depfile_relative if depfile_relative else None
        depfile_present = bool(depfile and depfile.is_file())
        action_completed = output_present and depfile_present
        observed_project: Set[str] = set()
        observed_headers: Set[str] = set()
        third_party_components: Set[str] = set()
        if action_completed:
            observed = set()
            for value in _read_depfile(depfile):
                normalized = _normalize_observed_path(
                    value, workspace, execution_root
                )
                if normalized is not None:
                    observed.add(normalized)
            observed_project = {
                value for value in observed if _is_project_input(value)
            }
            observed_headers = {
                value for value in observed_project if _is_header(value)
            }
            for value in observed_project:
                component = _third_party_component(value)
                if component is not None:
                    third_party_components.add(component)

        input_ratio = (
            len(declared_project) / len(observed_project)
            if observed_project
            else None
        )
        header_ratio = (
            len(declared_headers) / len(observed_headers)
            if observed_headers
            else None
        )
        row = {
            "target": targets.get(_id(action.get("targetId", "")), "<unknown>"),
            "output": primary_output,
            "output_present": output_present,
            "depfile": depfile_relative,
            "depfile_present": depfile_present,
            "action_completed": action_completed,
            "declared_project_inputs": len(declared_project),
            "observed_project_inputs": len(observed_project),
            "input_ratio": input_ratio,
            "declared_headers": len(declared_headers),
            "observed_headers": len(observed_headers),
            "header_ratio": header_ratio,
            "third_party_components": sorted(third_party_components),
        }
        if include_paths:
            row["observed_project_paths"] = sorted(observed_project)
        rows.append(row)

    ratios = [float(row["input_ratio"]) for row in rows if row["input_ratio"]]
    header_ratios = [
        float(row["header_ratio"]) for row in rows if row["header_ratio"]
    ]
    return {
        "actions": len(rows),
        "actions_with_depfile": sum(bool(row["depfile_present"]) for row in rows),
        "actions_completed": sum(bool(row["action_completed"]) for row in rows),
        "input_ratio_p50": _percentile(ratios, 0.50),
        "input_ratio_p95": _percentile(ratios, 0.95),
        "input_ratio_max": max(ratios) if ratios else None,
        "header_ratio_p50": _percentile(header_ratios, 0.50),
        "header_ratio_p95": _percentile(header_ratios, 0.95),
        "header_ratio_max": max(header_ratios) if header_ratios else None,
        "rows": sorted(
            rows,
            key=lambda row: (
                row["input_ratio"] is not None,
                row["input_ratio"] or 0,
                row["declared_project_inputs"],
            ),
            reverse=True,
        ),
    }


def _print_text(report: Mapping[str, object], limit: int) -> None:
    print(
        "actions={actions} completed={actions_completed} "
        "depfiles={actions_with_depfile} "
        "input_ratio[p50={p50},p95={p95},max={maximum}] "
        "header_ratio[p50={hp50},p95={hp95},max={hmax}]".format(
            actions=report["actions"],
            actions_completed=report["actions_completed"],
            actions_with_depfile=report["actions_with_depfile"],
            p50=_display_number(report["input_ratio_p50"]),
            p95=_display_number(report["input_ratio_p95"]),
            maximum=_display_number(report["input_ratio_max"]),
            hp50=_display_number(report["header_ratio_p50"]),
            hp95=_display_number(report["header_ratio_p95"]),
            hmax=_display_number(report["header_ratio_max"]),
        )
    )
    print(
        "declared  observed  ratio  declared_h  observed_h  h_ratio  target/output"
    )
    rows = report["rows"]
    shown = rows if limit == 0 else rows[:limit]
    for row in shown:
        print(
            f"{row['declared_project_inputs']:8d}  "
            f"{row['observed_project_inputs']:8d}  "
            f"{_display_number(row['input_ratio']):>5}  "
            f"{row['declared_headers']:10d}  "
            f"{row['observed_headers']:10d}  "
            f"{_display_number(row['header_ratio']):>7}  "
            f"{row['target']} {row['output']}"
        )
    missing = report["actions"] - report["actions_with_depfile"]
    if missing:
        print(
            f"note: {missing} action(s) have no depfile; build only the relevant "
            "target before treating percentiles as complete",
            file=sys.stderr,
        )
    incomplete = report["actions"] - report["actions_completed"]
    if incomplete:
        print(
            f"note: {incomplete} action(s) have no primary output; only "
            "successful compile actions have complete observations",
            file=sys.stderr,
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", help="Bazel target or target expression for aquery")
    parser.add_argument(
        "--aquery-json",
        type=Path,
        help="read an existing jsonproto file instead of invoking Bazel",
    )
    parser.add_argument(
        "--direct",
        action="store_true",
        help="measure only actions owned by target, not its dependency closure",
    )
    parser.add_argument(
        "--execution-root",
        type=Path,
        help="Bazel execution root containing depfiles",
    )
    parser.add_argument(
        "--format", choices=("text", "json"), default="text", dest="output_format"
    )
    parser.add_argument(
        "--include-paths",
        action="store_true",
        help="include observed project paths in JSON for ownership generation",
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=20,
        help="maximum action rows in text output; use 0 for every action",
    )
    arguments = parser.parse_args()

    workspace = Path(__file__).resolve().parents[2]
    execution_root = arguments.execution_root
    if execution_root is None:
        execution_root = Path(
            _run_stdout(
                [str(workspace / "bazel.py"), "info", "execution_root"],
                workspace,
            )
        )
    graph = _load_aquery(
        workspace,
        arguments.target,
        arguments.aquery_json,
        include_dependencies=not arguments.direct,
    )
    report = _collect_report(
        graph,
        workspace,
        execution_root.resolve(),
        include_paths=arguments.include_paths,
    )
    if arguments.output_format == "json":
        json.dump(report, sys.stdout, indent=2, sort_keys=True)
        print()
    else:
        _print_text(report, arguments.limit)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
