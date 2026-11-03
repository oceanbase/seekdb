// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real aggregate expression deduction and processor, supplied input frames,
// production loader and installed Rust TYPE/comparator. Not a live SQL server.
#ifndef SEEKDB_TEST_RUST_AGGREGATE_FIXTURE_H_
#define SEEKDB_TEST_RUST_AGGREGATE_FIXTURE_H_
#include "sql/engine/aggregate/ob_aggregate_processor.h"
namespace rust_aggregate_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;
template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
    ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  constexpr int MAX = 1031;
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_type_by_id(rust_stored_type_test::TYPE_ID, binding) == OB_SUCCESS);
  plugin_projection_test::InrowOnlyLobService lob_service;
  for (int variant = 0; variant < 5; ++variant) {
    const char *expressions[] = {"MIN(payload)", "MAX(payload)", "MIN(DISTINCT payload)",
        "MAX(seekdb_rust_identity(payload))", "MIN(CAST(payload AS BINARY))"};
    ObSqlCtx sql; sql.session_info_ = &session;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql);
    execution.set_lob_read_service(&lob_service);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(expressions[variant]),
        session.get_charsets4parser(), arena, node, session.get_sql_mode()) == OB_SUCCESS && node);
    ObSEArray<ObQualifiedName, 1> columns;
    ObSEArray<ObVarInfo, 1> variables;
    ObSEArray<ObAggFunRawExpr *, 1> aggregates;
    ObSEArray<ObWinFunRawExpr *, 1> windows;
    ObSEArray<ObSubQueryInfo, 1> subqueries;
    ObSEArray<ObUDFInfo, 1> udfs;
    ObSEArray<ObOpRawExpr *, 1> operators;
    ObRawExpr *raw = nullptr;
    CHECK(ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
        variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS && raw);
    CHECK(columns.count() == 1 && aggregates.count() == 1 && raw == aggregates.at(0));
    auto *column = static_cast<ObColumnRefRawExpr *>(columns.at(0).ref_expr_);
    oceanbase::share::schema::ObColumnSchemaV2 schema;
    schema.set_table_id(123); schema.set_column_id(456); schema.set_data_type(ObLongTextType);
    schema.set_collation_type(CS_TYPE_BINARY);
    CHECK(schema.set_column_name("payload") == OB_SUCCESS);
    ObSEArray<ObString, 7> fields;
    for (const char *field : {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2, "rust_stored_utf8",
        rust_stored_type_test::TYPE_ID, "org.seekdb.rust-text", "0", "org.seekdb.rust-text.stored-utf8.v1", "1"})
      CHECK(fields.push_back(ObString::make_string(field)) == OB_SUCCESS);
    CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
    CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
    column->set_ref_id(123,456);
    const int formalized = raw->formalize(&session);
    if (formalized != OB_SUCCESS) std::cerr << "aggregate formalize=" << formalized << " sql=" << expressions[variant] << std::endl;
    CHECK(formalized == OB_SUCCESS);
    if (variant != 4) CHECK(raw->get_plugin_type() && !raw->get_plugin_type()->stored_ &&
        raw->get_plugin_type()->logical_id_ == ObString::make_string(rust_stored_type_test::TYPE_ID));
    CHECK(!static_cast<ObAggFunRawExpr *>(raw)->is_param_distinct());
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0); generator.set_batch_size(MAX);
    ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr, *input = nullptr, *argument = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy->get_param_expr(0), outputs, argument) == OB_SUCCESS && argument);
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!input); input = &expr; }
    CHECK(input && input->is_batch_result());
    if (variant != 4) {
      const auto *info = dynamic_cast<const PluginTypeValueExtraInfo *>(argument->extra_info_);
      CHECK(info && info->mode_ == PluginTypeValueExtraInfo::ORDERED && info->ordering_);
      std::vector<char> wire(info->get_serialize_size()); int64_t pos = 0;
      CHECK(info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == wire.size());
      PluginTypeValueExtraInfo restored(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE); pos = 0;
      CHECK(restored.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS && restored.valid());
      CHECK(restored.ordering_ != info->ordering_ && restored.ordering_->binding_.catalog_epoch == binding.catalog_epoch);
      for (size_t length = 0; length < wire.size(); ++length) {
        PluginTypeValueExtraInfo truncated(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE); pos = 0;
        CHECK(truncated.deserialize(wire.data(), length, pos) != OB_SUCCESS && !truncated.valid());
      }
      ObIExprExtraInfo *owned = nullptr;
      CHECK(info->deep_copy(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE, owned) == OB_SUCCESS);
      CHECK(static_cast<PluginTypeValueExtraInfo *>(owned)->ordering_ != info->ordering_);
    }
    ObFixedArray<ObAggrInfo, ObIAllocator> infos(arena);
    CHECK(infos.prepare_allocate(1) == OB_SUCCESS);
    auto &info = infos.at(0); info.set_allocator(&arena); info.expr_ = root;
    CHECK(info.param_exprs_.init(1) == OB_SUCCESS && info.param_exprs_.push_back(argument) == OB_SUCCESS);
    ObMonitorNode monitor;
    ObAggregateProcessor processor(eval, infos, "PluginAggrTest", monitor);
    CHECK(processor.init() == OB_SUCCESS && processor.init_one_group() == OB_SUCCESS);
    ObAggregateProcessor::GroupRow *group = nullptr;
    CHECK(processor.get_group_row(0, group) == OB_SUCCESS && group);
    auto *skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(MAX))); CHECK(skip);
    std::vector<std::vector<char>> lobs(MAX);
    const std::string texts[] = {"z", "aa", "🙂", "", "bbb", std::string("a\0b", 3)};
    const auto fill = [&](int size, bool nulls) {
      for (auto &expr : frame.rt_exprs_) {
        expr.get_eval_info(eval).evaluated_ = false;
        if (expr.is_batch_result()) expr.get_evaluated_flags(eval).reset(MAX);
      }
      for (int i = 0; i < size; ++i) {
        auto &datum = input->locate_batch_datums(eval)[i];
        if (nulls || i % 6 == 3) datum.set_null();
        else {
          const auto &text = texts[i % 6];
          rust_stored_type_test::Encoded encoded;
          seekdb_plugin_execution_value_v1_t value{}; value.struct_size = sizeof(value);
          value.type_id = rust_stored_type_test::TYPE_ID;
          value.data = reinterpret_cast<const uint8_t *>(text.data()); value.data_size = text.size();
          seekdb_plugin_execution_context_v1_t context{}; context.struct_size = sizeof(context);
          context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&encoded); context.emit_result = rust_stored_type_test::Encoded::emit;
          CHECK(loader.encode_bound_type(binding, &context, &value) == OB_SUCCESS && encoded.emitted);
          lobs[i].assign(sizeof(ObLobCommon) + encoded.bytes.size(), 0);
          auto *header = new (lobs[i].data()) ObLobCommon(); std::memcpy(header->buffer_, encoded.bytes.data(), encoded.bytes.size());
          datum.set_string(ObString(lobs[i].size(), lobs[i].data()));
        }
        input->get_evaluated_flags(eval).set(i);
      }
      input->obj_meta_.set_has_lob_header();
      input->get_eval_info(eval).evaluated_ = true; input->get_eval_info(eval).projected_ = true;
      input->get_eval_info(eval).cnt_ = size; skip->reset(MAX);
    };
    ObEvalCtx::BatchInfoScopeGuard scope(eval);
    fill(6, false); scope.set_batch_size(6);
    for (int i = 0; i < 6; ++i) skip->set(i);
    ObBatchRows skipped; skipped.size_ = 6; skipped.skip_ = skip;
    const int untouched = provider.decodes_ + provider.comparisons_ + provider.functions_;
    CHECK(processor.eval_aggr_param_batch(skipped) == OB_SUCCESS && processor.process_batch(*group, skipped, 0, 6) == OB_SUCCESS);
    CHECK(provider.decodes_ + provider.comparisons_ + provider.functions_ == untouched);
    for (int size : {6, 3, MAX}) {
      fill(size, false); scope.set_batch_size(size);
      skip->set(2);
      ObBatchRows rows; rows.size_ = size; rows.skip_ = skip;
      const int decoded = provider.decodes_, compared = provider.comparisons_;
      const int scalar = provider.scalar_functions_;
      CHECK(processor.eval_aggr_param_batch(rows) == OB_SUCCESS);
      CHECK(processor.process_batch(*group, rows, 0, size) == OB_SUCCESS);
      int nonnull = 0; for (int i = 0; i < size; ++i) if (!skip->at(i) && i % 6 != 3) ++nonnull;
      CHECK(provider.decodes_ == decoded + nonnull && provider.scalar_functions_ == scalar);
      if (variant != 4) CHECK(provider.comparisons_ > compared);
      else CHECK(provider.comparisons_ == compared);
      for (auto &lob : lobs) std::fill(lob.begin(), lob.end(), 'x');
      scope.set_batch_size(1); scope.set_batch_idx(0);
      CHECK(processor.collect() == OB_SUCCESS);
      const auto &result = root->locate_expr_datum(eval);
      const std::string expected = variant == 4 ? texts[5] : (variant == 1 || variant == 3) ? texts[4] : texts[0];
      CHECK(!result.is_null() && result.get_string() == ObString(expected.size(), expected.data()));
      CHECK(provider.resolves_ == resolves);
    }
    CHECK(processor.reuse_group(0) == OB_SUCCESS);
    CHECK(processor.get_group_row(0, group) == OB_SUCCESS && group);
    fill(6, true); scope.set_batch_size(6);
    ObBatchRows rows; rows.size_ = 6; rows.skip_ = skip;
    const int compared = provider.comparisons_, decoded = provider.decodes_;
    CHECK(processor.eval_aggr_param_batch(rows) == OB_SUCCESS && processor.process_batch(*group, rows, 0, 6) == OB_SUCCESS);
    scope.set_batch_size(1); scope.set_batch_idx(0);
    CHECK(processor.collect() == OB_SUCCESS && root->locate_expr_datum(eval).is_null());
    CHECK(provider.decodes_ == decoded && provider.comparisons_ == compared);
    // The row path and rollup merge use the same fixed comparator. Each group
    // sees a different half of the input, then merges without re-decoding.
    CHECK(processor.reuse_group(0) == OB_SUCCESS && processor.init_one_group(1) == OB_SUCCESS);
    ObAggregateProcessor::GroupRow *second = nullptr;
    CHECK(processor.get_group_row(1, second) == OB_SUCCESS && second);
    fill(6, false); scope.set_batch_size(6);
    const int row_decodes = provider.decodes_, row_compares = provider.comparisons_;
    for (int i = 0; i < 6; ++i) {
      scope.set_batch_idx(i);
      auto &target = i < 3 ? *group : *second;
      CHECK((i == 0 || i == 3 ? processor.prepare(target) : processor.process(target)) == OB_SUCCESS);
    }
    CHECK(provider.decodes_ == row_decodes + 5);
    if (variant != 4) CHECK(provider.comparisons_ > row_compares);
    const int merge_decodes = provider.decodes_;
    CHECK(processor.rollup_base_process(second, group, nullptr) == OB_SUCCESS);
    CHECK(provider.decodes_ == merge_decodes);
    scope.set_batch_size(1); scope.set_batch_idx(0);
    CHECK(processor.collect() == OB_SUCCESS);
    const std::string expected = variant == 4 ? texts[5] : (variant == 1 || variant == 3) ? texts[4] : texts[0];
    CHECK(root->locate_expr_datum(eval).get_string() == ObString(expected.size(), expected.data()));
    if (variant == 0) {
      // Inject an invalid already-decoded operand to isolate comparator error
      // handling from codec validation. No failed comparison replaces state.
      fill(1, false); scope.set_batch_size(1); scope.set_batch_idx(0);
      ObDatum *value = nullptr; CHECK(argument->eval(eval, value) == OB_SUCCESS && value);
      const char invalid[] = {char(0xff)}; value->set_string(ObString(1, invalid));
      CHECK(processor.process(*group) == OB_INVALID_ARGUMENT);
      CHECK(processor.collect() == OB_SUCCESS && root->locate_expr_datum(eval).get_string() == ObString::make_string("z"));
      fill(1, false); CHECK(argument->eval(eval, value) == OB_SUCCESS);
      class CancelAfterComparison final : public ObIExtraStatusCheck {
      public:
        explicit CancelAfterComparison(const int &calls) : calls_(calls), before_(calls) {}
        const char *name() const override { return "plugin-aggregate-cancel"; }
        int check() const override { return calls_ == before_ ? OB_SUCCESS : OB_TIMEOUT; }
      private:
        const int &calls_; int before_;
      } cancel(provider.comparisons_);
      { ObIExtraStatusCheck::Guard guard(execution, cancel);
        CHECK(processor.process(*group) == OB_TIMEOUT); }
      auto *ordered = dynamic_cast<PluginTypeValueExtraInfo *>(argument->extra_info_);
      CHECK(ordered && ordered->ordering_);
      ++ordered->ordering_->binding_.catalog_epoch;
      CHECK(processor.process(*group) == OB_INVALID_DATA);
      --ordered->ordering_->binding_.catalog_epoch;
      CHECK(processor.process(*group) == OB_SUCCESS);
    }
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
  }
}
}
#endif
