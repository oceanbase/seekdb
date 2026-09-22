// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_RUST_SQL_EXPRESSION_FIXTURE_H_
#define SEEKDB_TEST_RUST_SQL_EXPRESSION_FIXTURE_H_
#include <functional>
#include "native_activation_fixture.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "sql/engine/expr/plugin_sql_context.h"
#include "sql/engine/basic/ob_function_table_op.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_operator_factory.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/code_generator/ob_static_engine_cg.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/optimizer/ob_optimizer_util.h"
#include "sql/optimizer/ob_optimizer.h"
#include "sql/optimizer/ob_optimizer_context.h"
#include "sql/optimizer/ob_join_order.h"
#include "sql/optimizer/ob_select_log_plan.h"
#include "sql/optimizer/ob_log_function_table.h"
#include "sql/resolver/dml/ob_select_stmt.h"
#include "sql/resolver/expr/plugin_expr_type.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/parser/ob_parser.h"
#include "routine_overlay_guard_fixture.h"
#include "query_catalog_fixture.h"
#include "query_mutation_fixture.h"
#include "observer/ob_server_plugin_runtime.h"
#include "rust_stored_type_fixture.h"
#include "rust_between_fixture.h"
#include "rust_in_fixture.h"
#include "rust_simple_case_fixture.h"
#include "rust_row_comparison_fixture.h"
#include "rust_row_in_fixture.h"
#include "rust_type_batch_fixture.h"
#include "rust_function_batch_fixture.h"
#include "rust_aggregate_fixture.h"
#include "rust_aggregate_plan_fixture.h"
#include "rust_sort_plan_fixture.h"

namespace rust_sql_expression_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::share::plugin;

// Only routes the module-provider boundary. Selection, binding, leases, native
// callbacks and codecs below are the production loader and actual Rust DSO.
class RuntimeProvider final : public ObIModuleProvider
{
public:
  explicit RuntimeProvider(ObPluginLoader &loader) : loader_(loader), saved_(g_mp) { g_mp = this; }
  ~RuntimeProvider() { g_mp = saved_; }
  int resolves_ = 0, casts_ = 0, functions_ = 0, decodes_ = 0, encodes_ = 0;
  int custom_decode_fault_ = 0, custom_encode_fault_ = 0;
  // Deliberately faulty codec provider for host-sink contract tests only.
  // Normal conversions below continue through the actual Rust DSO/loader.
  static int codec_fault(int mode, const char *type, const seekdb_plugin_execution_context_v1 *context)
  {
    if (mode == 6) return OB_SUCCESS; // Missing emission.
    if (mode == 10) { context->emit_result(nullptr, nullptr); return OB_SUCCESS; }
    seekdb_plugin_execution_result_v1_t value = {};
    value.struct_size = sizeof(value); value.type_id = mode == 1 ? "wrong.type" : type;
    value.data = reinterpret_cast<const uint8_t *>("fault"); value.data_size = 5;
    if (mode == 2) value.reserved[0] = 1;
    if (mode == 3) { value.is_null = 1; value.data = nullptr; value.data_size = 0; }
    if (mode == 5) value.data_size = SEEKDB_PLUGIN_CUSTOM_MAX_ROW_BYTES + 1;
    if (mode == 9) value.is_null = 1; // Noncanonical NULL with payload.
    context->emit_result(context->host, &value); // Intentionally swallow sink errors.
    if (mode == 4) context->emit_result(context->host, &value);
    return mode == 8 ? OB_TIMEOUT : OB_SUCCESS;
  }
  int comparisons_ = 0;
  int function_batches_ = 0, function_batch_rows_ = 0;
  int function_batch_outputs_ = 0;
  std::vector<std::pair<uint32_t, uint64_t>> function_batch_shapes_;
  std::vector<std::string> function_batch_names_;
  int scalar_functions_ = 0;
  int batch_calls(const char *name) const {
    int count = 0;
    for (const auto &called : function_batch_names_) if (called == name) ++count;
    return count;
  }
  int batch_rows(const char *name) const {
    int count = 0;
    for (size_t i = 0; i < function_batch_names_.size(); ++i)
      if (function_batch_names_[i] == name) count += function_batch_shapes_[i].first;
    return count;
  }
  int table_describes_ = 0, table_opens_ = 0;
  int table_nexts_ = 0;
  uint32_t table_last_maximum_ = 0;
  int expected_projection_ = -1;
  void check_projection(const seekdb_plugin_table_execution_context_v1_t *context) const {
    if (expected_projection_ < 0) return;
    CHECK(context && context->struct_size == sizeof(seekdb_plugin_table_execution_context_v4_t));
    const auto &projection = *reinterpret_cast<const seekdb_plugin_table_execution_context_v4_t *>(context);
    CHECK(projection.column_count == 2 && projection.requested_columns && !projection.reserved_word);
    for (int i = 0; i < 2; ++i) CHECK(projection.requested_columns[i] == ((expected_projection_ >> i) & 1));
  }
  class ObservedCursor final : public IPluginTableCursor {
  public:
    ObservedCursor(RuntimeProvider &provider, std::unique_ptr<IPluginTableCursor> cursor)
        : provider_(provider), cursor_(std::move(cursor)) {}
    int next(const seekdb_plugin_table_execution_context_v1_t *context, uint32_t maximum,
        uint32_t *emitted) override {
      ++provider_.table_nexts_; provider_.table_last_maximum_ = maximum;
      provider_.check_projection(context);
      return cursor_->next(context, maximum, emitted);
    }
    int rescan(const seekdb_plugin_execution_value_v1_t *arguments, uint32_t count) override {
      return cursor_->rescan(arguments, count);
    }
    int close() override { return cursor_->close(); }
  private:
    RuntimeProvider &provider_;
    std::unique_ptr<IPluginTableCursor> cursor_;
  };
  int optimizes_ = 0;
  ObPluginLoader *candidate_loader_ = nullptr;
  bool candidate_enabled_ = true;
  bool candidate_build_enabled_ = false;
  bool candidate_custom_enabled_ = false;
  int custom_opens_ = 0, custom_nexts_ = 0, custom_closes_ = 0;
  // Numeric bridge fixtures inspect actual input before it enters the Rust
  // cursor. Fault injection below only replaces output presented to the host.
  std::function<void(const seekdb_plugin_custom_row_v1_t &)> custom_input_observer_;
  std::function<void()> custom_binding_observer_;
  // Snapshot tests overwrite the source frame after Rust has owned its row,
  // but before the real host binding callback consumes its saved Datum values.
  std::function<int(const seekdb_plugin_custom_context_v4_t &)> custom_before_binding_;
  std::function<void(const seekdb_plugin_custom_context_v2_t &)> custom_schema_observer_;
  std::function<void(std::vector<seekdb_plugin_execution_value_v1_t> &)> custom_output_rewriter_;
  class ObservedCustomCursor final : public oceanbase::share::plugin::ICustomExecutor {
  public:
    ObservedCustomCursor(RuntimeProvider &provider, std::unique_ptr<oceanbase::share::plugin::ICustomExecutor> cursor)
        : provider_(provider), cursor_(std::move(cursor)) {}
    int next(const seekdb_plugin_custom_context_v1_t &context) override {
      ++provider_.custom_nexts_;
      if (provider_.custom_schema_observer_) {
        CHECK(context.struct_size == sizeof(seekdb_plugin_custom_context_v4_t));
        provider_.custom_schema_observer_(*reinterpret_cast<const seekdb_plugin_custom_context_v2_t *>(&context));
      }
      if (!provider_.custom_input_observer_ && !provider_.custom_output_rewriter_ && !provider_.custom_binding_observer_ &&
          !provider_.custom_before_binding_) {
        const int ret = cursor_->next(context);
        if (provider_.candidate_layout_derived_ && ret != OB_SUCCESS && ret != OB_ITER_END &&
            context.struct_size == sizeof(seekdb_plugin_custom_context_v4_t)) {
          const auto &view = reinterpret_cast<const seekdb_plugin_custom_context_v2_t &>(context);
          for (const auto *schema : {view.inputs, view.output}) for (uint32_t i = 0; i < schema->column_count; ++i) {
            const auto &c = schema->columns[i];
            std::cerr << "derived layout " << (schema == view.inputs ? "input" : "output") << i << " type=" << c.type_id
                << " sql=" << c.sql_type << " encoding=" << c.encoding << " flags=" << c.flags
                << " collation=" << c.collation << " precision=" << c.precision << " scale=" << c.scale << std::endl;
          }
        }
        return ret;
      }
      struct Bridge {
        RuntimeProvider &provider;
        const seekdb_plugin_custom_context_v1_t &original;
      } bridge{provider_, context};
      seekdb_plugin_custom_context_v4_t bound = {};
      CHECK(context.struct_size == sizeof(bound));
      bound = *reinterpret_cast<const seekdb_plugin_custom_context_v4_t *>(&context);
      auto &controlled = bound.v3;
      auto &extended = controlled.v2;
      auto &wrapped = extended.v1; wrapped = context; wrapped.host_context = &bridge;
      wrapped.next_input = [](void *host, uint32_t index, seekdb_plugin_custom_row_v1_t *row, int32_t *error) {
        auto &b = *static_cast<Bridge *>(host);
        const auto status = b.original.next_input(b.original.host_context, index, row, error);
        if (status == SEEKDB_PLUGIN_STATUS_OK && !*error && b.provider.custom_input_observer_)
          b.provider.custom_input_observer_(*row);
        return status;
      };
      wrapped.emit = [](void *host, const seekdb_plugin_execution_value_v1_t *values, uint32_t count, int32_t *error) {
        auto &b = *static_cast<Bridge *>(host);
        if (!b.provider.custom_output_rewriter_)
          return b.original.emit(b.original.host_context, values, count, error);
        std::vector<seekdb_plugin_execution_value_v1_t> altered(values, values + count);
        b.provider.custom_output_rewriter_(altered);
        return b.original.emit(b.original.host_context, altered.data(), count, error);
      };
      wrapped.check_interrupt = [](void *host, int32_t *error) {
        auto &b = *static_cast<Bridge *>(host);
        return b.original.check_interrupt(b.original.host_context, error);
      };
      controlled.rescan_input = [](void *host, uint32_t index, int32_t *error) {
        auto &b = *static_cast<Bridge *>(host);
        const auto &original = reinterpret_cast<const seekdb_plugin_custom_context_v3_t &>(b.original);
        return original.rescan_input(b.original.host_context, index, error);
      };
      bound.bind_rescan_input = [](void *host, uint32_t index, int32_t *error) -> seekdb_plugin_status_t {
        auto &b = *static_cast<Bridge *>(host);
        const auto &original = reinterpret_cast<const seekdb_plugin_custom_context_v4_t &>(b.original);
        if (b.provider.custom_before_binding_) {
          *error = b.provider.custom_before_binding_(original);
          if (*error) return SEEKDB_PLUGIN_STATUS_INTERNAL;
        }
        const auto status = original.bind_rescan_input(b.original.host_context, index, error);
        if (status == SEEKDB_PLUGIN_STATUS_OK && !*error && b.provider.custom_binding_observer_)
          b.provider.custom_binding_observer_();
        return status;
      };
      return cursor_->next(wrapped);
    }
    int rescan() override { return cursor_->rescan(); }
    int close() override { ++provider_.custom_closes_; return cursor_->close(); }
  private:
    RuntimeProvider &provider_;
    std::unique_ptr<oceanbase::share::plugin::ICustomExecutor> cursor_;
  };
  int bind_plugin_custom_executor(const char *service, uint32_t major, uint32_t minor,
      oceanbase::share::plugin::CustomExecutorBinding &binding) override {
    CHECK(candidate_loader_);
    return candidate_loader_->bind_custom_executor(service, major, minor, binding);
  }
  int open_plugin_custom_executor(const oceanbase::share::plugin::CustomExecutorBinding &binding,
      const uint8_t *plan, uint32_t size, std::unique_ptr<oceanbase::share::plugin::ICustomExecutor> &cursor) override {
    CHECK(candidate_loader_ && !cursor);
    std::unique_ptr<oceanbase::share::plugin::ICustomExecutor> native;
    const int ret = candidate_loader_->open_custom_executor(binding, plan, size, native);
    if (ret == OB_SUCCESS) {
      ++custom_opens_;
      cursor = std::make_unique<ObservedCustomCursor>(*this, std::move(native));
    }
    return ret;
  }
  int candidate_calls_ = 0;
  int natural_candidate_calls_ = 0;
  int candidate_probe_mode_ = 0;
  bool candidate_layout_enabled_ = false;
  bool candidate_fragment_enabled_ = false;
  bool candidate_join_enabled_ = false;
  bool candidate_relation_disabled_ = false;
  bool candidate_subproblem_enabled_ = false;
  bool candidate_upper_enabled_ = false;
  int upper_case_ = 0, upper_probe_mode_ = 0;
  int upper_calls_[7]{}, upper_builds_[7]{};
  int plugin_upper_hooks_available(seekdb_plugin_candidate_phase_t phase, bool &available) override {
    CHECK(phase >= SEEKDB_PLUGIN_PHASE_GROUP && phase <= SEEKDB_PLUGIN_PHASE_ORDERED);
    available = upper_probe_mode_ != 0;
    if (available || !candidate_upper_enabled_) return OB_SUCCESS;
    CHECK(candidate_loader_);
    return candidate_loader_->candidate_hooks_available(phase, available);
  }
  int run_plugin_upper_hooks(seekdb_plugin_candidate_phase_t phase,
      const seekdb_plugin_candidate_context_v1_t &view,
      int (*next)(void *), void *context, int (*validate)(void *)) override {
    CHECK(phase >= SEEKDB_PLUGIN_PHASE_GROUP && phase <= SEEKDB_PLUGIN_PHASE_ORDERED);
    ++upper_calls_[phase];
    if (upper_probe_mode_ == 6) {
      const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
      seekdb_plugin_custom_path_request_v1_t request{};
      request.v1.struct_size = sizeof(request); request.v1.kind = SEEKDB_PLUGIN_PATH_CUSTOM;
      request.service_id = "org.seekdb.rust-candidate.spool"; request.service_major = 1;
      request.operator_cost = 1; // Deliberately does NOT promise preserved order.
      uint32_t added = 0;
      CHECK(api.build(view.host_context, &request.v1, &added) == SEEKDB_PLUGIN_STATUS_UNSUPPORTED_ABI);
      CHECK(added == UINT32_MAX);
      return OB_SUCCESS; // Sticky builder error must survive this faulty hook.
    }
    if (upper_probe_mode_) {
      const int saved = relation_probe_mode_;
      relation_probe_mode_ = upper_probe_mode_;
      const int ret = run_plugin_relation_hooks(view, next, context, validate);
      relation_probe_mode_ = saved;
      return ret;
    }
    CHECK(candidate_upper_enabled_ && candidate_loader_);
    const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
    const uint32_t before = api.current_count(view.host_context);
    const int ret = candidate_loader_->run_candidate_hooks(view, next, context, validate, phase);
    if (ret == OB_SUCCESS) upper_builds_[phase] += api.current_count(view.host_context) - before;
    return ret;
  }
  int join_probe_mode_ = 0, subproblem_calls_ = 0, subproblem_builds_ = 0;
  int plugin_join_hooks_available(bool &available) override {
    available = join_probe_mode_ != 0;
    if (available || !candidate_subproblem_enabled_) return OB_SUCCESS;
    CHECK(candidate_loader_);
    return candidate_loader_->plugin_join_hooks_available(available);
  }
  int run_plugin_join_hooks(const seekdb_plugin_candidate_context_v1_t &view,
      int (*next)(void *), void *context, int (*validate)(void *)) override {
    ++subproblem_calls_;
    if (join_probe_mode_) {
      const int saved = relation_probe_mode_;
      relation_probe_mode_ = join_probe_mode_;
      const int ret = run_plugin_relation_hooks(view, next, context, validate);
      relation_probe_mode_ = saved;
      return ret;
    }
    CHECK(candidate_subproblem_enabled_ && candidate_loader_);
    const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
    const uint32_t before = api.current_count(view.host_context);
    const int ret = candidate_loader_->run_candidate_hooks(view, next, context, validate, SEEKDB_PLUGIN_PHASE_JOIN);
    if (ret == OB_SUCCESS) subproblem_builds_ += api.current_count(view.host_context) - before;
    return ret;
  }
  int candidate_join_builds_ = 0;
  int relation_probe_mode_ = 0;
  bool candidate_layout_derived_ = false;
  bool candidate_layout_target_only_ = false;
  int sort_case_ = 0;
  bool sort_payload_ = false;
  int candidate_layout_builds_ = 0;
  int run_plugin_relation_hooks(const seekdb_plugin_candidate_context_v1_t &view,
      int (*next)(void *), void *context, int (*validate)(void *)) override {
    if (candidate_relation_disabled_ && !relation_probe_mode_) {
      const int ret = next(context); return ret == OB_SUCCESS ? validate(context) : ret;
    }
    if (relation_probe_mode_) {
      CHECK(view.struct_size == sizeof(seekdb_plugin_candidate_context_v8_t));
      const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
      if (relation_probe_mode_ == 4) {
        CHECK(view.select(view.host_context, 0) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
        return OB_SUCCESS; // The planner must enforce sticky errors itself.
      }
      if (relation_probe_mode_ != 1) {
        seekdb_plugin_path_request_v1_t request{sizeof(request), SEEKDB_PLUGIN_PATH_MATERIALIZE, 0, 0, {0}};
        uint32_t added = UINT32_MAX;
        CHECK(api.build(view.host_context, &request, &added) == SEEKDB_PLUGIN_STATUS_OK);
        CHECK(added == view.candidate_count);
        if (relation_probe_mode_ == 3) return OB_TIMEOUT;
        if (relation_probe_mode_ == 5) {
          request.reserved[0] = 1;
          CHECK(api.build(view.host_context, &request, &added) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT);
          CHECK(added == UINT32_MAX);
          return OB_SUCCESS;
        }
        request.input_index = added;
        CHECK(api.build(view.host_context, &request, &added) == SEEKDB_PLUGIN_STATUS_OK);
        CHECK(added == view.candidate_count + 1);
      }
      const uint32_t count = api.current_count(view.host_context);
      const int ret = next(context);
      CHECK(api.current_count(view.host_context) == count);
      return ret == OB_SUCCESS ? validate(context) : ret;
    }
    if (candidate_loader_ && candidate_enabled_ && candidate_custom_enabled_) {
      const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
      const uint32_t before = api.current_count(view.host_context);
      // No C++ matching/building policy: invoke the actual Rust relation hook.
      const int ret = candidate_loader_->run_candidate_hooks(view, next, context, validate, SEEKDB_PLUGIN_PHASE_RELATION);
      if (ret == OB_SUCCESS) {
        const uint32_t added = api.current_count(view.host_context) - before;
        candidate_join_builds_ += added; candidate_calls_ += added;
      }
      return ret;
    }
    return loader_.run_candidate_hooks(view, next, context, validate, SEEKDB_PLUGIN_PHASE_RELATION);
  }
  int run_plugin_candidate_hooks(const seekdb_plugin_candidate_context_v1_t &view,
      int (*next)(void *), void *context, int (*validate)(void *)) override {
    if (candidate_join_enabled_ && !candidate_probe_mode_) {
      // No forced selection: cost the ORDER BY plans over native and custom
      // relation alternatives. The default Rust spool demo is unrelated.
      const int ret = next(context);
      return ret == OB_SUCCESS ? validate(context) : ret;
    }
    if (candidate_layout_enabled_ && !candidate_probe_mode_) {
      CHECK(view.struct_size == sizeof(seekdb_plugin_candidate_context_v8_t));
      const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v3_t &>(view);
      uint32_t root = UINT32_MAX;
      CHECK(api.root(view.host_context, 0, &root) == SEEKDB_PLUGIN_STATUS_OK);
      seekdb_plugin_plan_info_v1_t info{}; info.struct_size = sizeof(info);
      CHECK(api.plan(view.host_context, root, &info) == SEEKDB_PLUGIN_STATUS_OK);
      if (info.expression_counts[2] == 2) {
        uint32_t keys[2], order = 0;
        for (uint32_t i = 0; i < 2; ++i)
          CHECK(api.expression(view.host_context, root, SEEKDB_PLUGIN_PLAN_ORDERING, i, &keys[i], &order) == SEEKDB_PLUGIN_STATUS_OK);
        // The derived fixture knows its Rust table produces positive ordinals:
        // ABS(ordinal) is therefore a valid identity projection in that relation.
        const uint32_t inputs[] = {keys[1], candidate_layout_derived_ ? keys[1] : keys[0], keys[1]};
        const uint32_t outputs[] = {keys[0], keys[1]};
        const uint8_t plan[] = {'S','P','J','1', 2,0,0,0, 1,0,0,0, 0,0,0,0};
        seekdb_plugin_custom_path_request_v2_t request{
          {{sizeof(request), SEEKDB_PLUGIN_PATH_CUSTOM, 0, 0, {0}}, "org.seekdb.rust-candidate.spool",
            1, 0, plan, sizeof(plan), SEEKDB_PLUGIN_PATH_PRESERVES_ORDER | SEEKDB_PLUGIN_PATH_BLOCKING, 1.0, {0}},
          inputs, 3, 2, outputs, {0}};
        uint32_t target_outputs[] = {keys[0], keys[1], UINT32_MAX};
        const uint8_t target_plan[] = {'S','P','J','1', 3,0,0,0, 1,0,0,0, 0,0,0,0, 0,0,0,0};
        if (candidate_layout_target_only_) {
          const auto &query_api = reinterpret_cast<const seekdb_plugin_candidate_context_v4_t &>(view);
          seekdb_plugin_query_info_v1_t query{}; query.struct_size = sizeof(query);
          CHECK(query_api.query(view.host_context, &query) == SEEKDB_PLUGIN_STATUS_OK);
          CHECK(query.flags == SEEKDB_PLUGIN_QUERY_SELECT_LIST && query.target_count == 2);
          uint32_t ordinal = UINT32_MAX;
          CHECK(query_api.target(view.host_context, 0, &ordinal) == SEEKDB_PLUGIN_STATUS_OK && ordinal == keys[1]);
          CHECK(query_api.target(view.host_context, 1, &target_outputs[2]) == SEEKDB_PLUGIN_STATUS_OK);
          CHECK(target_outputs[2] != keys[0] && target_outputs[2] != keys[1]);
          request.outputs = target_outputs; request.output_count = 3;
          request.v1.plan = target_plan; request.v1.plan_size = sizeof(target_plan);
        }
        uint32_t added = UINT32_MAX;
        if (candidate_fragment_enabled_) {
          // Exercise the complete fragment builder/selection/codegen path on
          // normal SQL. The two-input property/topology cases are separate.
          const uint32_t offsets[] = {0, 3};
          seekdb_plugin_custom_path_request_v3_t fragment{request, &root, 1,
              SEEKDB_PLUGIN_CUSTOM_LOCAL_SERIAL, offsets, {0}};
          fragment.v2.v1.v1.struct_size = sizeof(fragment);
          const auto status = api.v2.build(view.host_context, &fragment.v2.v1.v1, &added);
          if (status != SEEKDB_PLUGIN_STATUS_OK)
            std::cerr << "fragment build=" << status << " database=" << api.v2.get_error(view.host_context) << std::endl;
          CHECK(status == SEEKDB_PLUGIN_STATUS_OK);
        } else {
          CHECK(api.v2.build(view.host_context, &request.v1.v1, &added) == SEEKDB_PLUGIN_STATUS_OK);
        }
        ++candidate_layout_builds_; ++candidate_calls_;
        const int ret = next(context);
        if (ret != OB_SUCCESS) return ret;
        CHECK(view.select(view.host_context, added) == SEEKDB_PLUGIN_STATUS_OK);
        return validate(context);
      }
    }
    // Fault injection checks ObLogPlan's actual final validator/output cleanup,
    // separately from the Rust DSO dispatch matrix.
    if (candidate_probe_mode_) {
      if (candidate_probe_mode_ >= 16) {
      CHECK(view.struct_size == sizeof(seekdb_plugin_candidate_context_v8_t));
        const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v3_t &>(view);
        uint32_t out = 0;
        CHECK(api.root(view.host_context, view.candidate_count, &out) == SEEKDB_PLUGIN_STATUS_INVALID_ARGUMENT && out == UINT32_MAX);
        CHECK(api.v2.get_error(view.host_context) == OB_INVALID_ARGUMENT);
        // Ignoring the failed probe cannot permit building or final selection.
        seekdb_plugin_path_request_v1_t request{sizeof(request), SEEKDB_PLUGIN_PATH_MATERIALIZE, 0, 0, {0}};
        CHECK(api.v2.build(view.host_context, &request, &out) != SEEKDB_PLUGIN_STATUS_OK && out == UINT32_MAX);
        CHECK(view.select(view.host_context, 0) == SEEKDB_PLUGIN_STATUS_OK);
        return validate(context);
      }
      if (candidate_probe_mode_ >= 10) {
      CHECK(view.struct_size == sizeof(seekdb_plugin_candidate_context_v8_t));
        const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
        const uint8_t plan[] = {'p', 'l', 'a', 'n'};
        seekdb_plugin_custom_path_request_v1_t request{{sizeof(request), SEEKDB_PLUGIN_PATH_CUSTOM, 0, 0, {0}},
            "org.seekdb.rust-candidate.spool", 1, 0, plan, sizeof(plan), 0, 1.0, {0}};
        if (candidate_probe_mode_ == 10) request.operator_cost = -1.0;
        if (candidate_probe_mode_ == 11) request.flags = 8;
        if (candidate_probe_mode_ == 12) request.service_id = "missing.executor";
        if (candidate_probe_mode_ == 14) request.reserved[3] = 1;
        if (candidate_probe_mode_ == 15) request.plan_size = SEEKDB_PLUGIN_CUSTOM_MAX_PLAN_BYTES + 1;
        uint32_t index = 0;
        const auto status = api.build(view.host_context, &request.v1, &index);
        if (candidate_probe_mode_ == 13) {
          CHECK(status == SEEKDB_PLUGIN_STATUS_OK && index == view.candidate_count);
          const int ret = next(context); return ret == OB_SUCCESS ? validate(context) : ret;
        }
        CHECK(status != SEEKDB_PLUGIN_STATUS_OK && index == UINT32_MAX);
        return api.get_error(view.host_context);
      }
      if (candidate_probe_mode_ >= 5) {
      CHECK(view.struct_size == sizeof(seekdb_plugin_candidate_context_v8_t));
        const auto &api = reinterpret_cast<const seekdb_plugin_candidate_context_v2_t &>(view);
        seekdb_plugin_path_request_v1_t request{sizeof(request), SEEKDB_PLUGIN_PATH_MATERIALIZE, 0, 0, {0}};
        if (candidate_probe_mode_ == 7) request.kind = 999;
        if (candidate_probe_mode_ == 9) request.reserved[0] = 1;
        uint32_t index = UINT32_MAX;
        const int attempts = candidate_probe_mode_ == 8 ? 65 : 1;
        for (int i = 0; i < attempts; ++i) {
          const auto status = api.build(view.host_context, &request, &index);
          if (candidate_probe_mode_ == 7 || candidate_probe_mode_ == 9 || i == 64)
            CHECK(status != SEEKDB_PLUGIN_STATUS_OK && index == UINT32_MAX);
          else CHECK(status == SEEKDB_PLUGIN_STATUS_OK && index == view.candidate_count + i);
        }
        if (candidate_probe_mode_ == 5) { const int ret = next(context); return ret == OB_SUCCESS ? validate(context) : ret; }
        return candidate_probe_mode_ == 6 ? OB_TIMEOUT : api.get_error(view.host_context);
      }
      if (candidate_probe_mode_ == 1) (void)view.select(view.host_context, view.candidate_count);
      if (candidate_probe_mode_ == 2) {
        seekdb_plugin_candidate_info_v1_t invalid{};
        (void)view.get(view.host_context, 0, &invalid);
      }
      if (candidate_probe_mode_ != 3) CHECK(view.select(view.host_context, 0) == SEEKDB_PLUGIN_STATUS_OK);
      return candidate_probe_mode_ == 4 ? OB_TIMEOUT : validate(context);
    }
    if (candidate_loader_ && candidate_enabled_) {
      ++candidate_calls_;
      if (candidate_upper_enabled_) {
        // Test-only choice: the additive Rust hook never selects or fakes a
        // lower cost. Force a contributed root through normal codegen/runtime.
        const int ret = next(context);
        if (ret != OB_SUCCESS) return ret;
        for (uint32_t i = 0; i < view.candidate_count; ++i) {
          seekdb_plugin_candidate_info_v1_t info{}; info.struct_size = sizeof(info);
          CHECK(view.get(view.host_context, i, &info) == SEEKDB_PLUGIN_STATUS_OK);
          if (info.operator_type == log_op_def::LOG_PLUGIN_CUSTOM) {
            CHECK(view.select(view.host_context, i) == SEEKDB_PLUGIN_STATUS_OK);
            break;
          }
        }
        return validate(context);
      }
      return candidate_loader_->run_candidate_hooks(view, next, context, validate);
    }
    return loader_.run_candidate_hooks(view, next, context, validate);
  }
  int table_estimates_ = 0;
  int estimate_bound_plugin_table_function(const seekdb_plugin_sql_binding_v1_t &binding,
      seekdb_plugin_table_estimate_v1_t &estimate) override {
    ++table_estimates_;
    return loader_.estimate_bound_table_function(binding, estimate);
  }
  int run_plugin_optimizer_hooks(const seekdb_plugin_optimizer_info_v1_t &info,
      int (*next)(void *), void *context) override {
    ++optimizes_;
    CHECK(info.statement_kind == SEEKDB_PLUGIN_OPTIMIZER_SELECT);
    return loader_.run_optimizer_hooks(info, next, context);
  }
  int execute_plugin_function(const char *, uint32_t, uint32_t,
      const seekdb_plugin_execution_context_v1 *, const seekdb_plugin_execution_value_v1 *, uint32_t) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  int execute_plugin_extension(seekdb_plugin_extension_kind_t, const char *,
      const seekdb_plugin_execution_context_v1 *, const seekdb_plugin_execution_value_v1 *, uint32_t) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  int describe_plugin_sql_column(const seekdb_plugin_sql_binding_v1_t *binding, uint32_t index,
      seekdb_plugin_sql_column_v1_t *column) override {
    CHECK(binding && column); ++table_describes_;
    return loader_.describe_sql_column(*binding, index, *column);
  }
  int open_bound_plugin_table_function(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_table_execution_context_v1_t *context, const seekdb_plugin_execution_value_v1_t *arguments,
      uint32_t count, std::unique_ptr<IPluginTableCursor> &cursor) override {
    CHECK(binding); ++table_opens_;
    check_projection(context);
    const int ret = loader_.open_bound_table_function(*binding, context, arguments, count, cursor);
    if (ret == OB_SUCCESS && cursor) cursor.reset(new ObservedCursor(*this, std::move(cursor)));
    return ret;
  }
  int mutate_plugin_type_dependency(ObISQLClient &, const seekdb_plugin_sql_binding_v1_t &,
      uint64_t, uint64_t, bool) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int resolve_plugin_sql_object(seekdb_plugin_extension_kind_t kind, const char *name,
      const char *const *types, uint32_t count, seekdb_plugin_sql_binding_v1_t *out) override
  {
    CHECK(out); ++resolves_;
    const int ret = loader_.resolve_sql_extension(kind, name, types, count, *out);
    if (ret != OB_SUCCESS) std::cerr << "Rust object lookup=" << ret << " name=" << name << std::endl;
    return ret;
  }
  int resolve_plugin_common_type(const char *const *types, uint32_t count,
      std::string &common_type, uint64_t &epoch) override
  {
    ++resolves_;
    return loader_.resolve_common_type(types, count, common_type, epoch);
  }
  int resolve_plugin_type_by_id(const char *id, seekdb_plugin_sql_binding_v1_t *out,
      uint64_t epoch = 0) override
  {
    if (!out) return OB_INVALID_ARGUMENT;
    ++resolves_;
    return loader_.resolve_type_by_id(id, *out, epoch);
  }
  int check_bound_plugin_type_comparison(const seekdb_plugin_sql_binding_v1_t &binding) override
  { return loader_.check_bound_type_comparison(binding); }
  int compare_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t &binding,
      const seekdb_plugin_execution_value_v1_t &left, const seekdb_plugin_execution_value_v1_t &right,
      int32_t &ordering) override
  { ++comparisons_; return loader_.compare_bound_type(binding, left, right, ordering); }
  int resolve_plugin_cast(const char *source, const char *target, seekdb_plugin_cast_context_t context,
      seekdb_plugin_sql_cast_binding_v1_t *out, uint64_t expected_epoch = 0) override
  {
    CHECK(out); ++resolves_;
    const int ret = loader_.resolve_sql_cast(source, target, context, *out, expected_epoch);
    if (ret != OB_SUCCESS) std::cerr << "Rust cast lookup=" << ret << " source=" << source << " target=" << target << std::endl;
    return ret;
  }
  int execute_bound_plugin_function(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  {
    CHECK(binding); ++functions_; ++scalar_functions_;
    return loader_.execute_bound_function(*binding, context, args, count);
  }
  int execute_bound_plugin_function_batch(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_batch_context_v1_t *context,
      const seekdb_plugin_batch_row_v1_t *rows, uint32_t count) override
  {
    CHECK(binding); ++function_batches_; function_batch_rows_ += count;
    uint64_t payload = 0;
    for (uint32_t row = 0; row < count; ++row)
      for (uint32_t a = 0; a < rows[row].argument_count; ++a)
        if (!rows[row].arguments[a].is_null) payload += rows[row].arguments[a].data_size;
    function_batch_shapes_.emplace_back(count, payload);
    function_batch_names_.emplace_back(binding->sql_name);
    // functions_ counts input rows at the provider boundary, not DSO calls.
    functions_ += count;
    CHECK(context);
    struct Observer {
      RuntimeProvider &provider;
      const seekdb_plugin_batch_context_v1_t &destination;
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
          uint32_t index, const seekdb_plugin_execution_result_v1_t *result) {
        auto &self = *reinterpret_cast<Observer *>(host);
        ++self.provider.function_batch_outputs_;
        return self.destination.emit_result(self.destination.host, index, result);
      }
    } observer{*this, *context};
    auto forwarded = *context;
    forwarded.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&observer);
    forwarded.emit_result = Observer::emit;
    return loader_.execute_bound_function_batch(*binding, &forwarded, rows, count);
  }
  int execute_bound_plugin_cast(const seekdb_plugin_sql_cast_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context, const seekdb_plugin_execution_value_v1 *value) override
  {
    CHECK(binding); ++casts_;
    return loader_.execute_bound_cast(*binding, context, value);
  }
  int decode_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context, const uint8_t *bytes, uint64_t size) override
  {
    CHECK(binding); ++decodes_;
    if (custom_decode_fault_) return codec_fault(custom_decode_fault_, binding->object_id, context);
    return loader_.decode_bound_type(*binding, context, bytes, size);
  }
  int encode_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context, const seekdb_plugin_execution_value_v1 *value) override
  {
    CHECK(binding); ++encodes_;
    if (custom_encode_fault_) return codec_fault(custom_encode_fault_, "core.type.bytes", context);
    return loader_.encode_bound_type(*binding, context, value);
  }
private:
  ObPluginLoader &loader_;
  ObIModuleProvider *saved_;
};

// Exercise the real set-query binder and evaluate its converted projections.
// This is not a full SELECT resolver or UNION physical-operator test.
inline void type_comparisons(RuntimeProvider &provider, ObPluginLoader &loader,
    ObArenaAllocator &arena, ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  struct Pair { const char *left, *right; int order; bool custom; int nulls = 0; };
  const Pair pairs[] = {
    {"seekdb_rust_text('z')", "seekdb_rust_text('aa')", -1, true},
    {"CAST('aa' AS rust_utf8)", "CAST('z' AS rust_utf8)", 1, true},
    {"CAST('中' AS rust_utf8)", "seekdb_rust_text('中')", 0, true},
    {"CAST('' AS rust_utf8)", "CAST('a\\0b' AS rust_utf8)", -1, true},
    {"CASE WHEN 1 THEN seekdb_rust_text('z') ELSE NULL END", "CAST('aa' AS rust_utf8)", -1, true},
    {"CAST(NULL AS rust_utf8)", "seekdb_rust_text('aa')", 0, true, 1},
    {"seekdb_rust_text('z')", "NULL", 0, true, 1},
    {"CAST(NULL AS rust_utf8)", "CAST(NULL AS rust_utf8)", 0, true, 2},
    {"'z'", "'aa'", 1, false},
    {"seekdb_rust_text('z')", "'aa'", 1, false}, // Declared implicit cast to bytes.
    {"1", "2", -1, false},
    {"NULL", "NULL", 0, false, 2},
  };
  bool tested_wire = false, tested_failures = false;
  for (int op = 0; op < 7; ++op) for (const auto &pair : pairs) {
    const char *operators[] = {"=", "<>", "<", "<=", ">", ">=", "<=>"};
    const std::string sql = std::string(pair.left) + " " + operators[op] + " " + pair.right;
    const bool expected = pair.nulls ? (op == 6 && pair.nulls == 2) :
        op == 0 || op == 6 ? pair.order == 0 : op == 1 ? pair.order != 0 :
        op == 2 ? pair.order < 0 : op == 3 ? pair.order <= 0 : op == 4 ? pair.order > 0 : pair.order >= 0;
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString(sql.size(), sql.data()), session.get_charsets4parser(),
        arena, node, session.get_sql_mode()) == OB_SUCCESS && node);
    ObSEArray<ObQualifiedName, 1> columns;
    ObSEArray<ObVarInfo, 1> variables;
    ObSEArray<ObAggFunRawExpr *, 1> aggregates;
    ObSEArray<ObWinFunRawExpr *, 1> windows;
    ObSEArray<ObSubQueryInfo, 1> subqueries;
    ObSEArray<ObUDFInfo, 1> udfs;
    ObSEArray<ObOpRawExpr *, 1> raw_operators;
    ObRawExpr *raw = nullptr;
    const int before = provider.comparisons_;
    int status = ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
        variables, aggregates, windows, subqueries, udfs, raw_operators);
    if (status == OB_SUCCESS && raw) status = raw->formalize(&session);
    if (status != OB_SUCCESS) std::cerr << "comparison bind=" << status << " sql=" << sql << std::endl;
    CHECK(status == OB_SUCCESS && raw && !raw->get_plugin_type() && provider.comparisons_ == before);
    auto *comparison = raw->get_param_expr(0);
    CHECK((comparison->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_COMPARE) == pair.custom);
    if (pair.custom) CHECK(raw->has_flag(CNT_STATE_FUNC));
    const int resolved = provider.resolves_;
    CHECK(raw->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolved);
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolved);
    if (pair.custom) {
      PluginTypeComparisonExtraInfo info(arena, T_FUN_SYS_PLUGIN_TYPE_COMPARE);
      CHECK(PluginTypeComparisonExpr::read_binding(*copy->get_param_expr(0), info) == OB_SUCCESS);
      CHECK(info.null_safe_ == (op == 6) && info.valid() && info.binding_.owner_generation && info.binding_.catalog_epoch);
      CHECK(std::strcmp(info.binding_.object_id, "org.seekdb.rust-text.utf8") == 0);
      CHECK(!(info.binding_.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT));
      if (!tested_wire) {
        tested_wire = true;
        const int64_t size = info.get_serialize_size();
        std::vector<char> wire(size);
        int64_t pos = 0; CHECK(info.serialize(wire.data(), size, pos) == OB_SUCCESS && pos == size);
        PluginTypeComparisonExtraInfo restored(arena, T_FUN_SYS_PLUGIN_TYPE_COMPARE);
        pos = 0; CHECK(restored.deserialize(wire.data(), size, pos) == OB_SUCCESS && pos == size && restored.valid());
        CHECK(restored.binding_.catalog_epoch == info.binding_.catalog_epoch &&
            restored.binding_.owner_generation == info.binding_.owner_generation);
        for (int64_t cut = 0; cut < size; ++cut) {
          PluginTypeComparisonExtraInfo short_info(arena, T_FUN_SYS_PLUGIN_TYPE_COMPARE);
          pos = 0; CHECK(short_info.deserialize(wire.data(), cut, pos) != OB_SUCCESS && !short_info.valid());
        }
        ObIExprExtraInfo *extra_copy = nullptr;
        CHECK(info.deep_copy(arena, T_FUN_SYS_PLUGIN_TYPE_COMPARE, extra_copy) == OB_SUCCESS && extra_copy);
        CHECK(static_cast<PluginTypeComparisonExtraInfo *>(extra_copy)->valid());
        static_cast<PluginTypeComparisonExtraInfo *>(extra_copy)->~PluginTypeComparisonExtraInfo();
        info.null_safe_ = 2; CHECK(!info.valid());
        CHECK(info.deep_copy(arena, T_FUN_SYS_PLUGIN_TYPE_COMPARE, extra_copy) == OB_INVALID_ARGUMENT && !extra_copy);
      }
    }
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    ObDatum *value = nullptr;
    status = root->eval(eval, value);
    if (status != OB_SUCCESS) std::cerr << "comparison eval=" << status << " sql=" << sql << std::endl;
    CHECK(status == OB_SUCCESS && value);
    if (pair.nulls && op != 6) CHECK(value->is_null());
    else CHECK(!value->is_null() && value->get_int() == expected);
    CHECK(provider.resolves_ == resolved && provider.comparisons_ == before + (pair.custom && !pair.nulls ? 1 : 0));
    if (pair.custom && !pair.nulls && !tested_failures) {
      tested_failures = true;
      ObExpr *cmp = nullptr;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy->get_param_expr(0), outputs, cmp) == OB_SUCCESS && cmp);
      auto *extra = dynamic_cast<PluginTypeComparisonExtraInfo *>(cmp->extra_info_);
      CHECK(extra && extra->valid());
      const auto clear = [&] {
        cmp->get_eval_info(eval).evaluated_ = false;
        root->get_eval_info(eval).evaluated_ = false;
      };
      ++extra->binding_.catalog_epoch;
      clear(); CHECK(root->eval(eval, value) == OB_STATE_NOT_MATCH);
      --extra->binding_.catalog_epoch;
      const auto saved = cmp->args_[0]->locate_expr_datum(eval);
      const char invalid = char(0xff);
      cmp->args_[0]->locate_expr_datum(eval).set_string(ObString(1, &invalid));
      clear(); CHECK(root->eval(eval, value) == OB_INVALID_ARGUMENT); // Actual Rust comparator error.
      cmp->args_[0]->locate_expr_datum(eval) = saved;
      class Cancelled final : public ObIExtraStatusCheck {
      public:
        const char *name() const override { return "plugin-comparison-cancel"; }
        int check() const override { return OB_TIMEOUT; }
      } cancelled;
      const int called = provider.comparisons_;
      {
        ObIExtraStatusCheck::Guard cancellation(execution, cancelled);
        clear(); CHECK(root->eval(eval, value) == OB_TIMEOUT && provider.comparisons_ == called);
      }
      clear(); CHECK(root->eval(eval, value) == OB_SUCCESS && value && value->get_int() == expected);
    }
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}

inline void set_branches(RuntimeProvider &provider, ObArenaAllocator &arena,
                         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  struct Case {
    const char *left, *right, *id, *left_value, *right_value;
    bool distinct = false;
    int status = OB_SUCCESS;
  };
  for (const Case &test : {
      Case{"seekdb_rust_text('hello')", "NULL", "org.seekdb.rust-text.utf8", "hello", nullptr},
      Case{"NULL", "seekdb_rust_text('hello')", "org.seekdb.rust-text.utf8", nullptr, "hello"},
      Case{"seekdb_rust_text('hello')", "seekdb_rust_text('world')", "org.seekdb.rust-text.utf8", "hello", "world"},
      Case{"seekdb_rust_text('hello')", "'ok'", "core.type.bytes", "hello", "ok"},
      Case{"'ok'", "seekdb_rust_text('hello')", "core.type.bytes", "ok", "hello"},
      Case{"'a'", "'longer'", nullptr, "a", "longer"},
      Case{"seekdb_rust_char_count('abc')", "7", "core.type.int64", "3", "7"},
      Case{"seekdb_rust_char_count('abc')", "1e0", "core.type.float64", "3", "1"},
      Case{"seekdb_rust_text('hello')", "'ok'", "core.type.bytes", "hello", "ok", true},
      Case{"seekdb_rust_text('hello')", "seekdb_rust_text('hello')", nullptr, nullptr, nullptr,
          true, OB_NOT_SUPPORTED},
      Case{"seekdb_rust_text('hello')", "7", nullptr, nullptr, nullptr,
          false, OB_ERR_INVALID_TYPE_FOR_OP}}) {
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStmtFactory statements(arena);
    ObSelectStmt *left = nullptr, *right = nullptr, *combined = nullptr;
    CHECK(statements.create_stmt(left) == OB_SUCCESS);
    CHECK(statements.create_stmt(right) == OB_SUCCESS);
    CHECK(statements.create_stmt(combined) == OB_SUCCESS);
    auto parse = [&](const char *sql) {
      const ParseNode *node = nullptr;
      CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(sql), session.get_charsets4parser(),
          arena, node, session.get_sql_mode()) == OB_SUCCESS && node);
      ObSEArray<ObQualifiedName, 1> columns;
      ObSEArray<ObVarInfo, 1> variables;
      ObSEArray<ObAggFunRawExpr *, 1> aggregates;
      ObSEArray<ObWinFunRawExpr *, 1> windows;
      ObSEArray<ObSubQueryInfo, 1> subqueries;
      ObSEArray<ObUDFInfo, 1> udfs;
      ObSEArray<ObOpRawExpr *, 1> operators;
      ObRawExpr *raw = nullptr;
      CHECK(ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
          variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS);
      CHECK(raw && raw->formalize(&session) == OB_SUCCESS);
      return raw;
    };
    SelectItem item;
    auto *original_left = parse(test.left), *original_right = parse(test.right);
    item.expr_ = original_left; CHECK(left->add_select_item(item) == OB_SUCCESS);
    item.expr_ = original_right; CHECK(right->add_select_item(item) == OB_SUCCESS);
    combined->assign_set_op(ObSelectStmt::UNION);
    if (test.distinct) combined->assign_set_distinct(); else combined->assign_set_all();
    CHECK(combined->add_set_query(left) == OB_SUCCESS);
    const int calls = provider.casts_ + provider.functions_;
    const int normalized = ObOptimizerUtil::try_add_cast_to_set_child_list(&arena, &session, &factory,
        test.distinct, combined->get_set_query(), right);
    if (normalized != test.status) std::cerr << "set normalize=" << normalized << " left=" << test.left << std::endl;
    CHECK(normalized == test.status && provider.casts_ + provider.functions_ == calls);
    if (test.status != OB_SUCCESS) {
      CHECK(left->get_select_item(0).expr_ == original_left && right->get_select_item(0).expr_ == original_right);
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    CHECK(combined->add_set_query(right) == OB_SUCCESS);
    CHECK(ObOptimizerUtil::gen_set_target_list(&arena, &session, &factory, combined) == OB_SUCCESS);
    auto *output = combined->get_select_item(0).expr_;
    if (test.id) {
      CHECK(output->get_plugin_type() && output->get_plugin_type()->logical_id_ == ObString::make_string(test.id));
      CHECK(output->get_plugin_type()->catalog_epoch_ != 0 && !output->get_plugin_type()->stored_);
      ObRawExpr *copy = nullptr;
      CHECK(ObRawExprCopier::copy_expr(factory, output, copy) == OB_SUCCESS);
      // Set expressions deliberately use pointer identity for same_as().
      CHECK(copy->get_plugin_type() && *copy->get_plugin_type() == *output->get_plugin_type());
      CHECK(copy->get_plugin_type() != output->get_plugin_type());
      CHECK(copy->get_plugin_type()->logical_id_.ptr() != output->get_plugin_type()->logical_id_.ptr());
      CHECK(static_cast<ObSetOpRawExpr *>(copy)->get_idx() == static_cast<ObSetOpRawExpr *>(output)->get_idx());
    } else CHECK(!output->get_plugin_type());
    CHECK(output->deduce_type(&session) == OB_SUCCESS);
    if (test.id && std::strcmp(test.id, "org.seekdb.rust-text.utf8") == 0) {
      // A normalized set result remains a logical value in another set group.
      ObSelectStmt *outer = nullptr, *tail = nullptr;
      CHECK(statements.create_stmt(outer) == OB_SUCCESS && statements.create_stmt(tail) == OB_SUCCESS);
      item.expr_ = parse("NULL"); CHECK(tail->add_select_item(item) == OB_SUCCESS);
      outer->assign_set_op(ObSelectStmt::UNION); outer->assign_set_all();
      CHECK(outer->add_set_query(combined) == OB_SUCCESS);
      CHECK(ObOptimizerUtil::try_add_cast_to_set_child_list(&arena, &session, &factory, false,
          outer->get_set_query(), tail) == OB_SUCCESS);
      CHECK(outer->add_set_query(tail) == OB_SUCCESS);
      CHECK(ObOptimizerUtil::gen_set_target_list(&arena, &session, &factory, outer) == OB_SUCCESS);
      CHECK(outer->get_select_item(0).expr_->get_plugin_type()->logical_id_ == ObString::make_string(test.id));
      CHECK(tail->get_select_item(0).expr_->get_plugin_type()->logical_id_ == ObString::make_string(test.id));
    }
    ObRawExprUniqueSet roots(false);
    CHECK(roots.append(left->get_select_item(0).expr_) == OB_SUCCESS);
    CHECK(roots.append(right->get_select_item(0).expr_) == OB_SUCCESS);
    const int resolves = provider.resolves_;
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    for (int i = 0; i < 2; ++i) {
      auto *raw = (i == 0 ? left : right)->get_select_item(0).expr_;
      ObExpr *root = nullptr;
      ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS && root);
      ObDatum *value = nullptr; CHECK(root->eval(eval, value) == OB_SUCCESS && value);
      const char *expected = i == 0 ? test.left_value : test.right_value;
      if (!expected) CHECK(value->is_null());
      else if (raw->get_data_type() == ObDoubleType) CHECK(!value->is_null() && value->get_double() == std::strtod(expected, nullptr));
      else if (raw->get_data_type() == ObIntType) CHECK(!value->is_null() && value->get_int() == std::strtoll(expected, nullptr, 10));
      else CHECK(!value->is_null() && value->get_string() == ObString::make_string(expected));
    }
    CHECK(provider.resolves_ == resolves);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}

inline void select_queries(RuntimeProvider &provider, ObArenaAllocator &arena,
                           ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  using namespace oceanbase::share::schema;
  auto schema_service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  struct Case {
    const char *sql, *type;
    const char *argument = nullptr;
    int status = OB_SUCCESS;
  };
  for (const Case &test : {
      Case{"SELECT seekdb_rust_text('hello')", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_text('hello') UNION ALL SELECT NULL", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_char_count(v) FROM (SELECT seekdb_rust_text('hello') AS v UNION ALL SELECT NULL) d", "core.type.int64", "org.seekdb.rust-text.utf8"},
      Case{"SELECT (SELECT seekdb_rust_text('hello'))", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_char_count((SELECT seekdb_rust_text('hello')))", "core.type.int64", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_char_count((SELECT (SELECT seekdb_rust_text('hello'))))", "core.type.int64", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_char_count((SELECT CAST(NULL AS rust_utf8)))", "core.type.int64", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_char_count(CASE WHEN 1 THEN (SELECT seekdb_rust_text('hello')) ELSE NULL END)", "core.type.int64", "org.seekdb.rust-text.utf8"},
      Case{"SELECT (SELECT seekdb_rust_text('hello')) UNION ALL SELECT NULL", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_char_count((SELECT seekdb_rust_text('hello') UNION ALL SELECT NULL LIMIT 1))", "core.type.int64", "org.seekdb.rust-text.utf8"},
      Case{"SELECT seekdb_rust_identity((SELECT seekdb_rust_text('hello')))", "org.seekdb.rust-text.utf8", "org.seekdb.rust-text.utf8"},
      Case{"SELECT (SELECT 1)", nullptr},
      Case{"SELECT seekdb_rust_text('z') < CAST('aa' AS rust_utf8)", nullptr},
      Case{"SELECT CAST(NULL AS rust_utf8) <=> CAST(NULL AS rust_utf8)", nullptr},
      Case{"SELECT 1 WHERE seekdb_rust_text('z') < CAST('aa' AS rust_utf8)", nullptr},
      Case{"SELECT EXISTS(SELECT seekdb_rust_text('hello'))", nullptr},
      Case{"SELECT seekdb_rust_text('z') IN (CAST('a' AS rust_utf8),CAST('z' AS rust_utf8))", nullptr},
      Case{"SELECT seekdb_rust_text('z') IN (SELECT seekdb_rust_text('z'))", nullptr, nullptr, OB_NOT_SUPPORTED},
      Case{"SELECT 'z' IN (SELECT seekdb_rust_text('z'))", nullptr, nullptr, OB_NOT_SUPPORTED},
      Case{"SELECT seekdb_rust_text('z') NOT IN (SELECT 'z')", nullptr, nullptr, OB_NOT_SUPPORTED},
      Case{"SELECT 1 IN (SELECT 1)", nullptr},
      Case{"SELECT CASE seekdb_rust_text('z') WHEN CAST('z' AS rust_utf8) THEN 1 ELSE 0 END", nullptr},
      Case{"SELECT CASE seekdb_rust_text('z') WHEN CAST('z' AS rust_utf8) THEN seekdb_rust_text('chosen') ELSE NULL END", "org.seekdb.rust-text.utf8"},
      Case{"SELECT CASE seekdb_rust_text('z') WHEN 7 THEN 1 ELSE 0 END", nullptr, nullptr, OB_ERR_INVALID_TYPE_FOR_OP},
      Case{"SELECT (seekdb_rust_text('z'),1) < (seekdb_rust_text('aa'),2)", nullptr},
      Case{"SELECT (seekdb_rust_text('z'),1) <=> (CAST(NULL AS rust_utf8),2)", nullptr},
      Case{"SELECT (seekdb_rust_text('z'),1) IN ((CAST('a' AS rust_utf8),1),(CAST('z' AS rust_utf8),1))", nullptr},
      Case{"SELECT (CAST(NULL AS rust_utf8),1) NOT IN ((CAST('z' AS rust_utf8),2))", nullptr},
      Case{"SELECT ((seekdb_rust_text('z'),1),2) < ((seekdb_rust_text('aa'),1),2)", nullptr},
      Case{"SELECT ((seekdb_rust_text('z'),1),2) IN (((NULL,1),2),((CAST('z' AS rust_utf8),1),2))", nullptr},
      Case{"SELECT ((CAST(NULL AS rust_utf8),1),2) NOT IN (((NULL,1),3))", nullptr},
      Case{"SELECT CASE WHEN 1 THEN (SELECT seekdb_rust_text('hello')) ELSE 7 END", nullptr, nullptr, OB_ERR_INVALID_TYPE_FOR_OP}}) {
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStmtFactory statements(arena);
    // Constant LIMIT resolution requires a guard even without table lookup.
    // The existing empty in-memory schema fixture supplies no authentication.
    ObSchemaGetterGuard schema_guard;
    CHECK(MockSchemaService::bind(schema_guard, *schema_service, *manager) == OB_SUCCESS);
    ObSchemaChecker checker;
    CHECK(checker.init(schema_guard) == OB_SUCCESS);
    ObResolverParams params;
    params.allocator_ = &arena; params.expr_factory_ = &factory;
    params.stmt_factory_ = &statements; params.query_ctx_ = statements.get_query_ctx();
    params.session_info_ = &session; params.schema_checker_ = &checker;
    CHECK(params.query_ctx_);
    ObParser parser(arena, session.get_sql_mode());
    ParseResult parsed{};
    CHECK(parser.parse(ObString::make_string(test.sql), parsed) == OB_SUCCESS);
    CHECK(parsed.result_tree_ && parsed.result_tree_->children_[0]);
    ObSelectResolver resolver(params);
    const int callbacks = provider.functions_ + provider.casts_ + provider.decodes_;
    const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
    if (status != test.status) std::cerr << "SELECT resolve=" << status << " sql=" << test.sql << std::endl;
    CHECK(status == test.status);
    CHECK(provider.functions_ + provider.casts_ + provider.decodes_ == callbacks);
    if (status != OB_SUCCESS) {
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    auto *statement = resolver.get_select_stmt();
    CHECK(statement && statement->get_select_item_size() == 1);
    const auto *type = statement->get_select_item(0).expr_->get_plugin_type();
    if (test.type) {
      if (!type) std::cerr << "SELECT lost plugin type: " << test.sql << std::endl;
      CHECK(type && type->logical_id_ == ObString::make_string(test.type) && type->catalog_epoch_);
    } else CHECK(!type);
    const int resolves = provider.resolves_;
    auto *output = statement->get_select_item(0).expr_;
    CHECK(output->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    if (test.argument) {
      CHECK(output->get_expr_type() == T_FUN_SYS_PLUGIN_FUNCTION);
      seekdb_plugin_sql_binding_v1_t binding = {};
      std::vector<std::string> arguments;
      CHECK(PluginFunctionExpr::resolve_raw_binding(*static_cast<ObSysFunRawExpr *>(output), binding, arguments) == OB_SUCCESS);
      CHECK(arguments == std::vector<std::string>{test.argument});
      CHECK(provider.resolves_ == resolves);
    }
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}

inline void table_queries(RuntimeProvider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
    ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  const std::string long_token(9000, 'x');
  const std::string long_argument = "'" + long_token + "'";
  struct Case {
    const char *argument; std::vector<std::string> tokens; std::vector<int64_t> lengths;
    const char *function = "seekdb_rust_words";
  };
  for (const auto &test : {
      Case{"seekdb_rust_text('  hello 中🙂 world  ')", {"hello", "中🙂", "world"}, {5, 2, 5}},
      Case{"CAST('a\tb\nc' AS rust_utf8)", {"a", "b", "c"}, {1, 1, 1}},
      Case{"seekdb_rust_text('')", {}, {}},
      Case{"seekdb_rust_text('   ')", {}, {}},
      Case{"CAST(NULL AS rust_utf8)", {}, {}},
      Case{"seekdb_rust_text('  hello 中🙂 world  ')", {"hello", "中🙂", "world"}, {5, 2, 5}, "seekdb_rust_words_bytes"},
      Case{"'a b'", {"a", "b"}, {1, 1}, "seekdb_rust_words_bytes"},
      Case{long_argument.c_str(), {long_token}, {9000}, "seekdb_rust_words_bytes"},
      Case{"seekdb_rust_text('')", {}, {}, "seekdb_rust_words_bytes"},
      Case{"CAST(NULL AS rust_utf8)", {}, {}, "seekdb_rust_words_bytes"},
      Case{"NULL", {"<NULL>"}, {6}, "seekdb_rust_words_or_null"},
      Case{"CAST(NULL AS rust_utf8)", {"<NULL>"}, {6}, "seekdb_rust_words_or_null"},
      Case{"''", {}, {}, "seekdb_rust_words_or_null"},
      Case{"NULL", {}, {}, "seekdb_rust_words_strict"},
      Case{"CAST(NULL AS rust_utf8)", {}, {}, "seekdb_rust_words_strict"},
      Case{"seekdb_rust_text('a b')", {"a", "b"}, {1, 1}, "seekdb_rust_words_strict"}}) {
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStmtFactory statements(arena); ObSchemaChecker checker;
    ObResolverParams params;
    params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
    params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = &session; params.schema_checker_ = &checker;
    const std::string sql = std::string("SELECT token, ordinal, seekdb_rust_char_count(token) FROM TABLE(") +
        test.function + "(" + test.argument + "))";
    ObParser parser(arena, session.get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(params);
    const int ret = resolver.resolve(*parsed.result_tree_->children_[0]);
    if (ret != OB_SUCCESS) std::cerr << "Rust table SELECT resolve=" << ret << " sql=" << sql << std::endl;
    CHECK(ret == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt();
    CHECK(stmt && stmt->get_select_item_size() == 3 && stmt->get_table_items().count() == 1);
    auto *raw = stmt->get_table_items().at(0)->function_table_expr_;
    {
      // Actual production candidate-cost method with the resolved expression;
      // this is distinct from the manually assembled executor spec below.
      ObAddr address;
      ObGlobalHint hint;
      ObOptimizerContext context(&session, &execution, nullptr, nullptr, arena, nullptr,
          address, hint, factory, stmt, false, params.query_ctx_);
      ObSelectLogPlan plan(context, stmt);
      ObJoinOrder relation(&arena, &plan, FUNCTION_TABLE_ACCESS);
      relation.set_output_rows(199); relation.set_output_row_size(199);
      FunctionTablePath path;
      path.parent_ = &relation; path.value_expr_ = raw;
      path.strong_sharding_ = context.get_match_all_sharding(); path.parallel_ = 1;
      const int estimates = provider.table_estimates_, opens = provider.table_opens_;
      const int executes = provider.functions_ + provider.casts_;
      CHECK(path.estimate_cost() == OB_SUCCESS);
      CHECK(provider.table_estimates_ == estimates + 1);
      CHECK(provider.table_opens_ == opens && provider.functions_ + provider.casts_ == executes);
      CHECK(path.cost_ == 4 && path.op_cost_ == 4);
      CHECK(relation.get_output_rows() == 8 && relation.get_output_row_size() == 40);
      ObLogFunctionTable logical(plan);
      CHECK(logical.compute_property(&path) == OB_SUCCESS);
      CHECK(logical.get_card() == 8 && logical.get_width() == 40 && logical.get_cost() == 4);
      // A controlled competing candidate costs 2: the old hard-coded cost 1
      // would dominate it, while the Rust estimate 4 must lose to it.
      FunctionTablePath alternative;
      alternative.parent_ = &relation; alternative.value_expr_ = raw;
      alternative.strong_sharding_ = path.strong_sharding_; alternative.parallel_ = 1;
      alternative.op_cost_ = alternative.cost_ = 2;
      CHECK(relation.add_path(&path) == OB_SUCCESS);
      CHECK(relation.add_path(&alternative) == OB_SUCCESS);
      CHECK(relation.get_interesting_paths().count() == 1);
      CHECK(relation.get_interesting_paths().at(0) == &alternative);
    }
    auto *consumer = stmt->get_select_item(2).expr_;
    seekdb_plugin_sql_binding_v1_t binding = {}; std::vector<std::string> arguments;
    CHECK(PluginFunctionExpr::resolve_raw_binding(*static_cast<ObSysFunRawExpr *>(consumer), binding, arguments) == OB_SUCCESS);
    CHECK(arguments == std::vector<std::string>{"org.seekdb.rust-text.utf8"});
    const int lookups = provider.resolves_, descriptions = provider.table_describes_, opens = provider.table_opens_;
    CHECK(raw->deduce_type(&session) == OB_SUCCESS);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
    for (int64_t i = 0; i < 3; ++i) CHECK(roots.append(stmt->get_select_item(i).expr_) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    const auto runtime = [&](ObRawExpr *expr) {
      ObExpr *result = nullptr; ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*expr, outputs, result) == OB_SUCCESS && result);
      return result;
    };
    auto *table = runtime(raw), *count = runtime(consumer);
    ObSEArray<ObExpr *, 2> columns;
    CHECK(columns.push_back(runtime(stmt->get_select_item(0).expr_)) == OB_SUCCESS);
    CHECK(columns.push_back(runtime(stmt->get_select_item(1).expr_)) == OB_SUCCESS);
    // Real operator lifecycle over generated expressions. The spec is assembled
    // here; this is not yet evidence of optimizer-selected physical plans.
    ObPhysicalPlan physical;
    ObFunctionTableSpec spec(arena, PHY_FUNCTION_TABLE);
    spec.plan_ = &physical; spec.value_expr_ = table;
    CHECK(spec.column_exprs_.assign(columns) == OB_SUCCESS);
    CHECK(spec.output_.assign(columns) == OB_SUCCESS);
    CHECK(spec.calc_exprs_.prepare_allocate(3) == OB_SUCCESS);
    spec.calc_exprs_.at(0) = columns.at(0); spec.calc_exprs_.at(1) = columns.at(1);
    spec.calc_exprs_.at(2) = count;
    ObFunctionTableOp op(execution, spec, nullptr);
    CHECK(op.open() == OB_SUCCESS);
    for (int pass = 0; pass < 2; ++pass) {
      for (size_t i = 0; i < test.tokens.size(); ++i) {
        CHECK(op.get_next_row() == OB_SUCCESS);
        CHECK(columns.at(0)->locate_expr_datum(eval).get_string() == ObString(test.tokens[i].size(), test.tokens[i].data()));
        CHECK(columns.at(1)->locate_expr_datum(eval).get_int() == i + 1);
        ObDatum *length = nullptr;
        CHECK(count->eval(eval, length) == OB_SUCCESS && length && length->get_int() == test.lengths[i]);
      }
      for (int eof = 0; eof < 2; ++eof) CHECK(op.get_next_row() == OB_ITER_END);
      const int empty_admissions = provider.table_opens_;
      for (int eof = 0; eof < 2; ++eof) {
        CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_ITER_END);
      }
      CHECK(provider.table_opens_ == empty_admissions);
      CHECK(op.rescan() == OB_SUCCESS);
    }
    // All calls reach bound loader admission. A strict NULL returns an empty
    // result without calling Rust open; repeated fetch must not re-admit it.
    CHECK(provider.table_opens_ == opens + 2);
    if (!test.tokens.empty()) {
      CHECK(op.get_next_row() == OB_SUCCESS);
      const int admitted = provider.table_opens_;
      execution.get_physical_plan_ctx()->set_timeout_timestamp(1);
      CHECK(op.get_next_row() == OB_TIMEOUT);
      CHECK(provider.table_opens_ == admitted);
      execution.get_physical_plan_ctx()->set_timeout_timestamp(0);
      CHECK(op.rescan() == OB_SUCCESS);
      class TableFailure final : public ObIExtraStatusCheck {
      public:
        mutable int calls_ = 0;
        int fail_at_ = 1;
        const char *name() const override { return "plugin-table-query-control-fixture"; }
        int check() const override { return ++calls_ >= fail_at_ ? OB_TIMEOUT : OB_SUCCESS; }
      } failure;
      // For a long single word, fail during scanning, not at the initial poll
      // or before emitting the row. Direct fetch bypasses the operator's outer
      // status check, so only the real Rust next -> host poll observes this.
      failure.fail_at_ = test.tokens.front().size() == long_token.size() ? 3 : 1;
      {
        ObIExtraStatusCheck::Guard extra(execution, failure);
        CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_TIMEOUT);
        CHECK(failure.calls_ == failure.fail_at_);
      }
      ObPluginStatusSnapshot status;
      CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
      CHECK(status.lease_count_ == 0);
      const int failed_admissions = provider.table_opens_;
      // Removing the injected error cannot silently reopen a failed cursor.
      CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_TIMEOUT);
      CHECK(provider.table_opens_ == failed_admissions);
      CHECK(op.rescan() == OB_SUCCESS);
      CHECK(op.get_next_row() == OB_SUCCESS);
      CHECK(provider.table_opens_ == failed_admissions + 1);
      CHECK(columns.at(0)->locate_expr_datum(eval).get_string() ==
          ObString(test.tokens.front().size(), test.tokens.front().data()));
      CHECK(columns.at(1)->locate_expr_datum(eval).get_int() == 1);
    }
    CHECK(provider.resolves_ == lookups && provider.table_describes_ == descriptions);
    CHECK(op.close() == OB_SUCCESS);
    CHECK(op.close() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}

// Actual batch-sized expression frames, production spec codegen/operator/loader
// and Rust DSO. The logical plan/output plumbing is assembled here, so this is
// not evidence of a full optimizer-selected plan or aggregate execution.
inline void table_batches(RuntimeProvider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
    ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  CHECK(ObOperatorFactory::is_vectorized(PHY_FUNCTION_TABLE));
  const std::vector<std::string> tokens{"alpha", "中🙂", std::string(513, 'x'), "d", "end"};
  const std::string argument = tokens[0] + " " + tokens[1] + " " + tokens[2] + " d end";
  for (int projection : {3, 2, 1, 0, 7, 11}) {
    provider.expected_projection_ = projection & 3;
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStmtFactory statements(arena); ObSchemaChecker checker;
    ObResolverParams params;
    params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
    params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = &session; params.schema_checker_ = &checker;
    const char *selection = projection == 11 ? "token" : projection == 7 ? "ordinal, token" : projection == 3 ? "token, ordinal" :
        projection == 2 ? "ordinal" : projection == 1 ? "token" : "COUNT(*)";
    const std::string sql = std::string("SELECT ") + selection + " FROM TABLE(seekdb_rust_words_bytes('" + argument + "'))" +
        (projection == 11 ? " WHERE ordinal > 0" : "");
    ObParser parser(arena, session.get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(params);
    CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt();
    auto *raw = stmt->get_table_items().at(0)->function_table_expr_;
    // The normal rewrite pipeline recomputes references before codegen.
    // Resolving a function table initially declares all columns; bypassing
    // this phase falsely leaves unused columns marked as referenced.
    CHECK(stmt->formalize_stmt_expr_reference(&factory, &session) == OB_SUCCESS);
    CHECK(stmt->get_column_size() == ((projection & 1) != 0) + ((projection & 2) != 0));
    CHECK(stmt->formalize_stmt_expr_reference(&factory, &session) == OB_SUCCESS);
    CHECK(stmt->get_column_size() == ((projection & 1) != 0) + ((projection & 2) != 0));
    ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
    if (projection) {
      for (int64_t i = 0; i < stmt->get_select_item_size(); ++i)
        CHECK(roots.append(stmt->get_select_item(i).expr_) == OB_SUCCESS);
    }
    for (int64_t i = 0; i < stmt->get_condition_exprs().count(); ++i)
      CHECK(roots.append(stmt->get_condition_exprs().at(i)) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    generator.set_batch_size(4);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    const auto runtime = [&](ObRawExpr *expr) {
      ObExpr *result = nullptr; ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*expr, outputs, result) == OB_SUCCESS && result);
      return result;
    };
    ObPhysicalPlan physical;
    ObFunctionTableSpec spec(arena, PHY_FUNCTION_TABLE);
    spec.plan_ = &physical; spec.max_batch_size_ = 4;
    // Operator open allocates its skip vector from the plan's vectorization
    // flag, not only the operator spec's maximum. Match production codegen.
    physical.set_batch_size(4);
    ObAddr address; ObGlobalHint hint;
    ObOptimizerContext optimizer(&session, &execution, nullptr, nullptr, arena, nullptr,
        address, hint, factory, stmt, false, params.query_ctx_);
    ObSelectLogPlan plan(optimizer, stmt);
    ObLogFunctionTable logical(plan);
    logical.set_type(log_op_def::LOG_FUNCTION_TABLE);
    logical.set_table_id(stmt->get_table_items().at(0)->table_id_);
    logical.add_values_expr(raw);
    if (projection) {
      for (int64_t i = 0; i < stmt->get_select_item_size(); ++i)
        CHECK(logical.get_output_exprs().push_back(stmt->get_select_item(i).expr_) == OB_SUCCESS);
    }
    ObStaticEngineCG codegen;
    const int generated = ObOperatorFactory::generate_spec(codegen, logical, spec, true);
    if (generated != OB_SUCCESS) std::cerr << "table spec codegen=" << generated << " projection=" << projection
        << " columns=" << stmt->get_column_size() << " logical stmt=" << logical.get_stmt() << std::endl;
    CHECK(generated == OB_SUCCESS);
    CHECK(spec.value_expr_ == runtime(raw) && spec.column_exprs_.count() == 2);
    CHECK((spec.column_exprs_.at(0) != nullptr) == ((projection & 1) != 0));
    CHECK((spec.column_exprs_.at(1) != nullptr) == ((projection & 2) != 0));
    if (projection == 3) {
      // Corrupt only the resolved ordinal metadata, not generated expressions.
      // Both out-of-range ordinals and duplicate slots must fail in production
      // codegen before a cursor can be admitted.
      CHECK(stmt->get_column_size() == 2);
      auto *item = stmt->get_column_item(1);
      const uint64_t saved_id = item->column_id_;
      const int opens = provider.table_opens_;
      for (uint64_t invalid_id : {uint64_t(OB_APP_MIN_COLUMN_ID - 1),
          uint64_t(OB_APP_MIN_COLUMN_ID + 2), stmt->get_column_item(0)->column_id_}) {
        item->column_id_ = invalid_id;
        ObStaticEngineCG invalid_codegen;
        ObFunctionTableSpec invalid_spec(arena, PHY_FUNCTION_TABLE);
        CHECK(ObOperatorFactory::generate_spec(invalid_codegen, logical, invalid_spec, true) == OB_ERR_UNEXPECTED);
      }
      item->column_id_ = saved_id;
      CHECK(provider.table_opens_ == opens);
    }
    CHECK(spec.output_.init(2) == OB_SUCCESS);
    CHECK(spec.calc_exprs_.init(3) == OB_SUCCESS);
    if (projection) {
      for (int64_t i = 0; i < stmt->get_select_item_size(); ++i) {
        auto *column = runtime(stmt->get_select_item(i).expr_);
        CHECK(column->is_batch_result());
        const auto *raw_column = static_cast<const ObColumnRefRawExpr *>(stmt->get_select_item(i).expr_);
        CHECK(spec.column_exprs_.at(raw_column->get_column_id() - OB_APP_MIN_COLUMN_ID) == column);
        CHECK(spec.output_.push_back(column) == OB_SUCCESS);
      }
    }
    for (int64_t i = 0; i < spec.column_exprs_.count(); ++i) {
      if (spec.column_exprs_.at(i)) CHECK(spec.calc_exprs_.push_back(spec.column_exprs_.at(i)) == OB_SUCCESS);
    }
    CHECK(spec.filters_.init(stmt->get_condition_exprs().count()) == OB_SUCCESS);
    for (int64_t i = 0; i < stmt->get_condition_exprs().count(); ++i) {
      auto *filter = runtime(stmt->get_condition_exprs().at(i));
      CHECK(spec.filters_.push_back(filter) == OB_SUCCESS);
      CHECK(spec.calc_exprs_.push_back(filter) == OB_SUCCESS);
    }
    // The production expression-pointer encoding explicitly reserves index 0
    // for NULL. Check that sparse column mappings survive serialization.
    auto *saved_array = ObExpr::get_serialize_array();
    ObExpr::get_serialize_array() = &frame.rt_exprs_;
    const int64_t size = spec.column_exprs_.get_serialize_size();
    std::vector<char> serialized(size);
    int64_t position = 0;
    CHECK(spec.column_exprs_.serialize(serialized.data(), size, position) == OB_SUCCESS && position == size);
    ObFixedArray<ObExpr *, ObIAllocator> restored(arena);
    position = 0;
    CHECK(restored.deserialize(serialized.data(), size, position) == OB_SUCCESS && position == size);
    CHECK(restored.count() == 2 && restored.at(0) == spec.column_exprs_.at(0) && restored.at(1) == spec.column_exprs_.at(1));
    ObExpr::get_serialize_array() = saved_array;
    ObFunctionTableOp op(execution, spec, nullptr);
    CHECK(op.open() == OB_SUCCESS);
    auto &eval = op.get_eval_ctx();
    for (int pass = 0; pass < 2; ++pass) {
      size_t position = 0;
      for (int request : {2, 4}) {
        const int calls = provider.table_nexts_;
        const ObBatchRows *rows = nullptr;
        CHECK(op.get_next_batch(request, rows) == OB_SUCCESS && rows);
        CHECK(rows->size_ == (request == 2 ? 2 : 3) && !rows->end_);
        CHECK(provider.table_nexts_ == calls + 1 && provider.table_last_maximum_ == request);
        for (int64_t i = 0; i < rows->size_; ++i, ++position) {
          CHECK(!rows->skip_->at(i));
          if (projection & 1) CHECK(spec.column_exprs_.at(0)->locate_batch_datums(eval)[i].get_string() ==
              ObString(tokens[position].size(), tokens[position].data()));
          if (projection & 2) CHECK(spec.column_exprs_.at(1)->locate_batch_datums(eval)[i].get_int() == position + 1);
        }
      }
      CHECK(position == tokens.size());
      const int calls = provider.table_nexts_;
      for (int i = 0; i < 2; ++i) {
        const ObBatchRows *rows = nullptr;
        CHECK(op.get_next_batch(4, rows) == OB_SUCCESS && rows && rows->end_ && rows->size_ == 0);
        CHECK(provider.table_nexts_ == calls + 1);
      }
      CHECK(op.rescan() == OB_SUCCESS);
    }
    class BatchFailure final : public ObIExtraStatusCheck {
    public:
      mutable int calls_ = 0;
      const char *name() const override { return "plugin-batch-mid-emission"; }
      int check() const override { return ++calls_ >= 3 ? OB_TIMEOUT : OB_SUCCESS; }
    } failure;
    uint32_t emitted = 99;
    {
      ObIExtraStatusCheck::Guard extra(execution, failure);
      // Rust polls before scanning/emitting each word: third poll is after
      // first row emission. Direct fetch excludes the outer operator check.
      CHECK(PluginTableFunctionExpr::fetch_batch(*spec.value_expr_, eval, spec.column_exprs_, 4, emitted) == OB_TIMEOUT);
      CHECK(failure.calls_ == 3 && emitted == 0);
    }
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
    const int calls = provider.table_nexts_, opens = provider.table_opens_;
    CHECK(PluginTableFunctionExpr::fetch_batch(*spec.value_expr_, eval, spec.column_exprs_, 4, emitted) == OB_TIMEOUT);
    CHECK(emitted == 0 && provider.table_nexts_ == calls && provider.table_opens_ == opens);
    CHECK(op.rescan() == OB_SUCCESS);
    const ObBatchRows *rows = nullptr;
    CHECK(op.get_next_batch(4, rows) == OB_SUCCESS && rows && rows->size_ == 4);
    CHECK(op.close() == OB_SUCCESS);
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
  provider.expected_projection_ = -1;
}

// The same vectorized operator must still run the built-in scalar generator
// through its legacy one-row adapter, including empty streams and repeated EOF.
inline void generator_batches(ObArenaAllocator &arena, ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  for (int length : {0, 3}) {
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStmtFactory statements(arena); ObSchemaChecker checker; ObResolverParams params;
    params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
    params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = &session; params.schema_checker_ = &checker;
    const std::string sql = "SELECT COLUMN_VALUE FROM TABLE(generator(" + std::to_string(length) + "))";
    ObParser parser(arena, session.get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(params);
    const int resolved = resolver.resolve(*parsed.result_tree_->children_[0]);
    if (resolved != OB_SUCCESS) {
      auto *partial = resolver.get_select_stmt();
      std::cerr << "generator resolve=" << resolved << " sql=" << sql << " tables="
          << (partial ? partial->get_table_size() : -1) << " columns="
          << (partial ? partial->get_column_size() : -1) << std::endl;
    }
    CHECK(resolved == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt();
    CHECK(stmt->formalize_stmt_expr_reference(&factory, &session) == OB_SUCCESS);
    CHECK(stmt->get_column_size() == 1);
    auto *raw = stmt->get_table_items().at(0)->function_table_expr_;
    ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
    CHECK(roots.append(stmt->get_select_item(0).expr_) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0); generator.set_batch_size(4);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObAddr address; ObGlobalHint hint;
    ObOptimizerContext optimizer(&session, &execution, nullptr, nullptr, arena, nullptr,
        address, hint, factory, stmt, false, params.query_ctx_);
    ObSelectLogPlan plan(optimizer, stmt); ObLogFunctionTable logical(plan);
    logical.set_type(log_op_def::LOG_FUNCTION_TABLE);
    logical.set_table_id(stmt->get_table_items().at(0)->table_id_); logical.add_values_expr(raw);
    CHECK(logical.get_output_exprs().push_back(stmt->get_select_item(0).expr_) == OB_SUCCESS);
    ObPhysicalPlan physical; physical.set_batch_size(4);
    ObFunctionTableSpec spec(arena, PHY_FUNCTION_TABLE); spec.plan_ = &physical; spec.max_batch_size_ = 4;
    ObStaticEngineCG codegen;
    CHECK(ObOperatorFactory::generate_spec(codegen, logical, spec, true) == OB_SUCCESS);
    CHECK(spec.column_exprs_.count() == 1 && spec.column_exprs_.at(0));
    CHECK(spec.output_.assign(spec.column_exprs_) == OB_SUCCESS);
    CHECK(spec.calc_exprs_.init(2) == OB_SUCCESS);
    CHECK(spec.calc_exprs_.push_back(spec.value_expr_) == OB_SUCCESS);
    CHECK(spec.calc_exprs_.push_back(spec.column_exprs_.at(0)) == OB_SUCCESS);
    ObFunctionTableOp op(execution, spec, nullptr); CHECK(op.open() == OB_SUCCESS);
    const ObBatchRows *rows = nullptr;
    for (int i = 1; i <= length; ++i) {
      CHECK(op.get_next_batch(4, rows) == OB_SUCCESS && rows && rows->size_ == 1 && !rows->end_);
      CHECK(spec.column_exprs_.at(0)->locate_batch_datums(op.get_eval_ctx())[0].get_int() == i);
    }
    for (int i = 0; i < 2; ++i)
      CHECK(op.get_next_batch(4, rows) == OB_SUCCESS && rows && rows->size_ == 0 && rows->end_);
    CHECK(op.close() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}

// Actual generated table expression -> loader -> Rust open -> host SQL status
// check. The failure happens before the uninitialized inner SQL connection, so
// this proves propagation/cleanup, not successful live database SQL execution.
inline void table_sql_failure(RuntimeProvider &provider, ObPluginLoader &loader,
    ObArenaAllocator &arena, ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  ObExecContext execution(arena); execution.set_my_session(&session);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
  ObStmtFactory statements(arena); ObSchemaChecker checker;
  ObResolverParams params;
  params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
  params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = &session; params.schema_checker_ = &checker;
  ObParser parser(arena, session.get_sql_mode()); ParseResult parsed{};
  CHECK(parser.parse(ObString::make_string("SELECT ordinal FROM TABLE(seekdb_rust_sql_series('A中🙂'))"), parsed) == OB_SUCCESS);
  ObSelectResolver resolver(params);
  CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
  auto *statement = resolver.get_select_stmt();
  CHECK(statement && statement->get_select_item_size() == 1 && statement->get_table_items().count() == 1);
  auto *raw = statement->get_table_items().at(0)->function_table_expr_;
  auto *column = statement->get_select_item(0).expr_;
  ObRawExprUniqueSet roots(false);
  CHECK(roots.append(raw) == OB_SUCCESS && roots.append(column) == OB_SUCCESS);
  ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
  ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
  CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
  CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObEvalCtx eval(execution);
  ObExpr *table = nullptr, *output = nullptr; ObSEArray<ObRawExpr *, 1> generated;
  CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, generated, table) == OB_SUCCESS && table);
  CHECK(ObStaticEngineExprCG::generate_rt_expr(*column, generated, output) == OB_SUCCESS && output);
  ObSEArray<ObExpr *, 1> columns; CHECK(columns.push_back(output) == OB_SUCCESS);
  class SqlFailure final : public ObIExtraStatusCheck {
  public:
    mutable int calls_ = 0;
    const char *name() const override { return "plugin-table-sql-fixture"; }
    int check() const override { ++calls_; return OB_TIMEOUT; }
  } failure;
  const int opens = provider.table_opens_;
  for (int pass = 0; pass < 2; ++pass) {
    {
      ObIExtraStatusCheck::Guard extra(execution, failure);
      const int fetched = PluginTableFunctionExpr::fetch_row(*table, eval, columns);
      if (fetched != OB_TIMEOUT) std::cerr << "table SQL open failure=" << fetched << " polls=" << failure.calls_ << std::endl;
      CHECK(fetched == OB_TIMEOUT);
      CHECK(failure.calls_ == pass + 1);
    }
    CHECK(provider.table_opens_ == opens + pass + 1);
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
    CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_TIMEOUT);
    CHECK(provider.table_opens_ == opens + pass + 1);
    CHECK(PluginTableFunctionExpr::rescan(*table, eval) == OB_SUCCESS);
  }
  CHECK(PluginTableFunctionExpr::close(*table, eval) == OB_SUCCESS);
  // The same real parameter conversion path is used by scalar SQL. Exercise
  // every supported kind before the controlled status failure; a default,
  // unbound ParamStore allocator used to fail before reaching this check.
  int64_t signed_value = -7; uint64_t unsigned_value = 9; double floating_value = 1.5;
  const char text[] = "hello";
  const uint8_t bytes[] = {0, 255};
  seekdb_plugin_sql_value_v1_t parameters[] = {
    {sizeof(seekdb_plugin_sql_value_v1_t), SEEKDB_PLUGIN_SQL_NULL, nullptr, 0, {0}},
    {sizeof(seekdb_plugin_sql_value_v1_t), SEEKDB_PLUGIN_SQL_INT64, &signed_value, sizeof(signed_value), {0}},
    {sizeof(seekdb_plugin_sql_value_v1_t), SEEKDB_PLUGIN_SQL_UINT64, &unsigned_value, sizeof(unsigned_value), {0}},
    {sizeof(seekdb_plugin_sql_value_v1_t), SEEKDB_PLUGIN_SQL_FLOAT64, &floating_value, sizeof(floating_value), {0}},
    {sizeof(seekdb_plugin_sql_value_v1_t), SEEKDB_PLUGIN_SQL_TEXT, text, sizeof(text) - 1, {0}},
    {sizeof(seekdb_plugin_sql_value_v1_t), SEEKDB_PLUGIN_SQL_BYTES, bytes, sizeof(bytes), {0}},
  };
  PluginSqlContext scalar_sql(execution);
  seekdb_plugin_execution_context_v2_t scalar_context{};
  scalar_context.v1.struct_size = sizeof(scalar_context); scalar_sql.attach(scalar_context);
  seekdb_plugin_sql_result_v1_t result{}; result.struct_size = sizeof(result);
  const char statement_sql[] = "SELECT ?,?,?,?,?,?";
  {
    ObIExtraStatusCheck::Guard extra(execution, failure);
    CHECK(scalar_context.sql_api->execute(scalar_context.sql_context, statement_sql, sizeof(statement_sql) - 1,
        parameters, 6, 1, nullptr, nullptr, &result) == SEEKDB_PLUGIN_STATUS_TIMEOUT);
  }
  CHECK(failure.calls_ == 3 && scalar_sql.error() == OB_TIMEOUT && result.database_error == OB_TIMEOUT);
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
}

// Real optimizer entry -> C++ loader -> Rust continuation -> Rust DSO -> core.
// Deliberately incomplete context tests core error preservation, not successful
// plan construction (which requires initialized optimizer/schema services).
inline void optimizer_entry(RuntimeProvider &provider, ObPluginLoader &loader,
    ObArenaAllocator &arena, ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  seekdb_plugin_sql_binding_v1_t binding = {};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION,
      "seekdb_rust_optimizer_calls", nullptr, 0, binding) == OB_SUCCESS);
  struct Counter {
    int64_t value = 0; int calls = 0;
    static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
        const seekdb_plugin_execution_result_v1_t *result) {
      auto &self = *reinterpret_cast<Counter *>(host);
      CHECK(!result->is_null && result->data_size == sizeof(self.value));
      CHECK(std::strcmp(result->type_id, "core.type.int64") == 0);
      std::memcpy(&self.value, result->data, sizeof(self.value));
      ++self.calls;
      return SEEKDB_PLUGIN_STATUS_OK;
    }
  } counter;
  seekdb_plugin_execution_context_v1_t call = {};
  call.struct_size = sizeof(call); call.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&counter);
  call.emit_result = Counter::emit;
  CHECK(loader.execute_bound_function(binding, &call, nullptr, 0) == OB_SUCCESS);
  const int64_t before = counter.value;
  const int invocations = provider.optimizes_;
  ObSelectStmt statement;
  ObAddr address;
  ObGlobalHint hint;
  ObOptimizerContext context(&session, nullptr, nullptr, nullptr, arena, nullptr,
      address, hint, factory, &statement, false);
  ObOptimizer optimizer(context);
  ObLogPlan *plan = nullptr;
  CHECK(optimizer.optimize(statement, plan) == OB_INVALID_ARGUMENT);
  CHECK(plan == nullptr && provider.optimizes_ == invocations + 1);
  CHECK(loader.execute_bound_function(binding, &call, nullptr, 0) == OB_SUCCESS);
  CHECK(counter.calls == 2 && counter.value == before + 1);
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS);
  CHECK(status.lease_count_ == 0);
}

inline void query_control(ObPluginLoader &loader)
{
  const char *types[] = {"core.type.bytes"};
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_char_count", types, 1, binding) == OB_SUCCESS);
  for (int scenario = 0; scenario < 5; ++scenario) {
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    auto *plan = execution.get_physical_plan_ctx();
    plan->set_timeout_timestamp(scenario == 1 ? ObClockGenerator::getClock() + 10000000 : scenario == 2 ? 1 : 0);
    if (scenario == 3) CHECK(session.set_session_state(QUERY_KILLED) == OB_SUCCESS);
    class MidCallFailure final : public ObIExtraStatusCheck {
    public:
      mutable int calls_ = 0;
      bool fail_ = false;
      const char *name() const override { return "plugin-query-control-fixture"; }
      int check() const override { return ++calls_ >= 3 && fail_ ? OB_TIMEOUT : OB_SUCCESS; }
    } failure;
    failure.fail_ = scenario == 4;
    ObIExtraStatusCheck::Guard extra(execution, failure);
    struct Sink {
      bool emitted = false;
      static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *raw,
          const seekdb_plugin_execution_result_v1_t *result) {
        auto &self = *reinterpret_cast<Sink *>(raw);
        CHECK(!self.emitted && result && result->data_size == sizeof(int64_t));
        int64_t value = 0; std::memcpy(&value, result->data, sizeof(value)); CHECK(value == 9000);
        self.emitted = true; return SEEKDB_PLUGIN_STATUS_OK;
      }
    } sink;
    PluginSqlContext control(execution);
    seekdb_plugin_execution_context_v2_t context{};
    context.v1.struct_size = sizeof(context); context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    context.v1.emit_result = Sink::emit; control.attach(context);
    CHECK(context.sql_api && context.sql_api->struct_size == sizeof(seekdb_plugin_sql_api_v4_t));
    const auto &api = *reinterpret_cast<const seekdb_plugin_sql_api_v2_t *>(context.sql_api);
    seekdb_plugin_query_status_v1_t output{}; output.struct_size = sizeof(output);
    const int expected = scenario == 2 || scenario == 4 ? OB_TIMEOUT : scenario == 3 ? OB_ERR_QUERY_INTERRUPTED : OB_SUCCESS;
    if (scenario != 4) {
      const auto status = api.poll_query(context.sql_context, &output);
      CHECK((status == SEEKDB_PLUGIN_STATUS_OK) == (expected == OB_SUCCESS));
      CHECK(output.database_error == expected && control.error() == expected);
      if (scenario == 0) CHECK(output.remaining_us == -1);
      if (scenario == 1) CHECK(output.remaining_us >= 0 && output.remaining_us <= 10000000);
      if (expected != OB_SUCCESS) CHECK(output.remaining_us == 0);
    }
    if (scenario == 2) {
      plan->set_timeout_timestamp(0); // Once observed, timeout remains sticky.
      CHECK(api.poll_query(context.sql_context, &output) == SEEKDB_PLUGIN_STATUS_TIMEOUT);
      seekdb_plugin_sql_result_v1_t result{}; result.struct_size = sizeof(result);
      CHECK(api.v1.execute(context.sql_context, nullptr, 0, nullptr, 0, 0, nullptr, nullptr, &result) == SEEKDB_PLUGIN_STATUS_TIMEOUT);
      CHECK(result.database_error == OB_TIMEOUT);
    }
    const std::string text(9000, 'x');
    seekdb_plugin_execution_value_v1_t argument{}; argument.struct_size = sizeof(argument);
    argument.type_id = types[0]; argument.data = reinterpret_cast<const uint8_t *>(text.data()); argument.data_size = text.size();
    const int code = loader.execute_bound_function(binding, &context.v1, &argument, 1);
    CHECK((code == OB_SUCCESS) == (expected == OB_SUCCESS));
    CHECK(control.error() == expected && sink.emitted == (expected == OB_SUCCESS));
    if (scenario == 4) CHECK(failure.calls_ == 3);
  }
}

inline void run(const char *artifact_path, const char *package_root, const char *candidate_path)
{
  native_activation_test::Observation candidate_observation;
  candidate_observation.expected_services = candidate_observation.expected_extensions = 1;
  auto candidate_guard = std::make_shared<native_activation_test::TestGuard>(candidate_observation);
  ObPluginLoader candidate_loader;
  const std::string native_path(candidate_path);
  const auto native_slash = native_path.rfind('/'); CHECK(native_slash != std::string::npos);
  CHECK(candidate_loader.init(native_path.substr(0, native_slash),
      std::make_shared<native_activation_test::TestVerifier>(false, false, false),
      candidate_guard, candidate_guard, candidate_observation.registry) == OB_SUCCESS);
  CHECK(candidate_loader.load(native_path.substr(native_slash + 1)) == OB_SUCCESS);
  {
    oceanbase::observer::ObServerPluginRuntime unavailable;
    seekdb_plugin_sql_binding_v1_t binding{}; binding.catalog_epoch = 99;
    CHECK(unavailable.resolve_type_by_id("org.seekdb.rust-text.utf8", &binding) == OB_NOT_INIT);
    CHECK(binding.struct_size == 0 && binding.catalog_epoch == 0);
    CHECK(unavailable.resolve_type_by_id("org.seekdb.rust-text.utf8", nullptr) == OB_INVALID_ARGUMENT);
    CHECK(unavailable.check_bound_type_comparison(binding) == OB_NOT_INIT);
    seekdb_plugin_execution_value_v1_t value{};
    int32_t ordering = 99;
    CHECK(unavailable.compare_bound_type(binding, value, value, ordering) == OB_NOT_INIT && ordering == 0);
  }
  native_activation_test::Observation observation;
  observation.expected_services = 19; observation.expected_extensions = 23;
  auto guard = std::make_shared<native_activation_test::TestGuard>(observation);
  ObPluginLoader loader;
  const std::string path(artifact_path);
  const auto slash = path.rfind('/'); CHECK(slash != std::string::npos);
  CHECK(loader.init(path.substr(0, slash), std::make_shared<native_activation_test::TestVerifier>(false, false, true),
      guard, guard, observation.registry) == OB_SUCCESS);
  const int load_status = loader.load(path.substr(slash + 1));
  if (load_status != OB_SUCCESS) {
    std::cerr << "Rust fixture load failed: " << load_status << ": " << loader.last_error() << std::endl;
  }
  CHECK(load_status == OB_SUCCESS);
  CHECK(observation.committed && observation.completed && !observation.aborted);
  query_control(loader);
  query_catalog_test::run(loader);
  query_mutation_test::run(loader);
  {
    RuntimeProvider provider(loader);
    provider.candidate_loader_ = &candidate_loader;
    const char *branches[] = {"org.seekdb.rust-text.utf8", nullptr, "core.type.bytes"};
    std::string common_type;
    uint64_t epoch = 0;
    CHECK(g_mp->resolve_plugin_common_type(branches, 3, common_type, epoch) == OB_SUCCESS);
    CHECK(common_type == "core.type.bytes" && epoch == observation.registry->registry_epoch());
    {
      seekdb_plugin_sql_binding_v1_t type{};
      CHECK(g_mp->resolve_plugin_type_by_id(branches[0], &type, epoch) == OB_SUCCESS);
      CHECK(std::strcmp(type.sql_name, "rust_utf8") == 0 && type.catalog_epoch == epoch);
      CHECK(g_mp->check_bound_plugin_type_comparison(type) == OB_SUCCESS);
      seekdb_plugin_execution_value_v1_t left{}, right{};
      left.struct_size = right.struct_size = sizeof(left);
      left.type_id = right.type_id = type.object_id;
      left.data = reinterpret_cast<const uint8_t *>("z"); left.data_size = 1;
      right.data = reinterpret_cast<const uint8_t *>("aa"); right.data_size = 2;
      int32_t ordering = 99;
      CHECK(g_mp->compare_bound_plugin_type(type, left, right, ordering) == OB_SUCCESS && ordering == -1);
      auto stale = type; ++stale.catalog_epoch;
      CHECK(g_mp->check_bound_plugin_type_comparison(stale) == OB_STATE_NOT_MATCH);
      ordering = 99;
      CHECK(g_mp->compare_bound_plugin_type(stale, left, right, ordering) == OB_STATE_NOT_MATCH && ordering == 0);
      CHECK(g_mp->resolve_plugin_type_by_id(branches[0], &type, epoch + 1) == OB_STATE_NOT_MATCH);
      CHECK(type.struct_size == 0 && type.catalog_epoch == 0);
      CHECK(g_mp->resolve_plugin_type_by_id(branches[0], nullptr, epoch) == OB_INVALID_ARGUMENT);
      // A provider that has not opted into the new optional methods cannot
      // accidentally retain the previous successful result.
      type.catalog_epoch = 99;
      CHECK(provider.ObIModuleProvider::resolve_plugin_type_by_id(branches[0], &type, epoch) == OB_NOT_SUPPORTED);
      CHECK(type.struct_size == 0 && type.catalog_epoch == 0);
      CHECK(provider.ObIModuleProvider::check_bound_plugin_type_comparison(type) == OB_NOT_SUPPORTED);
      ordering = 99;
      CHECK(provider.ObIModuleProvider::compare_bound_plugin_type(type, left, right, ordering)
          == OB_NOT_SUPPORTED && ordering == 0);
    }
    seekdb_plugin_sql_cast_binding_v1_t conversion = {};
    CHECK(g_mp->resolve_plugin_cast(branches[0], common_type.c_str(), SEEKDB_PLUGIN_CAST_IMPLICIT,
        &conversion, epoch) == OB_SUCCESS);
    CHECK(conversion.catalog_epoch == epoch);
    CHECK(g_mp->resolve_plugin_cast(branches[0], common_type.c_str(), SEEKDB_PLUGIN_CAST_IMPLICIT,
        &conversion, epoch + 1) == OB_STATE_NOT_MATCH);
    CHECK(conversion.struct_size == 0 && conversion.catalog_epoch == 0);
    ObArenaAllocator arena;
    ObRawExprFactory factory(arena);
    auto session = std::make_unique<ObSQLSessionInfo>();
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
    type_comparisons(provider, loader, arena, factory, *session);
    optimizer_entry(provider, loader, arena, factory, *session);
    {
      // Real PL body resolution with the live Rust loader/provider. Only the
      // database schemas and Root publication are controlled; no SQL executes.
      auto native_session = std::make_unique<ObSQLSessionInfo>();
      CHECK(native_session->test_init(1, 1, &arena) == OB_SUCCESS);
      CHECK(native_session->load_default_sys_variable(false, false) == OB_SUCCESS);
      CHECK(native_session->set_user(ObString::make_string("fixture"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
      native_session->set_priv_user_id(123);
      native_session->set_database_id(OB_SYS_DATABASE_ID);
      CHECK(native_session->set_default_database(ObString::make_string(OB_SYS_DATABASE_NAME)) == OB_SUCCESS);
      ObMySQLProxy proxy;
      ObResolverParams services;
      services.session_info_ = native_session.get(); services.sql_proxy_ = &proxy;
      ObSqlCtx sql_context; sql_context.session_info_ = native_session.get();
      ObExecContext execution(arena); execution.set_my_session(native_session.get()); execution.set_sql_ctx(&sql_context);
      LinkExecCtxGuard link(*native_session, execution);
      const int lookups = provider.resolves_;
      routine_create_test::run(package_root, services, sql_context, "rust_text_ops");
      routine_create_test::run(package_root, services, sql_context, nullptr, false, nullptr, true);
      routine_create_test::run(package_root, services, sql_context, nullptr, false, nullptr, false, true);
      CHECK(provider.resolves_ > lookups);
      const int memory_lookups = provider.resolves_;
      routine_create_test::run(package_root, services, sql_context, "rust_text_ops", true);
      CHECK(provider.resolves_ > memory_lookups);
      const int catalog_lookups = provider.resolves_;
      routine_create_test::run(package_root, services, sql_context, "rust_text_ops", false, &loader);
      CHECK(provider.resolves_ > catalog_lookups);
      ObPluginStatusSnapshot catalog_status;
      CHECK(loader.get_status("org.seekdb.rust-text", catalog_status) == OB_SUCCESS && catalog_status.lease_count_ == 0);
      const int native_only_lookups = provider.resolves_;
      routine_create_test::run(package_root, services, sql_context, "rust_text_native", false, &loader);
      CHECK(provider.resolves_ > native_only_lookups);
      routine_create_test::run(package_root, services, sql_context, nullptr, false, &loader, false, true);
      CHECK(loader.get_status("org.seekdb.rust-text", catalog_status) == OB_SUCCESS && catalog_status.lease_count_ == 0);
    }
    table_queries(provider, loader, arena, factory, *session);
    table_batches(provider, loader, arena, factory, *session);
    generator_batches(arena, factory, *session);
    table_sql_failure(provider, loader, arena, factory, *session);
    select_queries(provider, arena, factory, *session);
    set_branches(provider, arena, factory, *session);
    struct Case {
      const char *sql;
      const char *bytes;
      int64_t length; // -1 means a bytes result.
      int casts;
      int functions;
      int status = OB_SUCCESS;
      int64_t byte_size = -1;
      int bind_status = OB_SUCCESS;
    };
    for (const Case &test : {
        Case{"seekdb_rust_concat3('A中','/','🙂')", "A中/🙂", -1, 0, 1},
        Case{"seekdb_rust_concat3('','','')", "", -1, 0, 1},
        Case{"seekdb_rust_concat3('a\\0b','','z')", "a\0bz", -1, 0, 1, OB_SUCCESS, 4},
        Case{"seekdb_rust_concat3(NULL,seekdb_rust_text(X'FF'),'z')", nullptr, -1, 0, 0},
        Case{"seekdb_rust_concat3('a',NULL,seekdb_rust_text(X'FF'))", nullptr, -1, 0, 0},
        Case{"seekdb_rust_concat3(seekdb_rust_text(X'FF'),NULL,'z')", nullptr, -1, 0, 1, OB_INVALID_ARGUMENT},
        Case{"seekdb_rust_concat3_called('a',NULL,'z')", nullptr, -1, 0, 1},
        Case{"seekdb_rust_concat3_called(NULL,seekdb_rust_text(X'FF'),'z')", nullptr, -1, 0, 1, OB_INVALID_ARGUMENT},
        Case{"CAST('A中🙂' AS rust_utf8)", "A中🙂", -1, 1, 0},
        Case{"CONVERT('', `rust_utf8`)", "", -1, 1, 0},
        Case{"CAST(NULL AS rust_utf8)", nullptr, -1, 0, 0},
        Case{"CAST('z' AS rust_utf8) BETWEEN 1 AND 2", nullptr, -1, 0, 0, OB_SUCCESS, -1, OB_ERR_INVALID_TYPE_FOR_OP},
        Case{"CAST(CAST('hello' AS rust_utf8) AS rust_utf8)", "hello", -1, 1, 0},
        Case{"CAST(CAST('hello' AS rust_utf8) AS BINARY(3))", "hel", -1, 2, 0},
        Case{"CAST(seekdb_rust_text('hello') AS rust_utf8)", "hello", -1, 0, 1},
        Case{"seekdb_rust_char_count(CAST('A中🙂' AS rust_utf8))", "A中🙂", 3, 1, 1},
        Case{"seekdb_rust_char_count(CONVERT('', rust_utf8))", "", 0, 1, 1},
        Case{"seekdb_rust_char_count(CAST(NULL AS rust_utf8))", nullptr, 0, 0, 1},
        Case{"CONVERT('a\\0b', rust_utf8)", "a\0b", -1, 1, 0, OB_SUCCESS, 3},
        Case{"seekdb_rust_char_count(CONVERT('a\\0b', rust_utf8))", "a\0b", 3, 1, 1},
        Case{"seekdb_rust_identity(CAST('hello' AS rust_utf8))", "hello", -1, 1, 1},
        Case{"seekdb_rust_char_count(seekdb_rust_identity(CAST('A中🙂' AS rust_utf8)))", "A中🙂", 3, 1, 2},
        Case{"seekdb_rust_identity_bytes(CAST('hello' AS rust_utf8))", "hello", -1, 1, 1},
        Case{"seekdb_rust_char_count(CASE WHEN 1 THEN CAST('A中🙂' AS rust_utf8) ELSE NULL END)", "A中🙂", 3, 1, 1},
        Case{"seekdb_rust_char_count(CASE WHEN 0 THEN CAST('A中🙂' AS rust_utf8) ELSE NULL END)", nullptr, 0, 0, 1},
        Case{"CASE WHEN 1 THEN CAST('hello' AS rust_utf8) ELSE 'ok' END", "hello", -1, 2, 0},
        Case{"CASE WHEN 0 THEN CAST('hello' AS rust_utf8) ELSE 'ok' END", "ok", -1, 0, 0},
        Case{"seekdb_rust_identity(CASE WHEN 1 THEN seekdb_rust_text('hello') ELSE seekdb_rust_text('other') END)", "hello", -1, 0, 2},
        Case{"CASE WHEN 1 THEN seekdb_rust_char_count('abc') ELSE 7 END", "abc", 3, 0, 1},
        Case{"CASE WHEN 1 THEN 2 ELSE 3 END", "", 2, 0, 0},
        Case{"seekdb_rust_char_count(CASE WHEN 0 THEN seekdb_rust_text('hello') END)", nullptr, 0, 0, 1},
        Case{"CAST(CASE WHEN 0 THEN seekdb_rust_char_count('abc') ELSE 1.5 END AS CHAR)", "1.5", -1, 0, 0},
        Case{"CASE WHEN 1 THEN seekdb_rust_text('hello') ELSE 7 END", nullptr, 0, 0, 0,
            OB_SUCCESS, -1, OB_ERR_INVALID_TYPE_FOR_OP},
        Case{"seekdb_rust_text('z') < 7", nullptr, 0, 0, 0,
            OB_SUCCESS, -1, OB_ERR_INVALID_TYPE_FOR_OP},
        Case{"(seekdb_rust_text('z'), 1) < (seekdb_rust_text('aa'), 2)", "", 1, 0, 2},
        Case{"(1, 2) < (1, 3)", "", 1, 0, 0},
        Case{"CAST(CAST(X'FF' AS BINARY) AS rust_utf8) < CAST('ok' AS rust_utf8)",
            nullptr, 0, 1, 0, OB_INVALID_ARGUMENT},
        // Invalid UTF-8 must fail in the Rust cast; the outer function must
        // not run. A subsequent valid expression uses the same live module.
        Case{"seekdb_rust_char_count(CAST(CAST(X'FF' AS BINARY) AS rust_utf8))", nullptr, 0, 1, 0, OB_INVALID_ARGUMENT},
        Case{"seekdb_rust_char_count(CAST('ok' AS rust_utf8))", "ok", 2, 1, 1}}) {
      // Core CAST type inference requires the session's current execution
      // context, before codegen or evaluation starts.
      ObExecContext execution(arena); execution.set_my_session(session.get());
      CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
      ObSQLSessionInfo::ExecCtxSessionRegister register_execution(*session, &execution);
      const ParseNode *node = nullptr;
      CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(test.sql), session->get_charsets4parser(),
          arena, node, session->get_sql_mode()) == OB_SUCCESS && node);
      ObSEArray<ObQualifiedName, 1> columns;
      ObSEArray<ObVarInfo, 1> variables;
      ObSEArray<ObAggFunRawExpr *, 1> aggregates;
      ObSEArray<ObWinFunRawExpr *, 1> windows;
      ObSEArray<ObSubQueryInfo, 1> subqueries;
      ObSEArray<ObUDFInfo, 1> udfs;
      ObSEArray<ObOpRawExpr *, 1> operators;
      ObRawExpr *raw = nullptr;
      const int calls_before_bind = provider.functions_ + provider.casts_ + provider.decodes_ + provider.encodes_;
      const int built = ObRawExprUtils::build_raw_expr(factory, *session, *node, raw, columns,
          variables, aggregates, windows, subqueries, udfs, operators);
      if (test.bind_status != OB_SUCCESS) {
        CHECK(built == test.bind_status || (built == OB_SUCCESS && raw && raw->formalize(session.get()) == test.bind_status));
        CHECK(provider.functions_ + provider.casts_ + provider.decodes_ + provider.encodes_ == calls_before_bind);
        continue;
      }
      if (built != OB_SUCCESS) std::cerr << "Rust SQL resolve=" << built << " sql=" << test.sql << std::endl;
      CHECK(built == OB_SUCCESS && raw && columns.empty());
      const int formalized = raw->formalize(session.get());
      if (formalized != OB_SUCCESS) std::cerr << "Rust SQL formalize=" << formalized << " sql=" << test.sql << std::endl;
      CHECK(formalized == OB_SUCCESS);
      const int bound_resolves = provider.resolves_;
      CHECK(raw->deduce_type(session.get()) == OB_SUCCESS);
      CHECK(provider.resolves_ == bound_resolves);
      ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0);
      ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
      ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
      CHECK(provider.resolves_ == bound_resolves);
      CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
      CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
      ObEvalCtx eval(execution);
      ObExpr *root = nullptr;
      ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS && root);
      const int resolved = provider.resolves_, casts = provider.casts_, functions = provider.functions_;
      ObDatum *result = nullptr;
      const int evaluated = root->eval(eval, result);
      if (evaluated != test.status) std::cerr << "Rust SQL eval=" << evaluated << " sql=" << test.sql << std::endl;
      CHECK(evaluated == test.status);
      if (evaluated == OB_SUCCESS) {
        CHECK(result);
        if (!test.bytes) CHECK(result->is_null());
        else if (test.length >= 0) CHECK(!result->is_null() && result->get_int() == test.length);
        else CHECK(!result->is_null() && result->get_string() ==
            ObString(test.byte_size >= 0 ? test.byte_size : std::strlen(test.bytes), test.bytes));
      }
      CHECK(provider.resolves_ == resolved && provider.casts_ == casts + test.casts);
      CHECK(provider.functions_ == functions + test.functions && provider.decodes_ == 0 && provider.encodes_ == 0);
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(*session, nullptr);
    }
    rust_stored_type_test::run(provider, loader, arena, factory, *session);
    rust_aggregate_test::run(provider, loader, arena, factory, *session);
    rust_aggregate_plan_test::run(provider, loader, arena);
    rust_sort_plan_test::run(provider, loader, arena);
    CHECK(provider.natural_candidate_calls_ > 0);
    for (bool native_rust : {false, true}) {
      native_activation_test::Observation built_observation;
      built_observation.expected_services = built_observation.expected_extensions = 1;
      if (native_rust) { built_observation.expected_services = 5; built_observation.expected_extensions = 7; }
      auto built_guard = std::make_shared<native_activation_test::TestGuard>(built_observation);
      ObPluginLoader built_loader;
      CHECK(built_loader.init(native_path.substr(0, native_slash),
          std::make_shared<native_activation_test::TestVerifier>(false, false, false, false, native_rust),
          built_guard, built_guard, built_observation.registry) == OB_SUCCESS);
      CHECK(built_loader.load(native_rust ? "candidate_native.so" : "candidate_9.so") == OB_SUCCESS);
      bool subproblem_available = true;
      CHECK(built_loader.plugin_join_hooks_available(subproblem_available) == OB_SUCCESS);
      CHECK(subproblem_available == native_rust);
      provider.candidate_loader_ = &built_loader;
      provider.candidate_build_enabled_ = true;
      provider.candidate_custom_enabled_ = native_rust;
      rust_sort_plan_test::run_built(provider, loader, arena);
      provider.candidate_build_enabled_ = false;
      provider.candidate_custom_enabled_ = false;
      provider.candidate_loader_ = &candidate_loader;
      CHECK(built_loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
      CHECK(built_observation.registry->extension_count() == 0 && built_observation.registry->service_count() == 0);
    }
    rust_between_test::run(provider, loader, arena, factory, *session);
    rust_in_test::run(provider, loader, arena, factory, *session);
    rust_simple_case_test::run(provider, loader, arena, factory, *session);
    rust_row_comparison_test::run(provider, loader, arena, factory, *session);
    rust_row_in_test::run(provider, loader, arena, factory, *session);
    rust_type_batch_test::run(provider, loader, arena, factory, *session);
    rust_function_batch_test::run(provider, loader, arena, factory, *session);
    rust_stored_type_test::table_predicates(provider, arena, factory, *session);
    CHECK(provider.candidate_calls_ > 0);
  }
  CHECK(candidate_loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  CHECK(candidate_observation.registry->extension_count() == 0 && candidate_observation.registry->service_count() == 0);
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  CHECK(observation.registry->extension_count() == 0 && observation.registry->service_count() == 0);
}
} // namespace rust_sql_expression_test
#endif
