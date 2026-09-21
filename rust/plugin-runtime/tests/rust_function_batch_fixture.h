// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual SQL batch frames -> production loader -> installed Rust DSO.
#ifndef SEEKDB_TEST_RUST_FUNCTION_BATCH_FIXTURE_H_
#define SEEKDB_TEST_RUST_FUNCTION_BATCH_FIXTURE_H_
namespace rust_function_batch_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;
template <typename Provider>
void stored_arguments(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
    ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  constexpr int MAX = 1031;
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_type_by_id(rust_stored_type_test::TYPE_ID, binding) == OB_SUCCESS);
  plugin_projection_test::InrowOnlyLobService lob_service;
  for (int variant = 0; variant < 3; ++variant) {
    const char *expressions[] = {
        "seekdb_rust_char_count(payload)",
        "seekdb_rust_concat3(payload,'/',seekdb_rust_text(CASE WHEN payload IS NULL THEN X'FF' ELSE 'z' END))",
        "seekdb_rust_concat3_called(payload,'/',seekdb_rust_text(CASE WHEN payload IS NULL THEN X'FF' ELSE 'z' END))"};
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
    CHECK(!columns.empty());
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
    for (int64_t i = 1; i < columns.count(); ++i)
      CHECK(ObRawExprUtils::replace_ref_column(raw, columns.at(i).ref_expr_, column) == OB_SUCCESS);
    CHECK(raw->formalize(&session) == OB_SUCCESS);
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS);
    bool transformed = false;
    CHECK(ObTransformPreProcess::transform_expr(factory, session, copy, transformed) == OB_SUCCESS);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0); generator.set_batch_size(MAX);
    ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr, *input = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    const auto *info = dynamic_cast<const PluginFunctionExtraInfo *>(root->extra_info_);
    CHECK(info && info->stored().empty());
    CHECK(root->args_[1]->type_ == T_FUN_SYS_PLUGIN_TYPE_VALUE);
    const auto *decoder = dynamic_cast<const PluginTypeValueExtraInfo *>(root->args_[1]->extra_info_);
    CHECK(decoder && decoder->mode_ == PluginTypeValueExtraInfo::DECODE);
    seekdb_plugin_sql_binding_v1_t root_binding{};
    CHECK(info->binding(root_binding) == OB_SUCCESS);
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!input); input = &expr; }
    CHECK(input && input->is_batch_result());
    auto *skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(MAX))); CHECK(skip);
    std::vector<std::string> texts(MAX);
    std::vector<std::vector<char>> lobs(MAX);
    const auto fill = [&](int size) {
      for (auto &expr : frame.rt_exprs_) {
        expr.get_eval_info(eval).evaluated_ = false;
        if (expr.is_batch_result()) expr.get_evaluated_flags(eval).reset(MAX);
      }
      for (int i = 0; i < size; ++i) {
        texts[i] = i % 4 == 0 ? "A中🙂" : i % 4 == 1 ? "" : std::string("a\0b", 3);
        auto &datum = input->locate_batch_datums(eval)[i];
        if (i % 4 == 3) datum.set_null();
        else {
          rust_stored_type_test::Encoded encoded;
          seekdb_plugin_execution_value_v1_t value{}; value.struct_size = sizeof(value);
          value.type_id = rust_stored_type_test::TYPE_ID;
          value.data = reinterpret_cast<const uint8_t *>(texts[i].data()); value.data_size = texts[i].size();
          seekdb_plugin_execution_context_v1_t context{}; context.struct_size = sizeof(context);
          context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&encoded);
          context.emit_result = rust_stored_type_test::Encoded::emit;
          CHECK(loader.encode_bound_type(binding, &context, &value) == OB_SUCCESS && encoded.emitted);
          lobs[i].assign(sizeof(ObLobCommon) + encoded.bytes.size(), 0);
          auto *header = new (lobs[i].data()) ObLobCommon();
          std::memcpy(header->buffer_, encoded.bytes.data(), encoded.bytes.size());
          datum.set_string(ObString(lobs[i].size(), lobs[i].data()));
        }
        input->get_evaluated_flags(eval).set(i);
      }
      input->obj_meta_.set_has_lob_header();
      input->get_eval_info(eval).evaluated_ = true;
      input->get_eval_info(eval).projected_ = true;
      input->get_eval_info(eval).cnt_ = size;
      skip->reset(MAX);
    };
    for (int size : {6, 3, MAX}) {
      fill(size);
      const int decoded = provider.decodes_, scalar = provider.scalar_functions_;
      const int calls = provider.batch_calls(root_binding.sql_name);
      for (int i = 0; i < size; ++i) skip->set(i);
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
      CHECK(provider.decodes_ == decoded && provider.batch_calls(root_binding.sql_name) == calls);
      skip->reset(MAX); skip->set(1);
      if (variant == 2) for (int i = 0; i < size; ++i) if (i % 4 == 3) skip->set(i);
      int needed = 0, nonnull = 0;
      for (int i = 0; i < size; ++i) if (!skip->at(i)) { ++needed; if (i % 4 != 3) ++nonnull; }
      const int evaluated = root->eval_batch(eval, *skip, size);
      if (evaluated != OB_SUCCESS)
        std::cerr << "stored function batch variant=" << variant << " size=" << size
                  << " ret=" << evaluated << " decodes=" << provider.decodes_ - decoded << std::endl;
      CHECK(evaluated == OB_SUCCESS);
      CHECK(provider.decodes_ == decoded + nonnull && provider.scalar_functions_ == scalar);
      CHECK(provider.batch_calls(root_binding.sql_name) == calls + ((variant == 0 ? needed : nonnull) + 1023) / 1024);
      for (int i = 0; i < size; ++i) if (!skip->at(i)) {
        const auto &datum = root->locate_batch_datums(eval)[i];
        CHECK(root->get_evaluated_flags(eval).at(i));
        if (i % 4 == 3) CHECK(datum.is_null());
        else if (variant == 0) CHECK(!datum.is_null() && datum.get_int() == (i % 4 == 1 ? 0 : 3));
        else {
          const std::string expected = texts[i] + "/z";
          CHECK(!datum.is_null() && datum.get_string() == ObString(expected.size(), expected.data()));
        }
      }
      const int functions = provider.functions_;
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
      CHECK(provider.decodes_ == decoded + nonnull && provider.functions_ == functions);
      skip->unset(1);
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
      CHECK(provider.decodes_ == decoded + nonnull + 1 && provider.scalar_functions_ == scalar);
      CHECK(provider.resolves_ == resolves);
      if (variant == 2 && size >= 4) {
        const int functions = provider.functions_, batches = provider.batch_calls(root_binding.sql_name);
        skip->unset(3);
        CHECK(root->eval_batch(eval, *skip, size) == OB_INVALID_ARGUMENT);
        CHECK(provider.decodes_ == decoded + nonnull + 1 && provider.functions_ == functions + 1);
        CHECK(provider.batch_calls(root_binding.sql_name) == batches);
        for (int i = 0; i < size; ++i) CHECK(!root->get_evaluated_flags(eval).at(i));
      }
    }
    // An invalid stored header must fail before any later nested function,
    // even for a called-on-NULL parent. It is never decoded twice or retried.
    fill(6);
    lobs[0][sizeof(ObLobCommon)] = 'X';
    const int decoded = provider.decodes_, functions = provider.functions_;
    CHECK(root->eval_batch(eval, *skip, 6) == OB_INVALID_ARGUMENT);
    CHECK(provider.decodes_ == decoded + 1 && provider.functions_ == functions);
    for (int i = 0; i < 6; ++i) CHECK(!root->get_evaluated_flags(eval).at(i));
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
  }
}

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
    ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  constexpr int MAX = 1031;
  stored_arguments(provider, loader, arena, factory, session);
  for (int variant = 0; variant < 10; ++variant) {
    const char *expressions[] = {"seekdb_rust_char_count(payload)",
        "seekdb_rust_char_count(CAST(payload AS rust_utf8))",
        "seekdb_rust_text(payload)", "seekdb_rust_identity(CAST(payload AS rust_utf8))",
        "seekdb_rust_concat3(payload,'/',payload)",
        "seekdb_rust_concat3_called(payload,'/',payload)",
        "seekdb_rust_concat3(payload,'/',seekdb_rust_text(CASE WHEN payload IS NULL THEN X'FF' ELSE payload END))",
        "seekdb_rust_concat3_called(payload,'/',seekdb_rust_text(CASE WHEN payload IS NULL THEN X'FF' ELSE payload END))",
        "seekdb_rust_char_count(seekdb_rust_concat3_called(payload,'/',payload))",
        "seekdb_rust_char_count(CAST(seekdb_rust_concat3_called(payload,'/',payload) AS rust_utf8))"};
    ObSqlCtx sql; sql.session_info_ = &session;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql);
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
    CHECK(!columns.empty());
    auto *column = static_cast<ObColumnRefRawExpr *>(columns.at(0).ref_expr_);
    oceanbase::share::schema::ObColumnSchemaV2 schema;
    schema.set_table_id(123); schema.set_column_id(456); schema.set_data_type(ObVarcharType);
    schema.set_collation_type(CS_TYPE_UTF8MB4_BIN); schema.set_data_length(20000000);
    CHECK(schema.set_column_name("payload") == OB_SUCCESS);
    CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
    column->set_ref_id(123,456);
    for (int64_t i = 1; i < columns.count(); ++i)
      CHECK(ObRawExprUtils::replace_ref_column(raw, columns.at(i).ref_expr_, column) == OB_SUCCESS);
    CHECK(raw->formalize(&session) == OB_SUCCESS);
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS);
    bool transformed = false;
    CHECK(ObTransformPreProcess::transform_expr(factory, session, copy, transformed) == OB_SUCCESS);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0); generator.set_batch_size(MAX);
    ObExprFrameInfo frame(arena);
    CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr, *input = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    CHECK(root->is_batch_result() && root->eval_batch_func_ == PluginFunctionExpr::evaluate_batch);
    seekdb_plugin_sql_binding_v1_t root_binding{};
    CHECK(dynamic_cast<const PluginFunctionExtraInfo *>(root->extra_info_)->binding(root_binding) == OB_SUCCESS);
    const auto calls_for_root = [&]() { return provider.batch_calls(root_binding.sql_name); };
    const auto rows_for_root = [&]() { return provider.batch_rows(root_binding.sql_name); };
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!input); input = &expr; }
    CHECK(input && input->is_batch_result());
    auto *skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(MAX))); CHECK(skip);
    std::vector<std::string> bytes(MAX);
    const auto fill = [&](int size) {
      for (auto &expr : frame.rt_exprs_) {
        expr.get_eval_info(eval).evaluated_ = false;
        if (expr.is_batch_result()) expr.get_evaluated_flags(eval).reset(MAX);
      }
      for (int i = 0; i < size; ++i) {
        bytes[i] = i % 4 == 0 ? "A中🙂" : i % 4 == 1 ? "" : std::string("a\0b", 3);
        auto &datum = input->locate_batch_datums(eval)[i];
        if (i % 4 == 3) datum.set_null();
        else datum.set_string(ObString(bytes[i].size(), bytes[i].data()));
        input->get_evaluated_flags(eval).set(i);
      }
      input->get_eval_info(eval).evaluated_ = true;
      input->get_eval_info(eval).projected_ = true;
      input->get_eval_info(eval).cnt_ = size;
      skip->reset(MAX);
    };
    const auto results = [&](int size) {
      for (int i = 0; i < size; ++i) if (!skip->at(i)) {
        CHECK(root->get_evaluated_flags(eval).at(i));
        const auto &datum = root->locate_batch_datums(eval)[i];
        if (i % 4 == 3) CHECK(datum.is_null());
        else if (variant < 2 || variant >= 8)
          CHECK(!datum.is_null() && datum.get_int() == (variant >= 8 ? (i % 4 == 1 ? 1 : 7) : (i % 4 == 1 ? 0 : 3)));
        else {
          const std::string expected = variant >= 4 ? bytes[i] + "/" + bytes[i] : bytes[i];
          CHECK(!datum.is_null() && datum.get_string() == ObString(expected.size(), expected.data()));
        }
      }
    };
    for (int size : {6, 3, MAX}) {
      fill(size);
      int calls = calls_for_root(), rows = rows_for_root();
      for (int i = 0; i < size; ++i) skip->set(i);
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
      CHECK(calls_for_root() == calls && rows_for_root() == rows);
      skip->reset(MAX); skip->set(1);
      if (variant == 7) for (int i = 0; i < size; ++i) if (i % 4 == 3) skip->set(i);
      int expected_rows = 0;
      for (int i = 0; i < size; ++i)
        if (!skip->at(i) && ((variant != 4 && variant != 6) || i % 4 != 3)) ++expected_rows;
      const int functions = provider.functions_;
      const int scalar = provider.scalar_functions_;
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
      CHECK(calls_for_root() == calls + (expected_rows + 1023) / 1024);
      CHECK(rows_for_root() == rows + expected_rows);
      if (variant >= 6) CHECK(provider.scalar_functions_ == scalar);
      if (variant >= 4)
        CHECK(provider.functions_ == functions + expected_rows * (variant >= 6 ? 2 : 1));
      results(size);
      calls = calls_for_root();
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS && calls_for_root() == calls);
      skip->reset(MAX);
      if (variant == 7) for (int i = 0; i < size; ++i) if (i % 4 == 3) skip->set(i);
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS && calls_for_root() == calls + 1);
      results(size);
      CHECK(provider.resolves_ == resolves);
      // String outputs cannot borrow the input column's buffers.
      if (variant >= 2 && variant < 8) {
        const std::string saved = variant >= 4 ? bytes[0] + "/" + bytes[0] : bytes[0];
        std::fill(bytes[0].begin(), bytes[0].end(), 'x');
        CHECK(root->locate_batch_datums(eval)[0].get_string() == ObString(saved.size(), saved.data()));
      }
      if (variant == 7 && size >= 4) {
        // Called-on-NULL must evaluate the third argument. Its invalid UTF-8
        // errors before concat enters the DSO; strict variant 6 skips it.
        const int functions = provider.functions_, batches = calls_for_root();
        skip->unset(3);
        CHECK(root->eval_batch(eval, *skip, size) == OB_INVALID_ARGUMENT);
        CHECK(provider.functions_ == functions + 1 && calls_for_root() == batches);
        for (int i = 0; i < size; ++i) CHECK(!root->get_evaluated_flags(eval).at(i));
      }
    }
    if (variant == 4 || variant == 6) {
      fill(6);
      for (int i = 0; i < 6; ++i) input->locate_batch_datums(eval)[i].set_null();
      const int functions = provider.functions_, batches = provider.function_batches_;
      CHECK(root->eval_batch(eval, *skip, 6) == OB_SUCCESS);
      CHECK(provider.functions_ == functions && provider.function_batches_ == batches);
      for (int i = 0; i < 6; ++i) CHECK(root->get_evaluated_flags(eval).at(i)
          && root->locate_batch_datums(eval)[i].is_null());
    }
    if (variant == 4) {
      // Both references to the same column are real argument payloads. Count
      // all three arguments, not just one column buffer, against the ABI budget.
      constexpr uint64_t PART = UINT64_C(4194304), ROW = 2 * PART + 1;
      std::string large(PART, 'x');
      fill(9);
      for (int i = 0; i < 9; ++i)
        input->locate_batch_datums(eval)[i].set_string(ObString(large.size(), large.data()));
      const size_t shape = provider.function_batch_shapes_.size();
      const int calls = provider.function_batches_, functions = provider.functions_;
      CHECK(root->eval_batch(eval, *skip, 9) == OB_SUCCESS);
      CHECK(provider.function_batches_ == calls + 2 && provider.functions_ == functions + 9);
      CHECK(provider.function_batch_shapes_[shape].first == 7
          && provider.function_batch_shapes_[shape].second == 7 * ROW);
      CHECK(provider.function_batch_shapes_[shape + 1].first == 2
          && provider.function_batch_shapes_[shape + 1].second == 2 * ROW);
      for (int i = 0; i < 9; ++i) {
        const auto &datum = root->locate_batch_datums(eval)[i];
        CHECK(root->get_evaluated_flags(eval).at(i) && !datum.is_null());
        const auto bytes = datum.get_string();
        CHECK(bytes.length() == ROW && bytes.ptr()[PART] == '/');
        CHECK(std::memcmp(bytes.ptr(), large.data(), PART) == 0
            && std::memcmp(bytes.ptr() + PART + 1, large.data(), PART) == 0);
      }
      CHECK(root->eval_batch(eval, *skip, 9) == OB_SUCCESS && provider.function_batches_ == calls + 2);
      // Individually legal arguments may produce a result larger than the
      // per-value output bound. Reject once; never retry the provider.
      fill(1); large.resize(2 * PART, 'x');
      input->locate_batch_datums(eval)[0].set_string(ObString(large.size(), large.data()));
      const int outputs = provider.function_batch_outputs_;
      CHECK(root->eval_batch(eval, *skip, 1) == OB_INVALID_ARGUMENT);
      CHECK(provider.function_batches_ == calls + 3 && provider.function_batch_outputs_ == outputs);
      CHECK(!root->get_evaluated_flags(eval).at(0) && root->locate_batch_datums(eval)[0].is_null());
    }
    if (variant == 0) {
      fill(MAX);
      bytes[MAX - 1].assign(1, char(0xff));
      input->locate_batch_datums(eval)[MAX - 1].set_string(ObString(1, bytes[MAX - 1].data()));
      // First chunk succeeds inside the loader; later invalid UTF-8 invalidates
      // the whole SQL batch, including every result flag from the first chunk.
      CHECK(root->eval_batch(eval, *skip, MAX) == OB_INVALID_ARGUMENT);
      for (int i = 0; i < MAX; ++i) CHECK(!root->get_evaluated_flags(eval).at(i)
          && root->locate_batch_datums(eval)[i].is_null());
      fill(6); skip->set(2);
      CHECK(root->eval_batch(eval, *skip, 6) == OB_SUCCESS);
      bytes[2].assign(1, char(0xff));
      input->locate_batch_datums(eval)[2].set_string(ObString(1, bytes[2].data()));
      skip->reset(MAX);
      CHECK(root->eval_batch(eval, *skip, 6) == OB_INVALID_ARGUMENT);
      for (int i = 0; i < 6; ++i) CHECK(!root->get_evaluated_flags(eval).at(i));
      fill(6);
      class Cancelled final : public ObIExtraStatusCheck {
      public:
        const char *name() const override { return "rust-function-batch"; }
        int check() const override { return OB_TIMEOUT; }
      } cancelled;
      const int calls = provider.function_batches_;
      { ObIExtraStatusCheck::Guard guard(execution, cancelled);
        CHECK(root->eval_batch(eval, *skip, 6) == OB_TIMEOUT); }
      CHECK(provider.function_batches_ == calls);
      for (int i = 0; i < 6; ++i) CHECK(!root->get_evaluated_flags(eval).at(i));
      CHECK(root->eval_batch(eval, *skip, 6) == OB_SUCCESS);
      results(6);
      fill(6);
      class DuringOutput final : public ObIExtraStatusCheck {
      public:
        DuringOutput(const int &outputs, int limit) : outputs_(outputs), limit_(limit) {}
        const char *name() const override { return "rust-function-batch-output-cancel"; }
        int check() const override { return outputs_ >= limit_ ? OB_TIMEOUT : OB_SUCCESS; }
      private:
        const int &outputs_; int limit_;
      } during_output(provider.function_batch_outputs_, provider.function_batch_outputs_ + 2);
      const int emitted = provider.function_batch_outputs_;
      { ObIExtraStatusCheck::Guard guard(execution, during_output);
        CHECK(root->eval_batch(eval, *skip, 6) == OB_TIMEOUT); }
      CHECK(provider.function_batch_outputs_ == emitted + 2);
      for (int i = 0; i < 6; ++i) CHECK(!root->get_evaluated_flags(eval).at(i)
          && root->locate_batch_datums(eval)[i].is_null());
      CHECK(root->eval_batch(eval, *skip, 6) == OB_SUCCESS);
      results(6);
    }
    if (variant < 3) {
      // Repeated aliases still consume transmission bytes. Five individually
      // legal 16-MiB values must form two calls, not fail a 64-MiB whole-frame
      // limit or reevaluate the fifth row's parameter/cast after flushing.
      constexpr uint64_t VALUE_BYTES = UINT64_C(16777216);
      std::string large(VALUE_BYTES, 'a');
      fill(7);
      for (int i = 0; i < 5; ++i)
        input->locate_batch_datums(eval)[i].set_string(ObString(large.size(), large.data()));
      input->locate_batch_datums(eval)[5].set_null();
      input->locate_batch_datums(eval)[6].set_string(ObString::make_string("z"));
      const int calls = provider.function_batches_, rows = provider.function_batch_rows_;
      const int casts = provider.casts_, functions = provider.functions_;
      const size_t shape = provider.function_batch_shapes_.size();
      CHECK(root->eval_batch(eval, *skip, 7) == OB_SUCCESS);
      CHECK(provider.function_batches_ == calls + 2 && provider.function_batch_rows_ == rows + 7);
      // A column's runtime NULL still invokes its declared cast. Unlike a
      // compile-time untyped NULL it is not replaced by a typed-NULL literal.
      if (provider.functions_ != functions + 7 || provider.casts_ != casts + (variant == 1 ? 7 : 0))
        std::cerr << "byte batch variant=" << variant << " functions=" << provider.functions_ - functions
                  << " casts=" << provider.casts_ - casts << std::endl;
      CHECK(provider.functions_ == functions + 7 && provider.casts_ == casts + (variant == 1 ? 7 : 0));
      CHECK(provider.function_batch_shapes_[shape].first == 4
          && provider.function_batch_shapes_[shape].second == 4 * VALUE_BYTES);
      CHECK(provider.function_batch_shapes_[shape + 1].first == 3
          && provider.function_batch_shapes_[shape + 1].second == VALUE_BYTES + 1);
      for (int i = 0; i < 7; ++i) {
        const auto &datum = root->locate_batch_datums(eval)[i];
        CHECK(root->get_evaluated_flags(eval).at(i));
        if (i == 5) CHECK(datum.is_null());
        else if (variant < 2) CHECK(!datum.is_null() && datum.get_int() == (i < 5 ? VALUE_BYTES : 1));
        else CHECK(!datum.is_null() && datum.get_string() == (i < 5
            ? ObString(large.size(), large.data()) : ObString::make_string("z")));
      }
      CHECK(root->eval_batch(eval, *skip, 7) == OB_SUCCESS && provider.function_batches_ == calls + 2);
      CHECK(provider.resolves_ == resolves);
      if (variant == 0) {
        // The new chunk is never retried when its plugin rejects the data.
        fill(7);
        for (int i = 0; i < 5; ++i)
          input->locate_batch_datums(eval)[i].set_string(ObString(large.size(), large.data()));
        bytes[4].assign(large.size(), char(0xff));
        input->locate_batch_datums(eval)[4].set_string(ObString(bytes[4].size(), bytes[4].data()));
        const int before = provider.function_batches_, outputs = provider.function_batch_outputs_;
        CHECK(root->eval_batch(eval, *skip, 7) == OB_INVALID_ARGUMENT);
        CHECK(provider.function_batches_ == before + 2 && provider.function_batch_outputs_ == outputs + 4);
        for (int i = 0; i < 7; ++i) CHECK(!root->get_evaluated_flags(eval).at(i)
            && root->locate_batch_datums(eval)[i].is_null());
        // A single oversized value is not split or passed to the DSO.
        fill(1); large.push_back('a');
        input->locate_batch_datums(eval)[0].set_string(ObString(large.size(), large.data()));
        const int before_oversize = provider.function_batches_;
        CHECK(root->eval_batch(eval, *skip, 1) == OB_SIZE_OVERFLOW);
        CHECK(provider.function_batches_ == before_oversize && !root->get_evaluated_flags(eval).at(0));
      }
    }
    ObPluginStatusSnapshot status;
    CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
  }
}
}
#endif
