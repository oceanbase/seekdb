// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real vectorized expression frames with a Rust persistent TYPE and codec.
// Input rows are supplied by the fixture, not read from a storage scan.
#ifndef SEEKDB_TEST_RUST_TYPE_BATCH_FIXTURE_H_
#define SEEKDB_TEST_RUST_TYPE_BATCH_FIXTURE_H_
#include <array>
#include "sql/rewrite/ob_transform_pre_process.h"
namespace rust_type_batch_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  constexpr int BATCH = 6, MAX = 1031;
  using Counts = std::array<int, BATCH>;
  const char *texts[] = {"a", "z", "aa", nullptr, "b", "bb"};
  const Counts nonnull{1,1,1,0,1,1}, membership{1,2,2,0,2,2};
  struct Case {
    const char *sql; Counts result, comparisons, decodes;
    bool text_result = false;
    Counts functions{};
    bool batch_functions = false;
  };
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_type_by_id(rust_stored_type_test::TYPE_ID, binding) == OB_SUCCESS);
  oceanbase::share::schema::ObColumnSchemaV2 schema;
  schema.set_table_id(123); schema.set_column_id(456); schema.set_data_type(ObLongTextType);
  CHECK(schema.set_column_name("payload") == OB_SUCCESS);
  schema.set_collation_type(CS_TYPE_BINARY);
  ObSEArray<ObString, 7> fields;
  for (const char *field : {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2, "rust_stored_utf8",
      rust_stored_type_test::TYPE_ID, "org.seekdb.rust-text", "0", "org.seekdb.rust-text.stored-utf8.v1", "1"})
    CHECK(fields.push_back(ObString::make_string(field)) == OB_SUCCESS);
  CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
  plugin_projection_test::InrowOnlyLobService lob_service;
  bool tested_failures[3] = {};
  for (const Case &test : {
      Case{"payload < CAST('aa' AS rust_stored_utf8)", {1,1,0,-1,1,0}, nonnull, nonnull},
      Case{"payload <= CAST('aa' AS rust_stored_utf8)", {1,1,1,-1,1,0}, nonnull, nonnull},
      Case{"payload > CAST('aa' AS rust_stored_utf8)", {0,0,0,-1,0,1}, nonnull, nonnull},
      Case{"payload >= CAST('aa' AS rust_stored_utf8)", {0,0,1,-1,0,1}, nonnull, nonnull},
      Case{"payload = CAST('aa' AS rust_stored_utf8)", {0,0,1,-1,0,0}, nonnull, nonnull},
      Case{"payload <> CAST('aa' AS rust_stored_utf8)", {1,1,0,-1,1,1}, nonnull, nonnull},
      Case{"payload <=> CAST('aa' AS rust_stored_utf8)", {0,0,1,0,0,0}, nonnull, nonnull},
      Case{"payload BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('aa' AS rust_stored_utf8)", {1,1,1,-1,1,0}, {2,2,2,0,2,2}, nonnull},
      Case{"payload NOT BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('aa' AS rust_stored_utf8)", {0,0,0,-1,0,1}, {2,2,2,0,2,2}, nonnull},
      Case{"payload IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))", {1,1,0,-1,0,0}, membership, nonnull},
      Case{"payload NOT IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))", {0,0,1,-1,1,1}, membership, nonnull},
      Case{"CASE payload WHEN CAST('a' AS rust_stored_utf8) THEN 11 WHEN CAST('z' AS rust_stored_utf8) THEN 22 ELSE 33 END", {11,22,33,33,33,33}, membership, nonnull},
      Case{"(payload,1) < (CAST('aa' AS rust_stored_utf8),2)", {1,1,1,-1,1,0}, nonnull, nonnull},
      Case{"(payload,1) IN ((NULL,1),(CAST('z' AS rust_stored_utf8),1))", {-1,1,-1,-1,-1,-1}, nonnull, nonnull},
      Case{"(payload,1) NOT IN ((NULL,1),(CAST('z' AS rust_stored_utf8),1))", {-1,0,-1,-1,-1,-1}, nonnull, nonnull},
      Case{"(1,payload) IN ((2,CAST(X'FF' AS rust_stored_utf8)),(3,CAST('z' AS rust_stored_utf8)))", {0,0,0,0,0,0}, {}, {}},
      Case{"((payload,1),2) < ((CAST('aa' AS rust_stored_utf8),2),2)", {1,1,1,-1,1,0}, nonnull, nonnull},
      Case{"((payload,1),2) IN (((NULL,1),2),((CAST('z' AS rust_stored_utf8),1),2))", {-1,1,-1,-1,-1,-1}, nonnull, nonnull},
      Case{"((payload,1),2) NOT IN (((NULL,1),2),((CAST('z' AS rust_stored_utf8),1),2))", {-1,0,-1,-1,-1,-1}, nonnull, nonnull},
      Case{"(((1,2),3),payload) IN ((((1,2),4),CAST(X'FF' AS rust_stored_utf8)),(((1,2),5),CAST('z' AS rust_stored_utf8)))", {0,0,0,0,0,0}, {}, {}},
      Case{"CASE WHEN payload < CAST('aa' AS rust_stored_utf8) THEN seekdb_rust_char_count('a') ELSE 0 END", {1,1,0,0,1,0}, nonnull, nonnull, false, {1,1,0,0,1,0}},
      Case{"CASE WHEN payload < CAST('aa' AS rust_stored_utf8) THEN 1 ELSE seekdb_rust_char_count('aa') END", {1,1,2,2,1,2}, nonnull, nonnull, false, {0,0,1,1,0,1}},
      Case{"CASE WHEN payload < CAST('aa' AS rust_stored_utf8) THEN payload ELSE CAST('fallback' AS rust_stored_utf8) END", {}, nonnull, {2,2,1,0,2,1}, true},
      Case{"seekdb_rust_identity(payload) < CAST('aa' AS rust_stored_utf8)", {1,1,0,-1,1,0}, nonnull, nonnull, false, {1,1,1,1,1,1}, true},
      Case{"seekdb_rust_identity(payload) <=> payload", {1,1,1,1,1,1}, nonnull, {2,2,2,0,2,2}, false, {1,1,1,1,1,1}, true},
      Case{"CAST(seekdb_rust_concat3_called(CAST(payload AS BINARY),'','') AS rust_stored_utf8) = payload",
          {1,1,1,-1,1,1}, nonnull, {2,2,2,0,2,2}, false, {1,1,1,1,1,1}, true},
      Case{"payload BETWEEN seekdb_rust_identity(payload) AND seekdb_rust_identity(payload)",
          {1,1,1,-1,1,1}, {2,2,2,0,2,2}, {3,3,3,0,3,3}, false, {2,2,2,0,2,2}, true},
      Case{"payload BETWEEN NULL AND seekdb_rust_identity(CAST(CASE WHEN payload IS NULL THEN X'FF' ELSE 'aa' END AS rust_stored_utf8))",
          {-1,-1,-1,-1,-1,0}, nonnull, nonnull, false, nonnull, true},
      Case{"payload IN (CAST('a' AS rust_stored_utf8),seekdb_rust_identity(payload))",
          {1,1,1,-1,1,1}, membership, membership, false, {0,1,1,0,1,1}, true},
      Case{"payload NOT IN (CAST('a' AS rust_stored_utf8),seekdb_rust_identity(payload))",
          {0,0,0,-1,0,0}, membership, membership, false, {0,1,1,0,1,1}, true},
      Case{"payload IN (seekdb_rust_identity(payload),CAST(X'FF' AS rust_stored_utf8))",
          {1,1,1,-1,1,1}, nonnull, {2,2,2,0,2,2}, false, nonnull, true},
      Case{"payload IN (NULL,seekdb_rust_identity(payload))",
          {1,1,1,-1,1,1}, nonnull, {2,2,2,0,2,2}, false, nonnull, true},
      Case{"payload NOT IN (NULL,CAST('a' AS rust_stored_utf8))",
          {0,-1,-1,-1,-1,-1}, nonnull, nonnull}}) {
    ObSqlCtx sql_context; sql_context.session_info_ = &session;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql_context);
    execution.set_lob_read_service(&lob_service);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(test.sql), session.get_charsets4parser(),
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
        variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS && raw);
    // Repeated references may be independently collected by the raw resolver.
    // Bind them to one schema-derived column, as the normal name resolver does.
    CHECK(!columns.empty());
    auto *column = static_cast<ObColumnRefRawExpr *>(columns.at(0).ref_expr_);
    CHECK(column && ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
    column->set_ref_id(123,456);
    for (int64_t i = 1; i < columns.count(); ++i) {
      CHECK(columns.at(i).ref_expr_);
      CHECK(ObRawExprUtils::replace_ref_column(raw, columns.at(i).ref_expr_, column) == OB_SUCCESS);
    }
    CHECK(raw->formalize(&session) == OB_SUCCESS);
    const auto check_callback_flags = [&](const auto &self, ObRawExpr &expr) -> void {
      if (expr.get_expr_type() >= T_FUN_SYS_PLUGIN_FUNCTION && expr.get_expr_type() <= T_FUN_SYS_PLUGIN_TYPE_IN) {
        CHECK(!expr.is_const_expr() && expr.is_vectorize_result());
      }
      for (int64_t i = 0; i < expr.get_param_count(); ++i) self(self, *expr.get_param_expr(i));
    };
    check_callback_flags(check_callback_flags, *raw);
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    bool transformed = false;
    CHECK(ObTransformPreProcess::transform_expr(factory, session, copy, transformed) == OB_SUCCESS);
    check_callback_flags(check_callback_flags, *copy);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0); generator.set_batch_size(MAX);
    ObExprFrameInfo frame(arena);
    const int generated = generator.generate(roots, frame);
    if (generated != OB_SUCCESS) std::cerr << "Rust type batch CG=" << generated << " sql=" << test.sql << std::endl;
    CHECK(generated == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr, *input = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root && root->is_batch_result());
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!input); input = &expr; }
    CHECK(input && input->is_batch_result());
    auto *skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(MAX))); CHECK(skip);
    std::array<std::vector<char>, MAX> lobs;
    const auto fill = [&](int size, int offset) {
      for (auto &expr : frame.rt_exprs_) {
        expr.get_eval_info(eval).evaluated_ = false;
        if (expr.is_batch_result()) expr.get_evaluated_flags(eval).reset(MAX);
      }
      auto *datums = input->locate_batch_datums(eval);
      for (int i = 0; i < size; ++i) {
        const char *text = texts[(i + offset) % BATCH];
        if (!text) datums[i].set_null();
        else {
          rust_stored_type_test::Encoded encoded;
          seekdb_plugin_execution_value_v1_t value{}; value.struct_size = sizeof(value);
          value.type_id = rust_stored_type_test::TYPE_ID;
          value.data = reinterpret_cast<const uint8_t *>(text); value.data_size = std::strlen(text);
          seekdb_plugin_execution_context_v1_t context{}; context.struct_size = sizeof(context);
          context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&encoded);
          context.emit_result = rust_stored_type_test::Encoded::emit;
          CHECK(loader.encode_bound_type(binding, &context, &value) == OB_SUCCESS && encoded.emitted);
          lobs[i].assign(sizeof(ObLobCommon) + encoded.bytes.size(), 0);
          auto *header = new (lobs[i].data()) ObLobCommon();
          std::memcpy(header->buffer_, encoded.bytes.data(), encoded.bytes.size());
          datums[i].set_string(ObString(lobs[i].size(), lobs[i].data()));
        }
        input->get_evaluated_flags(eval).set(i);
      }
      input->obj_meta_.set_has_lob_header();
      input->get_eval_info(eval).evaluated_ = true;
      input->get_eval_info(eval).projected_ = true;
      input->get_eval_info(eval).cnt_ = size;
    };
    const auto check_results = [&](int size, int offset) {
      const auto *results = root->locate_batch_datums(eval);
      const char *expected_text[] = {"a", "z", "fallback", "fallback", "b", "fallback"};
      for (int i = 0; i < size; ++i) if (!skip->at(i)) {
        CHECK(root->get_evaluated_flags(eval).at(i));
        const int source = (i + offset) % BATCH;
        if (test.text_result) CHECK(!results[i].is_null() && results[i].get_string() == ObString::make_string(expected_text[source]));
        else if (test.result[source] < 0) CHECK(results[i].is_null());
        else CHECK(!results[i].is_null() && results[i].get_int() == test.result[source]);
      }
    };
    int batch_number = 0;
    for (int size : {BATCH, 3, MAX}) {
      const int offset = batch_number++ * 2;
      fill(size, offset); skip->reset(MAX);
      int decoded = provider.decodes_, compared = provider.comparisons_;
      // An entirely skipped batch must not execute even constant callbacks.
      const int cast = provider.casts_, function = provider.functions_;
      const int scalar = provider.scalar_functions_, batch_rows = provider.function_batch_rows_;
      for (int i = 0; i < size; ++i) skip->set(i);
      CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
      if (provider.decodes_ != decoded || provider.comparisons_ != compared
          || provider.casts_ != cast || provider.functions_ != function)
        std::cerr << "Rust type all-skipped batch callbacks=" << provider.decodes_ - decoded << ","
                  << provider.comparisons_ - compared << "," << provider.casts_ - cast << ","
                  << provider.functions_ - function << " sql=" << test.sql << std::endl;
      CHECK(provider.decodes_ == decoded && provider.comparisons_ == compared
          && provider.casts_ == cast && provider.functions_ == function);
      skip->reset(MAX); skip->set(1); if (size >= BATCH) skip->set(4);
      for (bool complete : {false, false, true}) {
        if (complete) skip->reset(MAX);
        const int evaluated = root->eval_batch(eval, *skip, size);
        if (evaluated != OB_SUCCESS) std::cerr << "Rust type batch eval=" << evaluated << " sql=" << test.sql << std::endl;
        CHECK(evaluated == OB_SUCCESS);
        check_results(size, offset);
        int expected_decodes = 0, expected_comparisons = 0, expected_functions = 0;
        for (int i = 0; i < size; ++i) if (!skip->at(i)) {
          expected_decodes += test.decodes[(i + offset) % BATCH];
          expected_comparisons += test.comparisons[(i + offset) % BATCH];
          expected_functions += test.functions[(i + offset) % BATCH];
        }
        if (provider.decodes_ != decoded + expected_decodes || provider.comparisons_ != compared + expected_comparisons)
          std::cerr << "Rust type batch calls=" << provider.decodes_ - decoded << "," << provider.comparisons_ - compared
                    << " expected=" << expected_decodes << "," << expected_comparisons << " sql=" << test.sql << std::endl;
        CHECK(provider.decodes_ == decoded + expected_decodes && provider.comparisons_ == compared + expected_comparisons);
        CHECK(provider.functions_ == function + expected_functions);
        if (test.batch_functions) {
          if (provider.scalar_functions_ != scalar)
            std::cerr << "type batch scalar fallback=" << provider.scalar_functions_ - scalar
                      << " size=" << size << " sql=" << test.sql << std::endl;
          CHECK(provider.scalar_functions_ == scalar);
          CHECK(provider.function_batch_rows_ == batch_rows + expected_functions);
        }
        CHECK(provider.resolves_ == resolves);
      }
      for (auto &lob : lobs) std::fill(lob.begin(), lob.end(), 'x');
      check_results(size, offset); // Returned bytes do not borrow storage buffers.
    }
    const auto kind = root->args_[0]->type_;
    const int failure_index = kind == T_FUN_SYS_PLUGIN_TYPE_BETWEEN ? 1 : kind == T_FUN_SYS_PLUGIN_TYPE_IN ? 2 : 0;
    if (!tested_failures[failure_index]) {
      tested_failures[failure_index] = true;
      const auto invalidated = [&]() {
        for (int i = 0; i < BATCH; ++i)
          CHECK(!root->args_[0]->get_evaluated_flags(eval).at(i));
      };
      fill(BATCH, 0); skip->reset(MAX); skip->set(2);
      // The invalid row is initially skipped; evaluating it later must produce
      // an error, not a NULL result/success or reevaluate already completed rows.
      lobs[2][sizeof(ObLobCommon) + 3] = 2;
      CHECK(root->eval_batch(eval, *skip, BATCH) == OB_SUCCESS);
      check_results(BATCH, 0);
      const int decoded = provider.decodes_, compared = provider.comparisons_;
      skip->reset(MAX);
      CHECK(root->eval_batch(eval, *skip, BATCH) == OB_INVALID_ARGUMENT);
      CHECK(provider.decodes_ == decoded + 1 && provider.comparisons_ == compared);
      invalidated();
      fill(BATCH, 0);
      class Cancelled final : public ObIExtraStatusCheck {
      public:
        const char *name() const override { return "plugin-type-batch-cancel"; }
        int check() const override { return OB_TIMEOUT; }
      } cancelled;
      const int before = provider.decodes_ + provider.comparisons_;
      {
        ObIExtraStatusCheck::Guard cancellation(execution, cancelled);
        CHECK(root->eval_batch(eval, *skip, BATCH) == OB_TIMEOUT);
      }
      CHECK(provider.decodes_ + provider.comparisons_ == before);
      invalidated();
      // Operands are prefetched before comparison. Cancel after the first
      // comparator; no second comparison enters the DSO, although all five
      // non-NULL left operands have already been decoded in this batch.
      fill(BATCH, 0);
      class CancelAfterComparison final : public ObIExtraStatusCheck {
      public:
        explicit CancelAfterComparison(const int &count) : count_(count), before_(count) {}
        const char *name() const override { return "plugin-type-batch-mid-cancel"; }
        int check() const override { return count_ == before_ ? OB_SUCCESS : OB_TIMEOUT; }
      private:
        const int &count_;
        const int before_;
      } after_comparison(provider.comparisons_);
      const int mid_decoded = provider.decodes_, mid_compared = provider.comparisons_;
      {
        ObIExtraStatusCheck::Guard cancellation(execution, after_comparison);
        CHECK(root->eval_batch(eval, *skip, BATCH) == OB_TIMEOUT);
      }
      CHECK(provider.decodes_ == mid_decoded + 5 && provider.comparisons_ == mid_compared + 1);
      invalidated();
      ObPluginStatusSnapshot cancelled_module;
      CHECK(loader.get_status("org.seekdb.rust-text", cancelled_module) == OB_SUCCESS
          && cancelled_module.lease_count_ == 0);
      // Failed frames are reset before reuse; partial output is not consumed.
      fill(BATCH, 0); CHECK(root->eval_batch(eval, *skip, BATCH) == OB_SUCCESS);
      check_results(BATCH, 0);
    }
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
} // namespace rust_type_batch_test
#endif
