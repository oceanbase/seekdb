/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_PLUGIN_SERVER_DEV_PLANNER_H_
#define SEEKDB_PLUGIN_SERVER_DEV_PLANNER_H_
#include "seekdb/plugin/seekdb_plugin_abi.h"
#ifdef __cplusplus
extern "C" {
#endif

/* Version-bound candidate selection, NOT part of the long-lived Public SPI.
 * Only implementations owned by a admitted Server-dev module may run here.
 * Each invocation covers one equivalent candidate group supplied by the host;
 * choosing an index does not construct a new path or extend physical operators.
 * All pointers/callbacks are synchronous, same-thread, invocation-borrowed.
 * No candidate plan or C++ class layout crosses this bridge. */
#define SEEKDB_PLUGIN_CANDIDATE_HOOK_POINT "optimizer.candidate.select.v1"
/* Additive relation planning, after join/access paths become logical trees and
 * before upper operators (aggregation, ORDER BY, LIMIT) are allocated. Uses the
 * same context/service with spi_minor >= 1 and AROUND mode only. build appends
 * equivalent alternatives; select is forbidden (sticky INVALID_ARGUMENT).
 * next runs later contributors, not a cheapest-path selection. Success retains
 * ALL original and constructed candidates for normal upper planning/costing.
 * No parent links are committed while alternatives share input subtrees; the
 * host fixes the chosen final tree in its normal post-plan processing.
 * This is a query-block relation stage, not every join enumeration subproblem
 * or arbitrary subtree/upper-path callback. Existing selection hooks are NOT
 * invoked here implicitly. All existing graph/builder lifetimes still apply. */
#define SEEKDB_PLUGIN_RELATION_PATHS_HOOK_POINT "optimizer.relation.paths.v1"
/* Additive JOIN-enumeration subproblem stage. Each invocation contains new
 * native alternatives for one relation; subsequent hooks also see additions.
 * Requires builders and AROUND, exactly one next, no select. Query targets and
 * resolved columns retain their query-block scope: use dependency_scope to
 * distinguish a subproblem's values; unknown is not equivalent to outside.
 * This registration is independent of relation.paths and candidate.select. */
#define SEEKDB_PLUGIN_JOIN_PATHS_HOOK_POINT "optimizer.join.paths.v1"
/* Upper stages run after the named operation's native alternatives exist,
 * before subsequent upper operations. They use the same additive contract,
 * not candidate selection. ORDERED contributions must preserve target order.
 * This opens contribution points; individual builders/metadata still define
 * which algorithms can be expressed. No arbitrary aggregate/window rewrite
 * or distributed execution is implied by registering a hook. */
#define SEEKDB_PLUGIN_GROUP_PATHS_HOOK_POINT "optimizer.upper.group.paths.v1"
#define SEEKDB_PLUGIN_WINDOW_PATHS_HOOK_POINT "optimizer.upper.window.paths.v1"
#define SEEKDB_PLUGIN_DISTINCT_PATHS_HOOK_POINT "optimizer.upper.distinct.paths.v1"
#define SEEKDB_PLUGIN_ORDERED_PATHS_HOOK_POINT "optimizer.upper.ordered.paths.v1"
/* Host routing, not a new field in the invocation-borrowed context. A service
 * registered at several stages uses separate registrations/entrypoints when
 * it needs stage-specific behavior. Unknown phases never alias selection. */
typedef enum seekdb_plugin_candidate_phase {
  SEEKDB_PLUGIN_PHASE_SELECT = 0,
  SEEKDB_PLUGIN_PHASE_RELATION = 1,
  SEEKDB_PLUGIN_PHASE_JOIN = 2,
  SEEKDB_PLUGIN_PHASE_GROUP = 3,
  SEEKDB_PLUGIN_PHASE_WINDOW = 4,
  SEEKDB_PLUGIN_PHASE_DISTINCT = 5,
  SEEKDB_PLUGIN_PHASE_ORDERED = 6
} seekdb_plugin_candidate_phase_t;
static inline const char *seekdb_plugin_candidate_hook_point(seekdb_plugin_candidate_phase_t phase)
{
  switch (phase) {
    case SEEKDB_PLUGIN_PHASE_SELECT: return SEEKDB_PLUGIN_CANDIDATE_HOOK_POINT;
    case SEEKDB_PLUGIN_PHASE_RELATION: return SEEKDB_PLUGIN_RELATION_PATHS_HOOK_POINT;
    case SEEKDB_PLUGIN_PHASE_JOIN: return SEEKDB_PLUGIN_JOIN_PATHS_HOOK_POINT;
    case SEEKDB_PLUGIN_PHASE_GROUP: return SEEKDB_PLUGIN_GROUP_PATHS_HOOK_POINT;
    case SEEKDB_PLUGIN_PHASE_WINDOW: return SEEKDB_PLUGIN_WINDOW_PATHS_HOOK_POINT;
    case SEEKDB_PLUGIN_PHASE_DISTINCT: return SEEKDB_PLUGIN_DISTINCT_PATHS_HOOK_POINT;
    case SEEKDB_PLUGIN_PHASE_ORDERED: return SEEKDB_PLUGIN_ORDERED_PATHS_HOOK_POINT;
    default: return NULL;
  }
}
#define SEEKDB_PLUGIN_CANDIDATE_AROUND 1u
#define SEEKDB_PLUGIN_CANDIDATE_REPLACE 2u
typedef struct seekdb_plugin_candidate_info_v1 {
  uint32_t struct_size;
  uint32_t operator_type; /* host-build-specific log_op_def value */
  double cost;
  double rows;
  double width;
  uint64_t reserved[4];
} seekdb_plugin_candidate_info_v1_t;
typedef struct seekdb_plugin_candidate_context_v1 {
  uint32_t struct_size;
  uint32_t candidate_count;
  void *host_context;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *get)(void *, uint32_t,
      seekdb_plugin_candidate_info_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *select)(void *, uint32_t);
  void *continuation;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *next)(void *, int32_t *database_error);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v1_t;
/* spi_minor=1 adds host-owned path construction. Indices never move during an
 * invocation; current_count includes paths added by earlier/downstream hooks.
 * At most 64 new paths per invocation. New kinds extend the builder protocol;
 * kind 1 currently materializes one existing candidate using the host's own
 * factory, property derivation and cost model, not plugin-supplied fake costs.
 * Construction does not select the path. No partial index is returned on error.
 * The host keeps a sticky database error; get_error exposes its exact value. */
#define SEEKDB_PLUGIN_PATH_MATERIALIZE 1u
#define SEEKDB_PLUGIN_PATH_CUSTOM 2u
#define SEEKDB_PLUGIN_PATH_PRESERVES_ORDER 1u
#define SEEKDB_PLUGIN_PATH_BLOCKING 2u
typedef struct seekdb_plugin_path_request_v1 {
  uint32_t struct_size;
  uint32_t kind;
  uint32_t input_index;
  uint32_t reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_path_request_v1_t;
/* Custom unary equivalent path: inherits SQL column identities and relational
 * semantics from its input. Plugin supplies its own execution service, owned
 * plan bytes and per-worker operator cost. It may not silently change the
 * result relation. Only declare PRESERVES_ORDER when the algorithm preserves
 * input order. Host copies all borrowed fields before build returns.
 * This version-bound extension is submitted through the existing build slot;
 * older hosts reject its kind/size rather than treating it as MATERIAL. */
typedef struct seekdb_plugin_custom_path_request_v1 {
  seekdb_plugin_path_request_v1_t v1;
  const char *service_id;
  uint32_t service_major;
  uint32_t minimum_minor;
  const uint8_t *plan;
  uint32_t plan_size;
  uint32_t flags;
  double operator_cost;
  uint64_t reserved[4];
} seekdb_plugin_custom_path_request_v1_t;
/* Explicit unary dataflow. IDs come from this invocation's v3 graph. Inputs
 * are required child-side expressions in wire order; outputs are expressions
 * produced by the plugin in wire order, not the final SELECT-list order.
 * Outputs may be computed by the plugin rather than copied from inputs.
 * Each output identity must be unique (one SQL slot); input repetition is legal.
 * Empty arrays mean zero columns, never "inherit". Each count is <=1024.
 * The host owns/copies the resolved expressions before returning; graph IDs
 * are not retained in executable plans. SQL relation/value semantics remain
 * the plugin author's responsibility. Normal allocation/pruning must preserve
 * input dependencies and cannot fall back to stale child slots for outputs.
 * Submit with v1.v1.struct_size=sizeof(this) through v3's inherited build slot. */
typedef struct seekdb_plugin_custom_path_request_v2 {
  seekdb_plugin_custom_path_request_v1_t v1;
  const uint32_t *inputs;
  uint32_t input_count;
  uint32_t output_count;
  const uint32_t *outputs;
  uint64_t reserved[4];
} seekdb_plugin_custom_path_request_v2_t;
/* Explicit fragment replacement. v2.v1.v1.input_index identifies the equivalent
 * result candidate, NOT an input stream. input_plans are invocation-local v3
 * PlanIds in that candidate's subtree; distinct, non-overlapping subtrees become
 * the actual children. v2.inputs is partitioned by plan_count+1 offsets, each
 * segment <=1024, with an independent wire order. Zero plans uses offsets[0]=0.
 * The plugin implements the target relation and all declared output values.
 * Logical properties belong to that target, never to the first input.
 * Execution placement is explicit: LOCAL_SERIAL currently requires the target
 * and every input to execute locally with DOP 1. Distributed/PX replacement
 * needs a separate exchange/placement contract, not implicit first-child state.
 * PRESERVES_ORDER refers to the target candidate's ordering in this request.
 * All fields are copied/resolved before returning. No retained graph IDs.
 * This is version-bound; submit its exact size, older hosts reject it. */
#define SEEKDB_PLUGIN_CUSTOM_LOCAL_SERIAL 1u
typedef struct seekdb_plugin_custom_path_request_v3 {
  seekdb_plugin_custom_path_request_v2_t v2;
  const uint32_t *input_plans;
  uint32_t plan_count;
  uint32_t execution;
  const uint32_t *input_offsets;
  uint64_t reserved[4];
} seekdb_plugin_custom_path_request_v3_t;
/* Transfer the target's execution-parameter ownership to a custom fragment.
 * parameter is a v6 graph expression ID; source_input/source_column address a
 * declared input slot, whose expression must equal the parameter's source.
 * target_input is the child to execute after binding. The host accepts COMPLETE
 * NestedLoop lists of removed JOIN owners. Each owner's whole right subtree
 * must be retained as its target input; a source input must lie in that owner's
 * left subtree. Multiple removed JOINs, reordered inputs and fan-in are allowed.
 * Each parameter has one owner and target; the input graph must be acyclic.
 * Above-pushdown, split consumer trees and other removed owner kinds require
 * additional ownership protocols. Retained subtrees keep their native owners.
 * No graph IDs survive planning. The host snapshots source values, invalidates
 * the dependent input on source advance, and clears owned parameters on reset.
 * Use execution bind_rescan_input, not independent rescan_input, to publish a
 * new environment. Failed binding/reset poisons the cursor; no partial reads. */
typedef struct seekdb_plugin_input_binding_v1 {
  uint32_t parameter;
  uint32_t source_input;
  uint32_t source_column;
  uint32_t target_input;
} seekdb_plugin_input_binding_v1_t;
typedef struct seekdb_plugin_custom_path_request_v4 {
  seekdb_plugin_custom_path_request_v3_t v3;
  const seekdb_plugin_input_binding_v1_t *bindings;
  uint32_t binding_count;
  uint32_t reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_custom_path_request_v4_t;
typedef struct seekdb_plugin_candidate_context_v2 {
  seekdb_plugin_candidate_context_v1_t v1;
  uint32_t (SEEKDB_PLUGIN_CALL *current_count)(void *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *build)(void *,
      const seekdb_plugin_path_request_v1_t *, uint32_t *index);
  int32_t (SEEKDB_PLUGIN_CALL *get_error)(void *);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v2_t;
/* spi_minor=2: read-only planning graph, before expression allocation. Handles
 * are invocation-local identities, NOT candidate indices, SQL object IDs or
 * serializable plan bindings. The same node/expression has the same handle
 * throughout this invocation, including after build/next. UINT32_MAX is invalid.
 * Only already-present fields are inspected; no expression allocation occurs.
 * Expression roles below are NOT a complete output schema/dependency list.
 * Host errors remain sticky through v2.get_error. All outputs are copied. */
#define SEEKDB_PLUGIN_PLAN_FILTER 1u
#define SEEKDB_PLUGIN_PLAN_STARTUP 2u
#define SEEKDB_PLUGIN_PLAN_ORDERING 3u
#define SEEKDB_PLUGIN_PLAN_JOIN_CONDITION 4u
#define SEEKDB_PLUGIN_PLAN_JOIN_FILTER 5u
#define SEEKDB_PLUGIN_EXPR_PLUGIN_TYPE 1u
#define SEEKDB_PLUGIN_EXPR_STORED 2u
#define SEEKDB_PLUGIN_EXPR_COLUMN 4u
#define SEEKDB_PLUGIN_EXPR_CONSTANT 8u
#define SEEKDB_PLUGIN_EXPR_NOT_NULL 16u
typedef struct seekdb_plugin_plan_info_v1 {
  uint32_t struct_size, operator_type, child_count, join_type;
  uint32_t expression_counts[5]; /* indexed by role-1 */
  uint32_t reserved_word;
  double cost, rows, width;
  uint64_t reserved[4];
} seekdb_plugin_plan_info_v1_t;
typedef struct seekdb_plugin_expr_info_v1 {
  uint32_t struct_size, expression_type, sql_type, flags;
  uint32_t argument_count;
  int32_t collation, precision, scale;
  uint64_t table_id, column_id; /* query-local references, only with COLUMN */
  char type_id[SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES + 1]; /* empty without PLUGIN_TYPE */
  uint64_t reserved[4];
} seekdb_plugin_expr_info_v1_t;
typedef struct seekdb_plugin_candidate_context_v3 {
  seekdb_plugin_candidate_context_v2_t v2;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *root)(void *, uint32_t candidate, uint32_t *handle);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *plan)(void *, uint32_t handle, seekdb_plugin_plan_info_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *child)(void *, uint32_t handle, uint32_t index, uint32_t *child);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *expression)(void *, uint32_t handle, uint32_t role,
      uint32_t index, uint32_t *expression, uint32_t *ordering); /* ordering only for ORDERING */
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *describe_expression)(void *, uint32_t expression,
      seekdb_plugin_expr_info_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *argument)(void *, uint32_t expression, uint32_t index, uint32_t *argument);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v3_t;
/* spi_minor=3: complete logical SELECT list of this plan's query block, after
 * rewrite and before final expression allocation. This is NOT the output of
 * every candidate/subtree and does not authorize arbitrary relation changes.
 * Preserve ordinal order and duplicates; the same expression reuses its v3 ID.
 * SELECT * is already resolved. Aliases/names, bound values and nested query
 * traversal are not exported. A set query's targets describe the set result,
 * not a particular branch. Non-SELECT has flags/count zero (no supported list),
 * distinguishable from an available empty SELECT list. Missing stmt is error.
 * Callbacks are read-only, invocation-borrowed and use v2's sticky error. */
#define SEEKDB_PLUGIN_QUERY_SELECT_LIST 1u
#define SEEKDB_PLUGIN_QUERY_SET_OPERATION 2u
typedef struct seekdb_plugin_query_info_v1 {
  uint32_t struct_size;
  uint32_t statement_type; /* matched host-build stmt::StmtType */
  uint32_t flags;
  uint32_t target_count;
  uint64_t reserved[4];
} seekdb_plugin_query_info_v1_t;
typedef struct seekdb_plugin_candidate_context_v4 {
  seekdb_plugin_candidate_context_v3_t v3;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *query)(void *, seekdb_plugin_query_info_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *target)(void *, uint32_t ordinal, uint32_t *expression);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v4_t;
/* spi_minor=4: normalized semantics and query-block dependencies. These tags
 * are bridge values, NOT host C++ enum ordinals. Unknown kinds remain OTHER.
 * LOCAL_SERIAL is the same admission fact used by fragment v3, not a promise
 * of distributed execution. SCALAR_DETERMINISTIC excludes aggregate/window,
 * subquery, execution parameters, UDF, stateful and known non-pure expressions;
 * it is a host expression contract, not proof an arbitrary plugin is pure.
 * scope describes nonempty relation dependencies within this query block;
 * it does NOT prove physical availability, NULL equivalence, or movability
 * across arbitrary operators. Final allocation/layout validation still applies.
 * columns enumerates the rewritten statement's resolved column references,
 * including those used outside SELECT (ORDER BY/group/filter, etc.). It is not
 * a minimal projection nor an input schema. IDs share the existing graph. */
#define SEEKDB_PLUGIN_RELATION_OTHER 0u
#define SEEKDB_PLUGIN_RELATION_INNER_JOIN 1u
#define SEEKDB_PLUGIN_RELATION_LEFT_JOIN 2u
#define SEEKDB_PLUGIN_RELATION_RIGHT_JOIN 3u
#define SEEKDB_PLUGIN_RELATION_FULL_JOIN 4u
#define SEEKDB_PLUGIN_RELATION_LEFT_SEMI 5u
#define SEEKDB_PLUGIN_RELATION_RIGHT_SEMI 6u
#define SEEKDB_PLUGIN_RELATION_LEFT_ANTI 7u
#define SEEKDB_PLUGIN_RELATION_RIGHT_ANTI 8u
#define SEEKDB_PLUGIN_PLAN_LOCAL_SERIAL 1u
#define SEEKDB_PLUGIN_COMPARE_OTHER 0u
#define SEEKDB_PLUGIN_COMPARE_EQUAL 1u
#define SEEKDB_PLUGIN_COMPARE_NULL_SAFE_EQUAL 2u
#define SEEKDB_PLUGIN_VALUE_OTHER 0u
#define SEEKDB_PLUGIN_VALUE_SIGNED_INTEGER 1u
#define SEEKDB_PLUGIN_VALUE_UNSIGNED_INTEGER 2u
#define SEEKDB_PLUGIN_EXPR_SCALAR_DETERMINISTIC 1u
#define SEEKDB_PLUGIN_SCOPE_INDEPENDENT 0u
#define SEEKDB_PLUGIN_SCOPE_OUTSIDE 1u
#define SEEKDB_PLUGIN_SCOPE_CONTAINED 2u
typedef struct seekdb_plugin_plan_semantics_v1 {
  uint32_t struct_size, relation_kind, flags, reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_plan_semantics_v1_t;
typedef struct seekdb_plugin_expr_semantics_v1 {
  uint32_t struct_size, comparison_kind, value_kind, flags;
  uint64_t reserved[4];
} seekdb_plugin_expr_semantics_v1_t;
typedef struct seekdb_plugin_candidate_context_v5 {
  seekdb_plugin_candidate_context_v4_t v4;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *plan_semantics)(void *, uint32_t, seekdb_plugin_plan_semantics_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *expression_semantics)(void *, uint32_t, seekdb_plugin_expr_semantics_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *scope)(void *, uint32_t plan, uint32_t expression, uint32_t *scope);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *column_count)(void *, uint32_t *count);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *column)(void *, uint32_t ordinal, uint32_t *expression);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v5_t;

/* v6 (candidate service minor 5): inspect execution-parameter bindings owned
 * or carried by a JOIN. These are not prepared-statement parameter values.
 * All IDs share the invocation's expression graph. A binding exposes the
 * parameter expression and its source expression, not a mutable runtime slot.
 * Non-JOIN nodes report zero for these roles; this is NOT a recursive proof
 * that a subtree has no external dependencies. Enumeration does not allocate
 * or rewrite SQL expressions. Unknown roles/indices are sticky errors.
 * A custom implementation removing a binding owner must preserve its binding
 * semantics; the current independent-input executor has no bind/rescan API. */
#define SEEKDB_PLUGIN_BIND_JOIN_NESTED_LOOP 1u
#define SEEKDB_PLUGIN_BIND_JOIN_LEFT_PUSH_DOWN 2u
#define SEEKDB_PLUGIN_BIND_JOIN_RIGHT_PUSH_DOWN 3u
typedef struct seekdb_plugin_candidate_context_v6 {
  seekdb_plugin_candidate_context_v5_t v5;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *binding_count)(void *, uint32_t, uint32_t, uint32_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *binding)(void *, uint32_t, uint32_t, uint32_t, uint32_t *, uint32_t *);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v6_t;
/* v7 (service minor 6): semantic SORT inspection. Unlike PLAN_ORDERING's
 * build-specific direction, sort_key returns normalized DESC/NULLS_FIRST
 * flags for actual SQL sort keys, before encoded-key implementation details.
 * Non-SORT nodes return flags=0, zero counts and all expression IDs=UINT32_MAX.
 * Optional IDs are invocation-local expressions, NOT evaluated limit values.
 * Prefix/partition counts and top-N/top-K/ties/merge/runtime-filter facts must
 * be considered before replacing a SORT. Metadata does not prove equivalence
 * or grant permission to drop row-count, ordering, or side-effect semantics. */
#define SEEKDB_PLUGIN_SORT_PRESENT 1u
#define SEEKDB_PLUGIN_SORT_ENCODED_KEYS 2u
#define SEEKDB_PLUGIN_SORT_LOCAL_MERGE 4u
#define SEEKDB_PLUGIN_SORT_WITH_TIES 8u
#define SEEKDB_PLUGIN_SORT_RUNTIME_FILTER 16u
#define SEEKDB_PLUGIN_SORT_KEY_DESC 1u
#define SEEKDB_PLUGIN_SORT_KEY_NULLS_FIRST 2u
typedef struct seekdb_plugin_sort_info_v1 {
  uint32_t struct_size, flags, key_count, prefix_key_count;
  uint32_t partition_key_count, topn_expression, topk_limit_expression, topk_offset_expression;
  uint32_t hash_expression, reserved_word;
  uint64_t reserved[4];
} seekdb_plugin_sort_info_v1_t;
typedef struct seekdb_plugin_candidate_context_v7 {
  seekdb_plugin_candidate_context_v6_t v6;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *sort_info)(void *, uint32_t, seekdb_plugin_sort_info_v1_t *);
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *sort_key)(void *, uint32_t, uint32_t, uint32_t *, uint32_t *);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v7_t;
/* v8 (service minor 7): value availability at an existing plan boundary.
 * AVAILABLE means the same expression identity can be requested as an output
 * of this retained subtree, without recomputing aggregate/window state or
 * reaching through a projection barrier. It is NOT a final physical slot.
 * SCALAR_ARGUMENTS means ordinary evaluation consumes the graph arguments;
 * clients may decompose a late expression's dependencies, not move evaluation
 * across filters, LIMIT, grouping, NULL extension or side effects. It does not
 * imply determinism. Neither bit is a claim that an unknown value is absent.
 * Constants need no row-owned result. Unknown operators remain unproven.
 * Reads do not allocate/rewrite kernel expressions; final layout checks apply. */
#define SEEKDB_PLUGIN_VALUE_AVAILABLE 1u
#define SEEKDB_PLUGIN_VALUE_SCALAR_ARGUMENTS 2u
typedef struct seekdb_plugin_value_info_v1 {
  uint32_t struct_size, flags;
  uint64_t reserved[4];
} seekdb_plugin_value_info_v1_t;
typedef struct seekdb_plugin_candidate_context_v8 {
  seekdb_plugin_candidate_context_v7_t v7;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *value_info)(void *, uint32_t plan,
      uint32_t expression, seekdb_plugin_value_info_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_candidate_context_v8_t;
/* AROUND must call next once on success; REPLACE may choose without next.
 * Both may select after next. Final success requires a selected host candidate.
 * Invalid get/select and downstream errors cannot be swallowed. Selection has
 * no lasting side effect on failure: the host clears its output. */
typedef struct seekdb_plugin_candidate_service_v1 {
  uint32_t struct_size;
  uint32_t spi_major;
  uint32_t spi_minor;
  uint32_t mode;
  seekdb_plugin_status_t (SEEKDB_PLUGIN_CALL *invoke)(seekdb_plugin_instance_handle_t *,
      const seekdb_plugin_candidate_context_v1_t *);
  uint64_t reserved[4];
} seekdb_plugin_candidate_service_v1_t;

#ifdef __cplusplus
}
#endif
#endif
