#!/usr/bin/env python3
"""Source dependency guard for layer and architectural-domain boundaries.

The checker enforces eight complementary policies:

* Layer policy: modules listed in ``[scanned]`` may only include a target whose
  numeric layer is less than or equal to their own layer.
* Domain policy: paths listed in ``[domains]`` may only cross to another
  declared domain through an edge listed in ``[allowed_domain_edges]`` or a
  quarantined edge listed in ``[bridge_domain_edges]``.
* Bridge policy: every quarantined bridge-to-implementation include is kept in
  an exact baseline.  New bridge surface fails the normal check; removing
  bridge surface requires tightening the baseline.
* Bridge-consumer policy: every query/data-plane include of a transitional
  bridge is kept in a second exact baseline, so existing umbrella headers
  cannot acquire new callers while their implementation debt is unchanged.
* Bridge-artifact policy: source files under the migration-only bridge roots
  are kept in an exact baseline.  The normal check rejects new artifacts and
  requires the baseline to shrink when one is deleted; ``--strict-bridges``
  additionally requires the baseline to be empty.
* Compatibility-artifact policy: deleted forwarding headers are permanently
  forbidden so an unused escape hatch cannot bypass include-edge ratchets.
* Consumer policy: a module named as an ``[allowed_consumers]`` target may
  only be included by itself or by the exact files/directories listed there.
* Public-quarantine-consumer policy: direct data-plane consumers of the
  transitional query aggregate runtime header are kept in an independent
  exact baseline.  The normal check rejects both additions and stale entries,
  while reporting (but not requiring removal of) the residual consumers.
* Coverage policy: every C/C++ artifact under the maintained ``src``, Easy,
  and OBLib roots must belong to a configured ``[scanned]`` tree, so a new
  first-level module cannot bypass all dependency checks by omission.

The baseline-backed policies deliberately use separate files.  Historical
layer debt must not hide query/data-plane boundary debt, and each baseline is
an exact ratchet: once an edge disappears the build fails until the baseline
is tightened, so the dependency cannot silently return later.

Usage:
  python3 module_layer_check.py [repo_root]
  python3 module_layer_check.py [repo_root] --strict
  python3 module_layer_check.py [repo_root] --strict-boundaries
  python3 module_layer_check.py [repo_root] --strict-bridges
  python3 module_layer_check.py [repo_root] --update-baseline
  python3 module_layer_check.py [repo_root] --update-boundary-baseline
  python3 module_layer_check.py [repo_root] --update-bridge-baseline
  python3 module_layer_check.py [repo_root] --update-bridge-consumer-baseline
  python3 module_layer_check.py [repo_root] --update-bridge-artifact-baseline
  python3 module_layer_check.py [repo_root] --update-public-quarantine-consumer-baseline
"""

import os
import re
import sys


HERE = os.path.dirname(os.path.abspath(__file__))
POSITIONAL_ARGS = [arg for arg in sys.argv[1:] if not arg.startswith("--")]
REPO = os.path.abspath(POSITIONAL_ARGS[0]) if POSITIONAL_ARGS else os.path.abspath(
    os.path.join(HERE, "../..")
)
CONF = os.path.join(HERE, "module_layers.conf")
LAYER_BASELINE = os.path.join(HERE, "module_layer_baseline.txt")
BOUNDARY_BASELINE = os.path.join(HERE, "module_boundary_baseline.txt")
BRIDGE_BASELINE = os.path.join(HERE, "module_bridge_baseline.txt")
BRIDGE_CONSUMER_BASELINE = os.path.join(HERE, "module_bridge_consumer_baseline.txt")
BRIDGE_ARTIFACT_BASELINE = os.path.join(HERE, "module_bridge_artifact_baseline.txt")
PUBLIC_QUARANTINE_CONSUMER_BASELINE = os.path.join(
    HERE, "module_public_quarantine_consumer_baseline.txt"
)

UPDATE_LAYER = "--update-baseline" in sys.argv
UPDATE_BOUNDARY = "--update-boundary-baseline" in sys.argv
UPDATE_BRIDGE = "--update-bridge-baseline" in sys.argv
UPDATE_BRIDGE_CONSUMER = "--update-bridge-consumer-baseline" in sys.argv
UPDATE_BRIDGE_ARTIFACT = "--update-bridge-artifact-baseline" in sys.argv
UPDATE_PUBLIC_QUARANTINE_CONSUMER = (
    "--update-public-quarantine-consumer-baseline" in sys.argv
)
STRICT_LAYER = "--strict" in sys.argv
STRICT_BOUNDARY = "--strict-boundaries" in sys.argv
STRICT_BRIDGE = "--strict-bridges" in sys.argv

SOURCE_ROOTS = ("src/oblib/", "src/")
SOURCE_EXTENSIONS = (".h", ".hpp", ".hh", ".cpp", ".cc", ".cxx", ".ipp", ".c")
PREPROCESSOR_DIRECTIVE_PATTERN = re.compile(
    r"^[ \t]*#[ \t]*([A-Za-z_][A-Za-z0-9_]*)(.*)$", re.MULTILINE
)
INCLUDE_ARGUMENT_PATTERN = re.compile(r'^[ \t]*(?:"([^"]+)"|<([^>]+)>)')
LEXICAL_TOKEN_PATTERN = re.compile(
    r'(?<!\w)(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(|/\*|//|["\']'
)
NON_NEWLINE_RUN_PATTERN = re.compile(r"[^\n]+")
SOURCE_MARKER_PATTERN = re.compile(
    r'#|/\*|\*/|R"|\)[^\s()\\]{0,16}"'
)

# These are migration-only roots, not permanent extension points.  Edge
# baselines cannot detect an unused umbrella, an empty compatibility header,
# or a newly added source artifact with no cross-domain include.  Keep the
# roots explicit so strict CI continues to forbid those artifacts after both
# bridge baselines reach zero and the directories disappear from the tree.
FORBIDDEN_BRIDGE_SOURCE_ROOTS = (
    "src/data_plane/api/data_plane/bridge",
    "src/query/api/query/bridge",
)

# Deleted compatibility headers are not migration debt: they are forbidden
# artifacts.  Keep the exact paths here because include-edge baselines cannot
# detect an unused forwarding header that has been reintroduced.
FORBIDDEN_COMPATIBILITY_SOURCE_ARTIFACTS = (
    "src/observer/ob_server_struct.h",
)

# This is intentionally narrower than the allowed data_plane -> query_api
# domain edge.  It freezes only direct consumers of the broad transitional
# runtime facade while callers migrate to the final aggregate protocol.
PUBLIC_QUARANTINE_API_INCLUDE = "__no_public_quarantine_api__"
PUBLIC_QUARANTINE_API_PATH = "src/query/api/" + PUBLIC_QUARANTINE_API_INCLUDE
def _derive_include_prefix(path):
    for root in SOURCE_ROOTS:
        if path.startswith(root):
            return path[len(root) :]
    return path


def _is_under(path, prefix):
    return path == prefix or path.startswith(prefix + "/")


def _parse_edge(tokens, section, line_number):
    if len(tokens) != 3 or tokens[1] != "->":
        raise ValueError(
            "%s:%d: [%s] entry must be '<source> -> <target>'"
            % (CONF, line_number, section)
        )
    return tokens[0], tokens[2]


def _splice_preprocessor_lines(source):
    """Apply C/C++ backslash-newline splicing before parsing directives."""
    return re.sub(r"\\(?:\r\n|\n|\r)", "", source)


def _mask_non_newlines(source):
    """Replace source text with whitespace while preserving line boundaries."""
    return NON_NEWLINE_RUN_PATTERN.sub(" ", source)


def _quoted_literal_end(source, start, quote):
    """Return one past a quote not escaped by an odd backslash run."""
    search_from = start + 1
    while True:
        end = source.find(quote, search_from)
        if end < 0:
            return len(source)
        backslash_count = 0
        cursor = end - 1
        while cursor > start and source[cursor] == "\\":
            backslash_count += 1
            cursor -= 1
        if backslash_count % 2 == 0:
            return end + 1
        search_from = end + 1


def _strip_c_cpp_comments(source):
    """Return sanitized lines that can affect directive parsing or lexer state."""
    result = []
    state = "code"
    raw_terminator = None
    search_from = 0
    while True:
        marker_match = SOURCE_MARKER_PATTERN.search(source, search_from)
        if marker_match is None:
            break
        line_start = source.rfind("\n", 0, marker_match.start()) + 1
        line_end = source.find("\n", marker_match.end())
        line_end = len(source) if line_end < 0 else line_end
        line = source[line_start:line_end]
        line_result = []
        index = 0
        while index < len(line):
            if state == "block_comment":
                end = line.find("*/", index)
                if end < 0:
                    line_result.append(_mask_non_newlines(line[index:]))
                    index = len(line)
                else:
                    end += 2
                    line_result.append(_mask_non_newlines(line[index:end]))
                    index = end
                    state = "code"
                continue
            if state == "raw":
                end = line.find(raw_terminator, index)
                if end < 0:
                    line_result.append("x" + _mask_non_newlines(line[index:]))
                    index = len(line)
                else:
                    end += len(raw_terminator)
                    line_result.append("x" + _mask_non_newlines(line[index:end]))
                    index = end
                    state = "code"
                    raw_terminator = None
                continue

            token_match = LEXICAL_TOKEN_PATTERN.search(line, index)
            if token_match is None:
                line_result.append(line[index:])
                break

            line_result.append(line[index:token_match.start()])
            token = token_match.group(0)
            if token_match.group(1) is not None:
                raw_terminator = ")" + token_match.group(1) + '"'
                end = line.find(raw_terminator, token_match.end())
                if end < 0:
                    line_result.append(
                        "x" + _mask_non_newlines(line[token_match.start():])
                    )
                    index = len(line)
                    state = "raw"
                else:
                    end += len(raw_terminator)
                    line_result.append(
                        "x" + _mask_non_newlines(line[token_match.start():end])
                    )
                    index = end
            elif token == "/*":
                end = line.find("*/", token_match.end())
                if end < 0:
                    line_result.append(
                        _mask_non_newlines(line[token_match.start():])
                    )
                    index = len(line)
                    state = "block_comment"
                else:
                    end += 2
                    line_result.append(
                        _mask_non_newlines(line[token_match.start():end])
                    )
                    index = end
            elif token == "//":
                line_result.append(
                    _mask_non_newlines(line[token_match.start():])
                )
                index = len(line)
            else:
                end = _quoted_literal_end(line, token_match.start(), token)
                line_result.append(line[token_match.start():end])
                index = end
        result.extend(line_result)
        result.append("\n")
        search_from = line_end + 1
    return "".join(result)


def _literal_preprocessor_condition(expression):
    """Return True/False only for an unambiguous numeric #if literal."""
    normalized = expression.strip()
    while normalized.startswith("(") and normalized.endswith(")"):
        normalized = normalized[1:-1].strip()
    if re.fullmatch(r"0+[uUlL]*", normalized):
        return False
    if re.fullmatch(r"[1-9][0-9]*[uUlL]*", normalized):
        return True
    return None


def iter_active_includes(source):
    """Yield quote and angle includes outside comments and literal #if 0."""
    conditional_stack = []
    active = True
    logical_source = _splice_preprocessor_lines(source)
    sanitized_source = _strip_c_cpp_comments(logical_source)
    for directive_match in PREPROCESSOR_DIRECTIVE_PATTERN.finditer(sanitized_source):
        directive = directive_match.group(1)
        argument = directive_match.group(2)
        if directive == "if":
            condition = _literal_preprocessor_condition(argument)
            conditional_stack.append(
                {
                    "parent_active": active,
                    "known": condition is not None,
                    "branch_taken": bool(condition),
                }
            )
            active = active and (condition if condition is not None else True)
        elif directive in ("ifdef", "ifndef"):
            conditional_stack.append(
                {"parent_active": active, "known": False, "branch_taken": False}
            )
            # Unknown configuration: scan every potentially active branch.
            active = active
        elif directive == "elif" and conditional_stack:
            frame = conditional_stack[-1]
            if not frame["known"]:
                active = frame["parent_active"]
            elif frame["branch_taken"]:
                active = False
            else:
                condition = _literal_preprocessor_condition(argument)
                if condition is None:
                    frame["known"] = False
                    active = frame["parent_active"]
                else:
                    frame["branch_taken"] = bool(condition)
                    active = frame["parent_active"] and condition
        elif directive == "else" and conditional_stack:
            frame = conditional_stack[-1]
            if frame["known"]:
                active = frame["parent_active"] and not frame["branch_taken"]
                frame["branch_taken"] = True
            else:
                active = frame["parent_active"]
        elif directive == "endif" and conditional_stack:
            frame = conditional_stack.pop()
            active = frame["parent_active"]
        elif directive == "include" and active:
            include_match = INCLUDE_ARGUMENT_PATTERN.match(argument)
            if include_match is not None:
                yield include_match.group(1) or include_match.group(2)


layer_scan_roots = set()
targets = {}
domain_paths = []
allowed_domain_edges = set()
bridge_domain_edges = set()
bridge_consumer_domain_edges = set()
allowed_consumers = {}
section = None

with open(CONF) as config_file:
    for line_number, raw_line in enumerate(config_file, 1):
        line = raw_line.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("[") and line.endswith("]"):
            section = line[1:-1]
            continue
        if section is None:
            continue

        parts = line.split()
        if section == "scanned":
            layer_scan_roots.add(parts[0])
        elif section == "targets":
            if len(parts) < 2:
                raise ValueError("%s:%d: invalid [targets] entry" % (CONF, line_number))
            path = parts[0]
            layer = int(parts[1])
            include_prefix = None
            for token in parts[2:]:
                if token.startswith("@"):
                    include_prefix = token[1:]
            if include_prefix is None:
                include_prefix = _derive_include_prefix(path)
            targets[path] = (layer, include_prefix)
        elif section == "domains":
            if len(parts) != 2:
                raise ValueError(
                    "%s:%d: [domains] entry must be '<domain> <path>'"
                    % (CONF, line_number)
                )
            domain_paths.append((parts[0], parts[1]))
        elif section == "allowed_domain_edges":
            allowed_domain_edges.add(_parse_edge(parts, section, line_number))
        elif section == "bridge_domain_edges":
            bridge_domain_edges.add(_parse_edge(parts, section, line_number))
        elif section == "bridge_consumer_domain_edges":
            bridge_consumer_domain_edges.add(_parse_edge(parts, section, line_number))
        elif section == "allowed_consumers":
            consumer, target = _parse_edge(parts, section, line_number)
            allowed_consumers.setdefault(target, set()).add(consumer)

missing_targets = sorted(root for root in layer_scan_roots if root not in targets)
if missing_targets:
    raise ValueError("scanned path not in [targets]: %s" % missing_targets)

unknown_consumer_targets = sorted(set(allowed_consumers) - set(targets))
if unknown_consumer_targets:
    raise ValueError(
        "[allowed_consumers] target is not in [targets]: %s"
        % unknown_consumer_targets
    )

invalid_consumer_paths = sorted(
    consumer
    for consumers in allowed_consumers.values()
    for consumer in consumers
    if not os.path.isfile(os.path.join(REPO, consumer))
    and not os.path.isdir(os.path.join(REPO, consumer))
)
if invalid_consumer_paths:
    raise ValueError(
        "[allowed_consumers] source is not a file or directory: %s"
        % invalid_consumer_paths
    )

# A newly added first-level module must not become invisible merely because its
# author forgot to extend [scanned]. Check every C/C++ source artifact in the
# maintained architecture roots before evaluating individual include edges.
architecture_roots = ("src", "src/oblib/easy", "src/oblib")
architecture_extensions = SOURCE_EXTENSIONS + (".hxx", ".inl", ".tcc", ".def")
architecture_file_count = 0
unscanned_architecture_files = []
for architecture_root in architecture_roots:
    filesystem_root = os.path.join(REPO, architecture_root)
    if not os.path.isdir(filesystem_root):
        continue
    for directory, directory_names, file_names in os.walk(filesystem_root):
        directory_names[:] = [
            name for name in directory_names if not name.startswith(".")
        ]
        for file_name in file_names:
            if not file_name.endswith(architecture_extensions):
                continue
            architecture_file_count += 1
            relative_path = os.path.relpath(
                os.path.join(directory, file_name), REPO
            ).replace(os.sep, "/")
            if not any(
                _is_under(relative_path, scan_root)
                for scan_root in layer_scan_roots
            ):
                unscanned_architecture_files.append(relative_path)
if unscanned_architecture_files:
    raise ValueError(
        "C/C++ architecture files outside [scanned]: %s%s"
        % (
            unscanned_architecture_files[:20],
            " (and %d more)" % (len(unscanned_architecture_files) - 20)
            if len(unscanned_architecture_files) > 20
            else "",
        )
    )

domain_names = {domain for domain, _ in domain_paths}
unknown_domains = sorted(
    domain
    for edge in allowed_domain_edges | bridge_domain_edges | bridge_consumer_domain_edges
    for domain in edge
    if domain not in domain_names
)
if unknown_domains:
    raise ValueError("unknown domain in configured domain edges: %s" % unknown_domains)

overlapping_domain_edges = (
    (allowed_domain_edges & bridge_domain_edges)
    | (allowed_domain_edges & bridge_consumer_domain_edges)
    | (bridge_domain_edges & bridge_consumer_domain_edges)
)
if overlapping_domain_edges:
    raise ValueError(
        "domain edges cannot be both permanently allowed and bridge debt: %s"
        % sorted(overlapping_domain_edges)
    )

modules = [
    (os.path.join(REPO, path), path, layer, include_prefix)
    for path, (layer, include_prefix) in targets.items()
]
layers = {module[1]: module[2] for module in modules}
layers["__extsrc__"] = 99
include_map = sorted(
    ((module[3], module[1]) for module in modules), key=lambda item: -len(item[0])
)
domain_paths_by_specificity = sorted(domain_paths, key=lambda item: -len(item[1]))


def file_module(path):
    best = None
    best_length = -1
    for filesystem_path, name, _, _ in modules:
        if (path == filesystem_path or path.startswith(filesystem_path + os.sep)) and len(
            filesystem_path
        ) > best_length:
            best = name
            best_length = len(filesystem_path)
    return best


def include_module(include):
    normalized = include
    if normalized.startswith("src/oblib/"):
        normalized = normalized[len("src/oblib/") :]
    for prefix, name in include_map:
        if normalized == prefix or normalized.startswith(prefix + "/"):
            return name

    if os.path.isfile(os.path.join(REPO, "src", normalized)) or (
        normalized.startswith("src/") and os.path.isfile(os.path.join(REPO, normalized))
    ):
        return "__extsrc__"
    return None


def resolve_include_path(include, source_relative_path):
    normalized = os.path.normpath(include).replace(os.sep, "/")
    candidates = []
    if normalized.startswith(("src/", "deps/", "close_modules/")):
        candidates.append(normalized)
    else:
        candidates.extend(
            (
                os.path.normpath(os.path.join(os.path.dirname(source_relative_path), normalized)).replace(
                    os.sep, "/"
                ),
                "src/" + normalized,
                "src/oblib/" + normalized,
            )
        )

    for candidate in candidates:
        if os.path.isfile(os.path.join(REPO, candidate)):
            return candidate

    # Configured include aliases also cover optional/closed-source files that
    # are absent in a particular checkout.
    alias_normalized = normalized
    if alias_normalized.startswith("src/oblib/"):
        alias_normalized = alias_normalized[len("src/oblib/") :]
    for prefix, target_path in include_map:
        if alias_normalized == prefix:
            return target_path
        if alias_normalized.startswith(prefix + "/"):
            # Normal source roots use an include prefix that replaces their
            # physical root (storage/foo -> src/storage/foo).  API targets use
            # a custom include-root alias whose prefix remains in the physical
            # path (data_plane/foo -> src/data_plane/api/data_plane/foo).
            # Try both shapes and only then fall back for optional/closed files.
            candidates = (
                target_path + alias_normalized[len(prefix) :],
                target_path + "/" + alias_normalized,
            )
            for candidate in candidates:
                if os.path.isfile(os.path.join(REPO, candidate)):
                    return candidate
            return candidates[0]
    return None


def path_domain(path):
    if path is None:
        return None
    for domain, prefix in domain_paths_by_specificity:
        if _is_under(path, prefix):
            return domain
    return None


def include_domain(include, resolved_path):
    domain = path_domain(resolved_path)
    if domain is not None:
        return domain

    normalized = include
    if normalized.startswith("src/"):
        normalized = normalized[len("src/") :]
    for candidate_domain, path in domain_paths_by_specificity:
        include_prefix = _derive_include_prefix(path)
        if _is_under(normalized, include_prefix):
            return candidate_domain
    return None


def is_public_quarantine_api_include(include, resolved_path):
    normalized = os.path.normpath(include).replace(os.sep, "/")
    return (
        normalized == PUBLIC_QUARANTINE_API_INCLUDE
        or normalized == PUBLIC_QUARANTINE_API_PATH
        or resolved_path == PUBLIC_QUARANTINE_API_PATH
    )


def is_layer_scanned(relative_path):
    return any(_is_under(relative_path, root) for root in layer_scan_roots)


def is_allowed_consumer(source_path, source_module, target_module):
    if source_module == target_module:
        return True
    return any(
        source_path == consumer
        if os.path.isfile(os.path.join(REPO, consumer))
        else _is_under(source_path, consumer)
        for consumer in allowed_consumers[target_module]
    )


def read_baseline(path):
    baseline = set()
    if not os.path.exists(path):
        return baseline
    with open(path) as baseline_file:
        for raw_line in baseline_file:
            line = raw_line.split("#", 1)[0].strip()
            if " -> " in line:
                source, include = line.split(" -> ", 1)
                baseline.add((source.strip(), include.strip()))
    return baseline


def write_baseline(path, header, violations):
    with open(path, "w") as baseline_file:
        baseline_file.write(header + "\n")
        for key in sorted(violations):
            baseline_file.write("%s -> %s\n" % key)


def read_path_baseline(path):
    baseline = set()
    if not os.path.exists(path):
        return baseline
    with open(path) as baseline_file:
        for raw_line in baseline_file:
            line = raw_line.split("#", 1)[0].strip()
            if line:
                baseline.add(line)
    return baseline


def write_path_baseline(path, header, paths):
    with open(path, "w") as baseline_file:
        baseline_file.write(header + "\n")
        for item in sorted(paths):
            baseline_file.write(item + "\n")


def print_records(title, records, limit=30):
    if not records:
        return
    print("\n%s" % title)
    for record in records[:limit]:
        print("   %s -> \"%s\" (%s)" % (record[0], record[1], record[2]))
    if len(records) > limit:
        print("   ... and %d more" % (len(records) - limit))


def find_bridge_artifacts(roots):
    artifacts = []
    for relative_root in roots:
        filesystem_root = os.path.join(REPO, relative_root)
        if not os.path.isdir(filesystem_root):
            continue
        for root, directories, files in os.walk(filesystem_root):
            directories.sort()
            for filename in sorted(files):
                path = os.path.join(root, filename)
                if os.path.isfile(path):
                    artifacts.append(
                        os.path.relpath(path, REPO).replace(os.sep, "/")
                    )
    return artifacts


layer_violations = {}
boundary_violations = {}
bridge_dependencies = {}
bridge_consumers = {}
public_quarantine_consumers = {}
consumer_violations = {}
cross_module_includes = 0
cross_domain_includes = 0
seen_files = set()

scan_roots = set(layer_scan_roots)
scan_roots.update(path for _, path in domain_paths)

for scan_root in sorted(scan_roots):
    filesystem_root = os.path.join(REPO, scan_root)
    if not os.path.exists(filesystem_root):
        continue
    for root, _, files in os.walk(filesystem_root):
        if os.sep + "build" in root:
            continue
        for filename in files:
            if not filename.endswith(SOURCE_EXTENSIONS):
                continue
            path = os.path.join(root, filename)
            if path in seen_files:
                continue
            seen_files.add(path)

            source_relative_path = os.path.relpath(path, REPO).replace(os.sep, "/")
            source_module = file_module(path)
            source_domain = path_domain(source_relative_path)
            try:
                with open(path, errors="ignore") as source_file:
                    source = source_file.read()
            except OSError:
                continue

            for include in iter_active_includes(source):
                resolved_path = resolve_include_path(include, source_relative_path)
                target_module = (
                    file_module(os.path.join(REPO, resolved_path))
                    if resolved_path is not None
                    else include_module(include)
                )
                target_domain = include_domain(include, resolved_path)

                if (
                    source_domain == "data_plane"
                    and is_public_quarantine_api_include(include, resolved_path)
                ):
                    key = (source_relative_path, include)
                    public_quarantine_consumers[key] = (
                        "data_plane direct include of transitional public API"
                    )

                if target_module is not None and target_module != source_module:
                    cross_module_includes += 1
                    if (
                        target_module in allowed_consumers
                        and not is_allowed_consumer(
                            source_relative_path, source_module, target_module
                        )
                    ):
                        key = (source_relative_path, include)
                        consumer_violations[key] = (
                            "only declared consumers may include %s" % target_module
                        )
                    if (
                        is_layer_scanned(source_relative_path)
                        and source_module is not None
                        and layers[target_module] > layers[source_module]
                    ):
                        key = (source_relative_path, include)
                        layer_violations[key] = "%s L%d -> %s L%d" % (
                            source_module,
                            layers[source_module],
                            target_module,
                            layers[target_module],
                        )

                if (
                    source_domain is not None
                    and target_domain is not None
                    and source_domain != target_domain
                ):
                    cross_domain_includes += 1
                    domain_edge = (source_domain, target_domain)
                    key = (source_relative_path, include)
                    if domain_edge in bridge_domain_edges:
                        bridge_dependencies[key] = "bridge domain %s -> %s" % (
                            source_domain,
                            target_domain,
                        )
                    elif domain_edge in bridge_consumer_domain_edges:
                        bridge_consumers[key] = "bridge consumer %s -> %s" % (
                            source_domain,
                            target_domain,
                        )
                    elif domain_edge not in allowed_domain_edges:
                        boundary_violations[key] = "domain %s -> %s" % (
                            source_domain,
                            target_domain,
                        )

layer_baseline = read_baseline(LAYER_BASELINE)
boundary_baseline = read_baseline(BOUNDARY_BASELINE)
bridge_baseline = read_baseline(BRIDGE_BASELINE)
bridge_consumer_baseline = read_baseline(BRIDGE_CONSUMER_BASELINE)
bridge_artifact_baseline = read_path_baseline(BRIDGE_ARTIFACT_BASELINE)
public_quarantine_consumer_baseline = read_baseline(
    PUBLIC_QUARANTINE_CONSUMER_BASELINE
)
bridge_source_artifacts = set(find_bridge_artifacts(FORBIDDEN_BRIDGE_SOURCE_ROOTS))
compatibility_source_artifacts = {
    path
    for path in FORBIDDEN_COMPATIBILITY_SOURCE_ARTIFACTS
    if os.path.isfile(os.path.join(REPO, path))
}

if UPDATE_LAYER:
    write_baseline(
        LAYER_BASELINE,
        "# Exact upward-edge migration debt. Each line: <source> -> <include>",
        layer_violations,
    )
    print("layer baseline written: %d entries" % len(layer_violations))

if UPDATE_BOUNDARY:
    write_baseline(
        BOUNDARY_BASELINE,
        "# Historical cross-domain include baseline. Each line: <source> -> <include>",
        boundary_violations,
    )
    print("boundary baseline written: %d entries" % len(boundary_violations))

if UPDATE_BRIDGE:
    write_baseline(
        BRIDGE_BASELINE,
        "# Transitional API bridge debt. Each line: <source> -> <include>",
        bridge_dependencies,
    )
    print("bridge baseline written: %d entries" % len(bridge_dependencies))

if UPDATE_BRIDGE_CONSUMER:
    write_baseline(
        BRIDGE_CONSUMER_BASELINE,
        "# Transitional bridge consumers. Each line: <source> -> <include>",
        bridge_consumers,
    )
    print("bridge consumer baseline written: %d entries" % len(bridge_consumers))

if UPDATE_BRIDGE_ARTIFACT:
    write_path_baseline(
        BRIDGE_ARTIFACT_BASELINE,
        "# Transitional bridge artifacts. Each line is a repository-relative path.",
        bridge_source_artifacts,
    )
    print("bridge artifact baseline written: %d entries" % len(bridge_source_artifacts))

if UPDATE_PUBLIC_QUARANTINE_CONSUMER:
    write_baseline(
        PUBLIC_QUARANTINE_CONSUMER_BASELINE,
        "# Direct data-plane consumers of the transitional public aggregate runtime. "
        "Each line: <source> -> <include>",
        public_quarantine_consumers,
    )
    print(
        "public quarantine consumer baseline written: %d entries"
        % len(public_quarantine_consumers)
    )

if (UPDATE_LAYER or UPDATE_BOUNDARY or UPDATE_BRIDGE
        or UPDATE_BRIDGE_CONSUMER or UPDATE_BRIDGE_ARTIFACT
        or UPDATE_PUBLIC_QUARANTINE_CONSUMER):
    sys.exit(0)

layer_current = set(layer_violations)
boundary_current = set(boundary_violations)
bridge_current = set(bridge_dependencies)
bridge_consumer_current = set(bridge_consumers)
public_quarantine_consumer_current = set(public_quarantine_consumers)
new_layer = sorted(layer_current - layer_baseline)
new_boundary = sorted(boundary_current - boundary_baseline)
new_bridge = sorted(bridge_current - bridge_baseline)
new_bridge_consumer = sorted(bridge_consumer_current - bridge_consumer_baseline)
new_bridge_artifact = sorted(bridge_source_artifacts - bridge_artifact_baseline)
new_public_quarantine_consumer = sorted(
    public_quarantine_consumer_current - public_quarantine_consumer_baseline
)
stale_layer = sorted(layer_baseline - layer_current)
stale_boundary = sorted(boundary_baseline - boundary_current)
stale_bridge = sorted(bridge_baseline - bridge_current)
stale_bridge_consumer = sorted(bridge_consumer_baseline - bridge_consumer_current)
stale_bridge_artifact = sorted(bridge_artifact_baseline - bridge_source_artifacts)
stale_public_quarantine_consumer = sorted(
    public_quarantine_consumer_baseline - public_quarantine_consumer_current
)
residual_layer = sorted(layer_current & layer_baseline)
residual_boundary = sorted(boundary_current & boundary_baseline)
residual_bridge = sorted(bridge_current & bridge_baseline)
residual_bridge_consumer = sorted(bridge_consumer_current & bridge_consumer_baseline)
residual_bridge_artifact = sorted(bridge_source_artifacts & bridge_artifact_baseline)
residual_public_quarantine_consumer = sorted(
    public_quarantine_consumer_current & public_quarantine_consumer_baseline
)

print(
    "module-layer check: cross-module includes %d, violations %d "
    "(baseline %d, new %d)"
    % (cross_module_includes, len(layer_current), len(layer_baseline), len(new_layer))
)
print(
    "architecture coverage check: C/C++ files %d, unscanned %d"
    % (architecture_file_count, len(unscanned_architecture_files))
)
print(
    "domain-boundary check: cross-domain includes %d, violations %d "
    "(baseline %d, new %d)"
    % (cross_domain_includes, len(boundary_current), len(boundary_baseline), len(new_boundary))
)
print(
    "api-bridge check: implementation includes %d (baseline %d, new %d)"
    % (len(bridge_current), len(bridge_baseline), len(new_bridge))
)
print(
    "api-bridge consumer check: bridge includes %d (baseline %d, new %d)"
    % (
        len(bridge_consumer_current),
        len(bridge_consumer_baseline),
        len(new_bridge_consumer),
    )
)
print(
    "api-bridge artifact check: files %d (baseline %d, new %d)"
    % (len(bridge_source_artifacts), len(bridge_artifact_baseline),
       len(new_bridge_artifact))
)
print(
    "compatibility escape-hatch check: forbidden files %d"
    % len(compatibility_source_artifacts)
)
print(
    "protected-module consumer check: violations %d"
    % len(consumer_violations)
)
print(
    "public-quarantine consumer check: direct includes %d "
    "(baseline %d, residual %d, new %d, stale %d)"
    % (
        len(public_quarantine_consumer_current),
        len(public_quarantine_consumer_baseline),
        len(residual_public_quarantine_consumer),
        len(new_public_quarantine_consumer),
        len(stale_public_quarantine_consumer),
    )
)
print(
    "domains: %s"
    % ", ".join(
        "%s=%s" % (domain, path) for domain, path in sorted(domain_paths)
    )
)

failed = False

if new_layer:
    failed = True
    print_records(
        "[FAIL] new upward-edge includes:",
        [(source, include, layer_violations[(source, include)]) for source, include in new_layer],
    )

if new_boundary:
    failed = True
    print_records(
        "[FAIL] new forbidden cross-domain includes:",
        [
            (source, include, boundary_violations[(source, include)])
            for source, include in new_boundary
        ],
    )

if new_bridge:
    failed = True
    print_records(
        "[FAIL] new API bridge implementation dependencies:",
        [
            (source, include, bridge_dependencies[(source, include)])
            for source, include in new_bridge
        ],
    )

if new_bridge_consumer:
    failed = True
    print_records(
        "[FAIL] new API bridge consumers:",
        [
            (source, include, bridge_consumers[(source, include)])
            for source, include in new_bridge_consumer
        ],
    )

if new_bridge_artifact:
    failed = True
    print("\n[FAIL] new artifacts under migration-only bridge roots:")
    for path in new_bridge_artifact[:30]:
        print("   %s" % path)
    if len(new_bridge_artifact) > 30:
        print("   ... and %d more" % (len(new_bridge_artifact) - 30))

if compatibility_source_artifacts:
    failed = True
    print("\n[FAIL] forbidden compatibility escape-hatch artifacts:")
    for path in sorted(compatibility_source_artifacts):
        print("   %s" % path)

if consumer_violations:
    failed = True
    print_records(
        "[FAIL] undeclared protected-module consumers:",
        [
            (source, include, consumer_violations[(source, include)])
            for source, include in sorted(consumer_violations)
        ],
    )

if new_public_quarantine_consumer:
    failed = True
    print_records(
        "[FAIL] new direct consumers of the transitional public aggregate runtime:",
        [
            (source, include, public_quarantine_consumers[(source, include)])
            for source, include in new_public_quarantine_consumer
        ],
    )

if stale_layer:
    failed = True
    print_records(
        "[FAIL] layer baseline has eliminated entries; run --update-baseline to tighten:",
        [(source, include, "stale layer baseline") for source, include in stale_layer],
        limit=10,
    )

# Boundary debt is an exact ratchet.  Stale entries fail the normal build so an
# eliminated SQL/data-plane implementation dependency cannot be reintroduced.
if stale_boundary:
    failed = True
    print_records(
        "[FAIL] boundary baseline has eliminated entries; run --update-boundary-baseline to tighten:",
        [(source, include, "stale boundary baseline") for source, include in stale_boundary],
        limit=10,
    )

# Bridge debt uses the same exact-ratchet behavior: once a broad compatibility
# dependency is removed it cannot silently return.
if stale_bridge:
    failed = True
    print_records(
        "[FAIL] bridge baseline has eliminated entries; run --update-bridge-baseline to tighten:",
        [(source, include, "stale bridge baseline") for source, include in stale_bridge],
        limit=10,
    )

if stale_bridge_consumer:
    failed = True
    print_records(
        "[FAIL] bridge consumer baseline has eliminated entries; "
        "run --update-bridge-consumer-baseline to tighten:",
        [
            (source, include, "stale bridge consumer baseline")
            for source, include in stale_bridge_consumer
        ],
        limit=10,
    )

if stale_bridge_artifact:
    failed = True
    print("\n[FAIL] bridge artifact baseline has eliminated entries; "
          "run --update-bridge-artifact-baseline to tighten:")
    for path in stale_bridge_artifact[:10]:
        print("   %s" % path)
    if len(stale_bridge_artifact) > 10:
        print("   ... and %d more" % (len(stale_bridge_artifact) - 10))

if stale_public_quarantine_consumer:
    failed = True
    print_records(
        "[FAIL] public quarantine consumer baseline has eliminated entries; "
        "run --update-public-quarantine-consumer-baseline to tighten:",
        [
            (source, include, "stale public quarantine consumer baseline")
            for source, include in stale_public_quarantine_consumer
        ],
        limit=10,
    )

if STRICT_LAYER and residual_layer:
    failed = True
    print("\n[STRICT FAIL] %d historical layer violations remain" % len(residual_layer))

if STRICT_BOUNDARY and residual_boundary:
    failed = True
    print("\n[STRICT BOUNDARY FAIL] %d cross-domain violations remain" % len(residual_boundary))

if STRICT_BRIDGE and (residual_bridge or residual_bridge_consumer):
    failed = True
    print(
        "\n[STRICT BRIDGE FAIL] %d implementation dependencies and %d bridge consumers "
        "remain quarantined"
        % (len(residual_bridge), len(residual_bridge_consumer))
    )

if STRICT_BRIDGE and residual_bridge_artifact:
    failed = True
    print(
        "\n[STRICT BRIDGE ARTIFACT FAIL] %d files remain under "
        "forbidden bridge roots" % len(residual_bridge_artifact)
    )
    for path in residual_bridge_artifact[:30]:
        print("   %s" % path)
    if len(residual_bridge_artifact) > 30:
        print("   ... and %d more" % (len(residual_bridge_artifact) - 30))

if failed:
    print(
        "\nfixes: move shared vocabulary into a neutral API, depend on an API facade, "
        "move misplaced behavior to its owner, or tighten the relevant baseline"
    )
    sys.exit(1)

print(
    "[OK] no new layer, domain-boundary, protected-consumer, API-bridge, "
    "bridge-consumer, bridge-artifact, or public-quarantine-consumer violations"
)
