"""Bazel analysis enforcement for the authoritative module policy."""

load(
    ":module_policy.bzl",
    "ALLOWED_MODULE_DEPS",
    "MODULE_ROOTS",
    "UNITTEST_ALLOWED_DIRECT_MODULE_DEPS",
    "UNITTEST_MODULE_ROOTS",
    "UNITTEST_RUNTIME_DEPS",
)

def _validate_policy():
    modules = sorted(MODULE_ROOTS.keys())
    policy_modules = sorted(ALLOWED_MODULE_DEPS.keys())
    if modules != policy_modules:
        fail(
            "module policy keys must exactly match module roots: roots=%s policy=%s" %
            (modules, policy_modules),
        )

    for consumer, producers in ALLOWED_MODULE_DEPS.items():
        for producer in producers:
            if producer not in MODULE_ROOTS:
                fail("%s depends on unknown module %s" % (consumer, producer))
            if producer == consumer:
                fail("same-module dependencies are implicit; remove %s -> %s" % (consumer, producer))

    # Resolve producer leaves first.  A complete pass without progress means
    # the remaining policy nodes form one or more dependency cycles.
    resolved = {}
    for _ in range(len(modules)):
        made_progress = False
        for consumer in modules:
            if consumer in resolved:
                continue
            ready = True
            for producer in ALLOWED_MODULE_DEPS[consumer]:
                if producer not in resolved:
                    ready = False
            if ready:
                resolved[consumer] = True
                made_progress = True
        if not made_progress:
            break
    if len(resolved) != len(modules):
        unresolved_edges = []
        for consumer in modules:
            if consumer not in resolved:
                for producer in ALLOWED_MODULE_DEPS[consumer]:
                    if producer not in resolved:
                        unresolved_edges.append("%s -> %s" % (consumer, producer))
        fail(
            "module policy must be acyclic; unresolved cycle edges: %s" %
            ", ".join(unresolved_edges),
        )

    unittest_modules = sorted(UNITTEST_MODULE_ROOTS.keys())
    unittest_policy_modules = sorted(UNITTEST_ALLOWED_DIRECT_MODULE_DEPS.keys())
    if unittest_modules != unittest_policy_modules:
        fail(
            "unit-test policy keys must exactly match unit-test roots: roots=%s policy=%s" %
            (unittest_modules, unittest_policy_modules),
        )

    for consumer, producers in UNITTEST_ALLOWED_DIRECT_MODULE_DEPS.items():
        for producer in producers:
            if producer not in MODULE_ROOTS:
                fail("%s unit tests depend on unknown module %s" % (consumer, producer))
            if producer == consumer:
                fail(
                    "the module under test is implicit; remove %s -> %s from the unit-test policy" %
                    (consumer, producer),
                )

_POLICY_VALIDATED = _validate_policy()

def _is_main_repo(label):
    canonical = str(label)
    return canonical.startswith("//") or canonical.startswith("@@//")

def _main_repo_label(label):
    canonical = str(label)
    return canonical[2:] if canonical.startswith("@@//") else canonical

def _is_managed_package(package):
    return package == "src" or package.startswith("src/")

def _module_for_roots(label, roots):
    if not _is_main_repo(label):
        return None

    package = label.package
    matched_module = None
    matched_root_length = -1
    for module, root in roots.items():
        if package == root or package.startswith(root + "/"):
            if len(root) > matched_root_length:
                matched_module = module
                matched_root_length = len(root)
    return matched_module

def _module_for_label(label):
    return _module_for_roots(label, MODULE_ROOTS)

def _unittest_module_for_label(label):
    return _module_for_roots(label, UNITTEST_MODULE_ROOTS)

def _is_unittest_package(package):
    return package == "unittest" or package.startswith("unittest/")

def _targets_in_attribute(value):
    if hasattr(value, "label"):
        return [value]

    result = []
    value_type = type(value)
    if value_type == "list" or value_type == "tuple":
        for item in value:
            if hasattr(item, "label"):
                result.append(item)
    elif value_type == "dict":
        for key, item in value.items():
            if hasattr(key, "label"):
                result.append(key)
            if hasattr(item, "label"):
                result.append(item)
    return result

def _check_dependency(consumer_label, producer_label, attribute):
    unittest_consumer = _unittest_module_for_label(consumer_label)
    if unittest_consumer != None:
        if _main_repo_label(producer_label) in UNITTEST_RUNTIME_DEPS:
            return

        producer = _module_for_label(producer_label)
        if producer == None:
            producer = _unittest_module_for_label(producer_label)
        if producer == None or producer == unittest_consumer:
            return
        if producer in UNITTEST_ALLOWED_DIRECT_MODULE_DEPS[unittest_consumer]:
            return

        fail("\n".join([
            "unit-test module dependency violation:",
            "  %s [tests %s]" % (consumer_label, unittest_consumer),
            "    --%s--> %s [%s]" % (attribute, producer_label, producer),
            "  a unit test may directly name only its module under test and: %s" % (
                ", ".join(UNITTEST_ALLOWED_DIRECT_MODULE_DEPS[unittest_consumer]) or "(none)",
            ),
            "  move the test to its owner, use a module-local fixture/adapter, or remove it",
        ]))

    consumer = _module_for_label(consumer_label)
    producer = _module_for_label(producer_label)

    if consumer == None or producer == None or consumer == producer:
        return
    if producer in ALLOWED_MODULE_DEPS[consumer]:
        return

    fail("\n".join([
        "module dependency violation:",
        "  %s [%s]" % (consumer_label, consumer),
        "    --%s--> %s [%s]" % (attribute, producer_label, producer),
        "  allowed dependencies of %s: %s" % (
            consumer,
            ", ".join(ALLOWED_MODULE_DEPS[consumer]) or "(none)",
        ),
        "  change //bazel/architecture:module_policy.bzl to authorize a new module edge",
    ]))

def _module_dependency_aspect_impl(target, ctx):
    if not _is_main_repo(target.label):
        return []

    package = target.label.package
    module = _module_for_label(target.label)
    unittest_module = _unittest_module_for_label(target.label)
    if _is_managed_package(package) and module == None:
        fail(
            "%s is under the managed source tree but has no entry in MODULE_ROOTS" %
            target.label,
        )
    if _is_unittest_package(package) and package != "unittest" and unittest_module == None:
        fail(
            (
                "%s is under //unittest but has no owner in UNITTEST_MODULE_ROOTS; " +
                "move or delete it before adding it to the Bazel graph"
            ) % target.label,
        )

    if ctx.rule == None:
        return []

    if (
        _is_managed_package(package) and
        ctx.rule.kind == "cc_library" and
        not ctx.rule.attr.linkstatic
    ):
        fail("\n".join([
            "first-party cc_library must be static-only: %s" % target.label,
            "  load //bazel:defs.bzl%seekdb_cc_library",
            "  shared-library products must use an explicit cc_shared_library",
        ]))

    for attribute in dir(ctx.rule.attr):
        value = getattr(ctx.rule.attr, attribute)
        for dependency in _targets_in_attribute(value):
            _check_dependency(target.label, dependency.label, attribute)
    return []

module_dependency_aspect = aspect(
    implementation = _module_dependency_aspect_impl,
    # The graph contains custom link rules and generated Unity carriers in
    # addition to cc_library.  Propagating across every label attribute keeps
    # those paths subject to the same central policy.
    attr_aspects = ["*"],
)
