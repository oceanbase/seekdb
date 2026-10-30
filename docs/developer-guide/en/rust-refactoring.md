# Rust refactoring guidelines

These guidelines apply to individual Rust migration and refactoring tasks, including related C++ callers, CXX bridges, and build integration. Aim for clear responsibilities, safe resource lifetimes, centralized platform adaptation, and lower maintenance costs. Treat performance as a regression constraint. Do not assume that a rewrite makes code faster.

## 1. Keep dependencies directed from C++ to Rust by default

Keep Rust core modules independent and concentrate cross-language adaptation at their boundaries. Prefer explicit inputs, returned results, and orchestration by callers to connect the two languages.

Use cross-language callbacks or interface injection when needed. Do not add queues, event protocols, or extra threads solely to avoid callbacks. Define ownership and lifetimes for arguments and callback objects, execution threads, reentrancy, lock constraints, error handling, and cancellation and shutdown behavior. For temporary compatibility dependencies, also identify remaining callers and removal conditions.

## 2. Design native interfaces for Rust callers and use CXX for compatibility

Design natural Rust types, ownership, and error models first. Then adapt existing C++ types, error codes, and calling conventions. CXX representation constraints should not determine the core interface. Rust callers must be able to use the full capability through native interfaces without constructing bridge objects or following C++ compatibility conventions.

Use the repository's existing CXX bridges and build integration for interoperability in both directions. Keep bridge code with its owning module. Bridges handle representation conversion and compatibility. Native implementations provide the core behavior.

## 3. Pass business state explicitly and allow appropriate infrastructure access

Give context that affects business correctness an explicit owner and pass it through parameters or handles. Each module receives only the state and capabilities it needs. Avoid hiding dependencies in an all-purpose context object.

A global entry point can be appropriate for cross-cutting infrastructure with process-wide semantics that needs no isolation per request or instance, such as a logger. Still define state ownership, initialization, shutdown, concurrency safety, and test isolation. Give thread-local state an explicit thread scope. Across threads or asynchronous tasks, propagate required context explicitly rather than relying on state that happens to exist on the executing thread.

## 4. Give each piece of mutable state one authoritative owner

When migrating a responsibility, identify the owners of its state, resources, and invariants. Callers may hold handles, snapshots, or caches, but must not maintain a second authoritative copy that requires synchronization. Define versioning or invalidation rules for snapshots and caches.

Define when updates take effect and what state remains after failure. Apply configuration or state changes that require a consistent commit as a whole. Do not report success after a partial update.

## 5. Migrate complete responsibilities and preserve module cohesion

Check whether adjacent state management, validation, resource management, error handling, and platform adaptation should move into Rust together. Avoid moving only low-level functions while leaving most complexity in C++. Choose scope by responsibility and dependencies rather than migrated line count.

Organize modules around state ownership, invariants, and reasons to change together. Do not mechanically reproduce C++ classes, files, or inheritance hierarchies. Hide internal coordination behind small, complete interfaces. Lower layers accept the policy and data they need. Upper layers retain their domain decisions.

Reuse the standard library and existing modules. Add a trait, wrapper type, or intermediate layer only to meet an actual semantic, ownership, or variation requirement.

## 6. Redesign around behavior contracts

Before choosing data structures or execution mechanisms, identify the behavior callers need and the invariants the implementation must preserve. Redesign internals when useful rather than translating line by line.

For each task, define compatibility requirements for external behavior, data formats, durability, and recovery. When fixing an existing defect, changing behavior, or limiting support in an intermediate delivery, state the change, its impact, and its acceptance criteria.

## 7. Express valid states through types and ownership

Use domain types, enums, and handles with explicit lifetimes to express important states. Reduce invalid combinations and implicit call ordering. Parse and validate external input at boundaries. Use types that carry established constraints in internal interfaces.

Keep resources alive until their actual users finish. Logical completion, cancellation, or timeout does not imply that background work has stopped. Define when borrows end and ownership transfers across asynchronous boundaries. Shared reference counting does not replace these contracts.

Confine necessary `unsafe` code to small, reviewable areas. Explain its safety invariants and how the implementation maintains them. Define error and panic handling at cross-language boundaries. Do not let unwinding cross those boundaries.

## 8. Define concurrency protocols before choosing synchronization mechanisms

Define who can mutate state, when an operation is accepted, how results are published, and how cancellation and shutdown finish. Reduce shared mutable state before choosing locks, atomics, or message passing. Different modules may use different concurrency models.

When introducing queues or background work, define capacity, backpressure, and resource limits. Error, cancellation, and shutdown paths must make progress and reclaim resources without depending on a continued stream of normal requests.

## 9. Switch production callers and remove replaced implementations

Identify the production call paths to switch within the task and the conditions for deleting old code. After switching callers, remove replaced implementations, duplicate state, obsolete interfaces, and ineffective configuration.

An implementation that has not reached a production path is an intermediate deliverable. Define the scope and remaining callers of retained compatibility layers. Give temporary compatibility code explicit removal conditions. Maintain necessary long-term interfaces through clear contracts. An adapter or callback does not by itself make a design temporary.

## 10. Validate behavior according to risk

Use the repository's established test directories and test suites. Select native interface tests, cross-language integration tests, and production-path validation according to the change's risk. Assert observable behavior rather than implementation details.

Cover applicable behavior such as error propagation, partial success, resource exhaustion, cancellation, shutdown, and recovery. Concentrate platform differences in lower-level adapters. Distinguish unsupported capabilities from actual failures. Compilation alone does not prove that a migration is complete.

For performance-sensitive paths, compare the baseline and new implementation with equivalent semantics and workloads. Report the results, environment, and coverage gaps. Do not hide regressions by reducing functionality.

## Task deliverables

- Describe the migrated responsibility, module boundaries, and usage by native Rust callers.
- Identify state and resource owners and applicable failure, cancellation, and shutdown contracts.
- List switched production call paths, deleted implementations, and retained compatibility interfaces.
- Report validation results and unverified areas. Identify remaining work for intermediate deliverables and removal conditions for temporary compatibility code.

When departing from a default principle, explain the specific constraint, impact, and scope. Do not turn an exception into the default for unrelated modules.
