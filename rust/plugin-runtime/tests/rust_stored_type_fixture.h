// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Schema-derived column + real LOB decoder + actual Rust DSO. The column row
// is supplied to an execution frame, not read from a live storage table.
#ifndef SEEKDB_TEST_RUST_STORED_TYPE_FIXTURE_H_
#define SEEKDB_TEST_RUST_STORED_TYPE_FIXTURE_H_
#include "plugin_projection_fixture.h"
#include "routine_overlay_guard_fixture.h"
#include "sql/rewrite/ob_query_range_define.h"

namespace rust_stored_type_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::plugin;
constexpr const char *TYPE_ID = "org.seekdb.rust-text.stored-utf8";

struct Encoded {
  std::string bytes;
  bool emitted = false;
  const char *expected_type = "core.type.bytes";
  static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
      const seekdb_plugin_execution_result_v1_t *value) {
    auto &self = *reinterpret_cast<Encoded *>(host);
    CHECK(value && value->struct_size >= sizeof(*value) && !value->is_null && !self.emitted);
    CHECK(value->type_id && std::strcmp(value->type_id, self.expected_type) == 0);
    CHECK(!value->data_size || value->data);
    if (value->data_size) self.bytes.assign(reinterpret_cast<const char *>(value->data), value->data_size);
    else self.bytes.clear();
    self.emitted = true;
    return SEEKDB_PLUGIN_STATUS_OK;
  }
};

template <typename Provider>
void run(Provider &provider, ObPluginLoader &loader, ObArenaAllocator &arena,
         ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_type_by_id(TYPE_ID, binding) == OB_SUCCESS);
  CHECK(binding.flags == (SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT | SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG));
  CHECK(loader.check_bound_type_comparison(binding) == OB_SUCCESS);
  oceanbase::share::schema::ObColumnSchemaV2 schema;
  schema.set_table_id(123); schema.set_column_id(456); schema.set_data_type(ObLongTextType);
  CHECK(schema.set_column_name("payload") == OB_SUCCESS);
  schema.set_collation_type(CS_TYPE_BINARY);
  ObSEArray<ObString, 7> fields;
  for (const char *field : {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2, "rust_stored_utf8", TYPE_ID,
      "org.seekdb.rust-text", "0", "org.seekdb.rust-text.stored-utf8.v1", "1"}) {
    CHECK(fields.push_back(ObString::make_string(field)) == OB_SUCCESS);
  }
  CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
  plugin_projection_test::InrowOnlyLobService lob_service;
  struct Case { const char *sql, *input, *output; int value, decodes, comparisons;
    int invalid = 0; int status = OB_SUCCESS; int size = -1; };
  for (const Case &test : {
      Case{"payload < CAST('aa' AS rust_stored_utf8)", "z", nullptr, 1, 1, 1},
      Case{"payload < CAST('z' AS rust_stored_utf8)", "a", nullptr, 1, 1, 1}, // Storage bytes have reverse order.
      Case{"payload = CAST('z' AS rust_stored_utf8)", "z", nullptr, 1, 1, 1},
      Case{"payload = CAST('' AS rust_stored_utf8)", "", nullptr, 1, 1, 1},
      Case{"payload = CAST('a\\0b' AS rust_stored_utf8)", "a\0b", nullptr, 1, 1, 1, 0, OB_SUCCESS, 3},
      Case{"payload < CAST('aa' AS rust_stored_utf8)", nullptr, nullptr, -1, 0, 0},
      Case{"payload <=> CAST(NULL AS rust_stored_utf8)", nullptr, nullptr, 1, 0, 0},
      Case{"payload <=> CAST(NULL AS rust_stored_utf8)", "z", nullptr, 0, 1, 0},
      Case{"CAST(payload AS BINARY)", "z", "z", 0, 1, 0},
      Case{"seekdb_rust_identity(payload)", "z", "z", 0, 1, 0},
      Case{"payload IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))", "z", nullptr, 1, 1, 2},
      Case{"payload NOT IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))", "z", nullptr, 0, 1, 2},
      Case{"payload IN (CAST('a' AS rust_stored_utf8),NULL)", "z", nullptr, -1, 1, 1},
      Case{"payload IN (NULL,CAST('z' AS rust_stored_utf8))", "z", nullptr, 1, 1, 1},
      Case{"payload IN (CAST('z' AS rust_stored_utf8))", "z", nullptr, 1, 1, 1},
      Case{"payload IN (CAST(X'FF' AS rust_stored_utf8))", nullptr, nullptr, -1, 0, 0},
      Case{"payload IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"CASE payload WHEN CAST('a' AS rust_stored_utf8) THEN 11 WHEN CAST('z' AS rust_stored_utf8) THEN 22 ELSE 33 END", "z", nullptr, 22, 1, 2},
      Case{"CASE payload WHEN CAST('a' AS rust_stored_utf8) THEN 11 WHEN CAST('z' AS rust_stored_utf8) THEN 22 ELSE 33 END", nullptr, nullptr, 33, 0, 0},
      Case{"CASE payload WHEN NULL THEN 11 WHEN CAST('z' AS rust_stored_utf8) THEN 22 END", "z", nullptr, 22, 1, 1},
      Case{"CASE payload WHEN 'a' THEN 11 WHEN CAST('z' AS rust_stored_utf8) THEN 22 END", "z", nullptr, 22, 1, 1},
      Case{"CASE payload WHEN CAST('z' AS rust_stored_utf8) THEN CAST('chosen' AS rust_stored_utf8) ELSE NULL END", "z", "chosen", 0, 1, 1},
      Case{"CASE payload WHEN CAST('a' AS rust_stored_utf8) THEN 11 ELSE 33 END", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"(payload,1) < (CAST('aa' AS rust_stored_utf8),2)", "z", nullptr, 1, 1, 1},
      Case{"(payload,1) < (CAST('z' AS rust_stored_utf8),2)", "z", nullptr, 1, 1, 1},
      Case{"(payload,1) = (CAST(NULL AS rust_stored_utf8),2)", nullptr, nullptr, 0, 0, 0},
      Case{"(NULL,payload) = (NULL,CAST('a' AS rust_stored_utf8))", "z", nullptr, 0, 1, 1},
      Case{"(NULL,payload) < (NULL,CAST('a' AS rust_stored_utf8))", "z", nullptr, -1, 0, 0},
      Case{"(payload,1) <=> (CAST(NULL AS rust_stored_utf8),1)", nullptr, nullptr, 1, 0, 0},
      Case{"(payload,1) < (CAST('z' AS rust_stored_utf8),2)", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"(payload,1) IN ((CAST('a' AS rust_stored_utf8),1),(CAST('z' AS rust_stored_utf8),1))", "z", nullptr, 1, 1, 2},
      Case{"(payload,1) NOT IN ((CAST('a' AS rust_stored_utf8),1),(CAST('z' AS rust_stored_utf8),1))", "z", nullptr, 0, 1, 2},
      Case{"(payload,1) IN ((CAST('z' AS rust_stored_utf8),2))", nullptr, nullptr, 0, 0, 0},
      Case{"(payload,1) IN ((NULL,1),(CAST('a' AS rust_stored_utf8),1))", "z", nullptr, -1, 1, 1},
      Case{"(1,payload) IN ((2,CAST(X'FF' AS rust_stored_utf8)),(1,CAST('z' AS rust_stored_utf8)))", "z", nullptr, 1, 1, 1},
      Case{"(1,payload) IN ((2,CAST(X'FF' AS rust_stored_utf8)),(3,CAST('z' AS rust_stored_utf8)))", "z", nullptr, 0, 0, 0},
      Case{"(payload,1) IN (('a',1),(CAST('z' AS rust_stored_utf8),1))", "z", nullptr, 1, 1, 0},
      Case{"(payload,1) IN ((CAST('z' AS rust_stored_utf8),1))", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"((payload,1),2) < ((CAST('aa' AS rust_stored_utf8),1),2)", "z", nullptr, 1, 1, 1},
      Case{"((payload,1),2) = ((CAST(NULL AS rust_stored_utf8),1),3)", nullptr, nullptr, 0, 0, 0},
      Case{"((NULL,payload),1) < ((NULL,CAST('a' AS rust_stored_utf8)),2)", "z", nullptr, -1, 0, 0},
      Case{"((payload,1),2) <=> ((CAST(NULL AS rust_stored_utf8),1),2)", nullptr, nullptr, 1, 0, 0},
      Case{"((payload,1),2) IN (((CAST('a' AS rust_stored_utf8),1),2),((CAST('z' AS rust_stored_utf8),1),2))", "z", nullptr, 1, 1, 2},
      Case{"((payload,1),2) NOT IN (((NULL,1),2),((CAST('a' AS rust_stored_utf8),1),2))", "z", nullptr, -1, 1, 1},
      Case{"(((1,2),3),payload) IN ((((1,2),4),CAST(X'FF' AS rust_stored_utf8)),(((1,2),5),CAST('z' AS rust_stored_utf8)))", "z", nullptr, 0, 0, 0},
      Case{"((payload,1),2) IN (((CAST('z' AS rust_stored_utf8),1),2))", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"payload BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('zz' AS rust_stored_utf8)", "z", nullptr, 1, 1, 2},
      Case{"payload NOT BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('zz' AS rust_stored_utf8)", "z", nullptr, 0, 1, 2},
      Case{"payload BETWEEN NULL AND CAST('a' AS rust_stored_utf8)", "z", nullptr, 0, 1, 1},
      Case{"payload BETWEEN CAST('zz' AS rust_stored_utf8) AND NULL", "z", nullptr, 0, 1, 1},
      Case{"payload BETWEEN CAST(X'FF' AS rust_stored_utf8) AND CAST('zz' AS rust_stored_utf8)", nullptr, nullptr, -1, 0, 0},
      Case{"payload BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('zz' AS rust_stored_utf8)", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"payload < CAST('aa' AS rust_stored_utf8)", "z", nullptr, 0, 1, 0, 1, OB_INVALID_ARGUMENT},
      Case{"payload < CAST('aa' AS rust_stored_utf8)", "z", nullptr, 0, 1, 0, 2, OB_INVALID_ARGUMENT},
      Case{"payload < CAST('aa' AS rust_stored_utf8)", "z", nullptr, 0, 1, 0, 3, OB_INVALID_ARGUMENT}}) {
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_lob_read_service(&lob_service);
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
    CHECK(columns.count() == 1 && columns.at(0).ref_expr_ && columns.at(0).ref_expr_->is_column_ref_expr());
    auto *column = static_cast<ObColumnRefRawExpr *>(columns.at(0).ref_expr_);
    CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
    column->set_ref_id(123, 456);
    CHECK(column->get_plugin_type() && column->get_plugin_type()->stored_ && !column->get_plugin_type()->catalog_epoch_);
    const int callbacks = provider.decodes_ + provider.comparisons_ + provider.casts_;
    const int formalized = raw->formalize(&session);
    if (formalized != OB_SUCCESS) std::cerr << "stored Rust bind=" << formalized << " sql=" << test.sql << std::endl;
    CHECK(formalized == OB_SUCCESS && callbacks == provider.decodes_ + provider.comparisons_ + provider.casts_);
    const int resolves = provider.resolves_;
    ObRawExpr *copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(factory, raw, copy) == OB_SUCCESS && copy);
    CHECK(copy->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(copy) == OB_SUCCESS);
    ObExprFrameInfo frame(arena);
    const int generated = generator.generate(roots, frame);
    if (generated != OB_SUCCESS) std::cerr << "stored Rust CG=" << generated << " sql=" << test.sql << std::endl;
    CHECK(generated == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr, *input = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*copy, outputs, root) == OB_SUCCESS && root);
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!input); input = &expr; }
    CHECK(input);
    Encoded encoded;
    if (test.input) {
      seekdb_plugin_execution_value_v1_t value{}; value.struct_size = sizeof(value); value.type_id = TYPE_ID;
      value.data = reinterpret_cast<const uint8_t *>(test.input); value.data_size = test.size < 0 ? std::strlen(test.input) : test.size;
      seekdb_plugin_execution_context_v1_t context{}; context.struct_size = sizeof(context);
      context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&encoded); context.emit_result = Encoded::emit;
      CHECK(loader.encode_bound_type(binding, &context, &value) == OB_SUCCESS && encoded.emitted);
      CHECK(encoded.bytes.size() == value.data_size + 4 && encoded.bytes.substr(0, 4) == std::string("RUT\1", 4));
      for (size_t i = 0; i < value.data_size; ++i) CHECK(static_cast<uint8_t>(encoded.bytes[i + 4]) == uint8_t(~value.data[i]));
      if (test.invalid == 1) encoded.bytes[3] = 2;
      if (test.invalid == 2) encoded.bytes[4] = 0; // Decodes to invalid UTF-8.
      if (test.invalid == 3) encoded.bytes.resize(3);
    }
    std::vector<char> lob(sizeof(ObLobCommon) + encoded.bytes.size());
    auto *header = new (lob.data()) ObLobCommon();
    if (!encoded.bytes.empty()) std::memcpy(header->buffer_, encoded.bytes.data(), encoded.bytes.size());
    input->obj_meta_.set_has_lob_header();
    if (test.input) input->locate_expr_datum(eval).set_string(ObString(lob.size(), lob.data()));
    else input->locate_expr_datum(eval).set_null();
    input->get_eval_info(eval).evaluated_ = true;
    const int decodes = provider.decodes_, comparisons = provider.comparisons_;
    ObDatum *result = nullptr;
    const int status = root->eval(eval, result);
    if (status != test.status) std::cerr << "stored Rust eval=" << status << " sql=" << test.sql << std::endl;
    CHECK(status == test.status);
    if (status == OB_SUCCESS) {
      CHECK(result);
      if (test.value == -1) CHECK(result->is_null());
      else if (test.output) {
        CHECK(!result->is_null() && result->get_string() == ObString::make_string(test.output));
        std::fill(lob.begin(), lob.end(), 'x'); // Result does not borrow storage input.
        CHECK(result->get_string() == ObString::make_string(test.output));
      } else CHECK(!result->is_null() && result->get_int() == test.value);
    }
    CHECK(provider.resolves_ == resolves && provider.decodes_ == decodes + test.decodes &&
        provider.comparisons_ == comparisons + test.comparisons);
    ObPluginStatusSnapshot module;
    CHECK(loader.get_status("org.seekdb.rust-text", module) == OB_SUCCESS && module.lease_count_ == 0);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
  struct Assignment { const char *sql, *text; int size = -1; bool rejected = false; };
  for (const Assignment &test : {
      Assignment{"CAST('z' AS rust_stored_utf8)", "z"},
      Assignment{"CAST('' AS rust_stored_utf8)", ""},
      Assignment{"CAST('中🙂' AS rust_stored_utf8)", "中🙂"},
      Assignment{"CAST('a\\0b' AS rust_stored_utf8)", "a\0b", 3},
      Assignment{"CAST(NULL AS rust_stored_utf8)", nullptr},
      Assignment{"'z'", nullptr, -1, true}}) {
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_lob_read_service(&lob_service);
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
        variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS && raw && columns.empty());
    const int encodes = provider.encodes_;
    const int bound = ObRawExprUtils::build_column_conv_expr(factory, &schema, raw, &session);
    if (test.rejected) {
      CHECK(bound == OB_ERR_INVALID_TYPE_FOR_OP && provider.encodes_ == encodes);
      ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
      continue;
    }
    if (bound != OB_SUCCESS) std::cerr << "stored Rust assignment=" << bound << " sql=" << test.sql << std::endl;
    CHECK(bound == OB_SUCCESS && raw->get_expr_type() == T_FUN_COLUMN_CONV && raw->get_plugin_type()->stored_);
    const int resolves = provider.resolves_;
    CHECK(raw->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == resolves);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS && root);
    ObDatum *result = nullptr;
    const int evaluated = root->eval(eval, result);
    if (evaluated != OB_SUCCESS) std::cerr << "stored Rust assignment eval=" << evaluated << " sql=" << test.sql << std::endl;
    CHECK(evaluated == OB_SUCCESS && result);
    CHECK(provider.encodes_ == encodes + (test.text ? 1 : 0) && provider.resolves_ == resolves);
    if (!test.text) CHECK(result->is_null());
    else {
      CHECK(!result->is_null());
      ObEvalCtx::TempAllocGuard temporary(eval);
      ObString storage;
      CHECK(ObTextStringHelper::read_real_string_data_with_copy(execution, temporary.get_allocator(),
          *result, root->datum_meta_, root->obj_meta_.has_lob_header(), storage) == OB_SUCCESS);
      const int64_t size = test.size < 0 ? std::strlen(test.text) : test.size;
      CHECK(storage.length() == size + 4 && std::memcmp(storage.ptr(), "RUT\1", 4) == 0);
      Encoded decoded; decoded.expected_type = TYPE_ID;
      seekdb_plugin_execution_context_v1_t context{}; context.struct_size = sizeof(context);
      context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&decoded); context.emit_result = Encoded::emit;
      CHECK(loader.decode_bound_type(binding, &context, reinterpret_cast<const uint8_t *>(storage.ptr()), storage.length()) == OB_SUCCESS);
      CHECK(decoded.emitted && decoded.bytes == std::string(test.text, size));
    }
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
// Real base-table name/column resolution and the optimizer's range extractor.
// Schema objects are fixture-owned; this does not scan storage or authorize SQL.
template <typename Provider>
void table_predicates(Provider &provider, ObArenaAllocator &arena,
                      ObRawExprFactory &factory, ObSQLSessionInfo &session)
{
  using namespace oceanbase::share::schema;
  CHECK(ObSysTableChecker::instance().init() == OB_SUCCESS);
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSimpleServerRuntimeSchema runtime;
  runtime.set_schema_version(42); runtime.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  runtime.set_status(SERVER_RUNTIME_STATUS_NORMAL);
  CHECK(runtime.set_runtime_name(ObString::make_string("rust_stored_fixture")) == OB_SUCCESS);
  CHECK(manager->add_runtime_schema(runtime) == OB_SUCCESS);
  ObDatabaseSchema database;
  database.set_database_id(OB_SYS_DATABASE_ID); database.set_schema_version(42);
  CHECK(database.set_database_name(OB_SYS_DATABASE_NAME) == OB_SUCCESS);
  ObSimpleDatabaseSchema simple_database;
  simple_database.set_database_id(database.get_database_id()); simple_database.set_schema_version(42);
  CHECK(simple_database.set_database_name(OB_SYS_DATABASE_NAME) == OB_SUCCESS);
  CHECK(manager->add_database(simple_database) == OB_SUCCESS);
  ObTableSchema table;
  table.set_table_id(311235); table.set_database_id(database.get_database_id());
  table.set_schema_version(42); table.set_table_type(USER_TABLE);
  table.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  table.set_rowkey_column_num(1); table.set_max_used_column_id(17);
  CHECK(table.set_table_name("rust_stored_fixture") == OB_SUCCESS);
  ObColumnSchemaV2 payload;
  payload.set_table_id(table.get_table_id()); payload.set_column_id(16);
  CHECK(payload.set_column_name("payload") == OB_SUCCESS);
  payload.set_data_type(ObLongTextType); payload.set_collation_type(CS_TYPE_BINARY);
  payload.set_data_length(16 * 1024 * 1024);
  ObSEArray<ObString, 7> fields;
  for (const char *field : {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2, "rust_stored_utf8", TYPE_ID,
      "org.seekdb.rust-text", "0", "org.seekdb.rust-text.stored-utf8.v1", "1"}) {
    CHECK(fields.push_back(ObString::make_string(field)) == OB_SUCCESS);
  }
  CHECK(payload.set_extended_type_info(fields) == OB_SUCCESS);
  CHECK(table.add_column(payload) == OB_SUCCESS);
  ObColumnSchemaV2 integer;
  integer.set_table_id(table.get_table_id()); integer.set_column_id(17);
  CHECK(integer.set_column_name("number") == OB_SUCCESS);
  integer.set_data_type(ObIntType);
  integer.set_rowkey_position(1); integer.set_nullable(false);
  CHECK(table.add_column(integer) == OB_SUCCESS);
  CHECK(table.is_valid());
  const int added = manager->add_table(table);
  if (added != OB_SUCCESS) std::cerr << "Rust stored table schema=" << added << std::endl;
  CHECK(added == OB_SUCCESS);
  struct Case { const char *predicate; bool native = false; bool point = true; int in_values = 0; };
  for (const Case &test : {
      Case{"payload = CAST('z' AS rust_stored_utf8)"},
      Case{"payload <> CAST('z' AS rust_stored_utf8)"},
      Case{"payload < CAST('z' AS rust_stored_utf8)"},
      Case{"payload <= CAST('z' AS rust_stored_utf8)"},
      Case{"payload > CAST('z' AS rust_stored_utf8)"},
      Case{"payload >= CAST('z' AS rust_stored_utf8)"},
      Case{"payload <=> CAST(NULL AS rust_stored_utf8)"},
      Case{"CAST('z' AS rust_stored_utf8) > payload"},
      Case{"payload > CAST('a' AS rust_stored_utf8) AND payload < CAST('z' AS rust_stored_utf8)"},
      Case{"payload = CAST('a' AS rust_stored_utf8) OR payload = CAST('z' AS rust_stored_utf8)"},
      Case{"payload BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('zz' AS rust_stored_utf8)"},
      Case{"payload NOT BETWEEN CAST('a' AS rust_stored_utf8) AND CAST('zz' AS rust_stored_utf8)"},
      Case{"payload BETWEEN NULL AND CAST('z' AS rust_stored_utf8)"},
      Case{"payload NOT BETWEEN CAST('z' AS rust_stored_utf8) AND NULL"},
      Case{"payload IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))"},
      Case{"payload NOT IN (CAST('a' AS rust_stored_utf8),CAST('z' AS rust_stored_utf8))"},
      Case{"payload IN (NULL,CAST('z' AS rust_stored_utf8))"},
      Case{"payload IN (CAST('z' AS rust_stored_utf8))"},
      Case{"CASE payload WHEN CAST('z' AS rust_stored_utf8) THEN 1 ELSE 0 END = 1"},
      Case{"CASE payload WHEN CAST('a' AS rust_stored_utf8) THEN 1 WHEN CAST('z' AS rust_stored_utf8) THEN 2 ELSE 0 END = 2"},
      Case{"(payload,1) < (CAST('aa' AS rust_stored_utf8),2)"},
      Case{"(payload,1) = (CAST('z' AS rust_stored_utf8),1)"},
      Case{"(payload,1) IN ((CAST('a' AS rust_stored_utf8),1),(CAST('z' AS rust_stored_utf8),1))"},
      Case{"(payload,1) NOT IN ((CAST('a' AS rust_stored_utf8),1),(CAST('z' AS rust_stored_utf8),1))"},
      Case{"(payload,1) IN ((CAST('z' AS rust_stored_utf8),1))"},
      Case{"((payload,1),2) < ((CAST('aa' AS rust_stored_utf8),1),2)"},
      Case{"((payload,1),2) IN (((CAST('a' AS rust_stored_utf8),1),2),((CAST('z' AS rust_stored_utf8),1),2))"},
      Case{"((payload,1),2) NOT IN (((CAST('a' AS rust_stored_utf8),1),2),((CAST('z' AS rust_stored_utf8),1),2))"},
      // The parser retains a column's singleton IN until its type is known.
      // is_precise_get intentionally excludes IN nodes, even with one value.
      Case{"number IN (7)", true, false, 1},
      Case{"number IN (2,7)", true, false, 2},
      Case{"number = 7", true},
      Case{"number BETWEEN 2 AND 9", true, false},
      Case{"number NOT BETWEEN 2 AND 9", true, false}}) {
    ObSqlCtx sql_context;
    ObExecContext execution(arena); execution.set_my_session(&session);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObSchemaGetterGuard guard;
    CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_table(guard, table) == OB_SUCCESS);
    sql_context.session_info_ = &session; sql_context.schema_guard_ = &guard;
    execution.set_sql_ctx(&sql_context);
    ObSchemaChecker checker; CHECK(checker.init(guard) == OB_SUCCESS);
    const ObTableSchema *resolved_table = nullptr;
    CHECK(checker.get_table_schema(table.get_table_id(), resolved_table) == OB_SUCCESS && resolved_table == &table);
    auto *statements = execution.get_stmt_factory(); CHECK(statements);
    ObResolverParams params;
    params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = statements;
    params.query_ctx_ = statements->get_query_ctx(); params.session_info_ = &session; params.schema_checker_ = &checker;
    const std::string sql = std::string("SELECT ") + (test.native ? "number" : "payload") +
        " FROM " + OB_SYS_DATABASE_NAME + ".rust_stored_fixture WHERE " + test.predicate;
    ObParser parser(arena, session.get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(sql.size(), sql.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(params);
    const int callbacks = provider.functions_ + provider.casts_ + provider.decodes_ + provider.comparisons_;
    const int resolved = resolver.resolve(*parsed.result_tree_->children_[0]);
    if (resolved != OB_SUCCESS) std::cerr << "Rust stored table resolve=" << resolved << " sql=" << sql << std::endl;
    CHECK(resolved == OB_SUCCESS);
    auto *stmt = resolver.get_select_stmt();
    CHECK(stmt && stmt->get_table_size() == 1 && stmt->get_column_size() == 1);
    CHECK(stmt->get_table_items().at(0)->ref_id_ == table.get_table_id());
    const auto *type = stmt->get_column_items().at(0).expr_->get_plugin_type();
    if (test.native) CHECK(!type);
    else CHECK(type && type->stored_ && type->logical_id_ == ObString::make_string(TYPE_ID) && !type->catalog_epoch_);
    CHECK(!stmt->get_condition_exprs().empty());
    const int resolves = provider.resolves_;
    for (int64_t i = 0; i < stmt->get_condition_exprs().count(); ++i) {
      auto *condition = stmt->get_condition_exprs().at(i);
      CHECK(condition->deduce_type(&session) == OB_SUCCESS);
      if (!test.native) CHECK(condition->has_flag(CNT_STATE_FUNC));
    }
    ObPreRangeGraph range(arena);
    const int extracted = range.preliminary_extract_query_range(stmt->get_column_items(),
        stmt->get_condition_exprs(), &execution);
    if (extracted != OB_SUCCESS) std::cerr << "Rust stored range=" << extracted << " sql=" << sql << std::endl;
    CHECK(extracted == OB_SUCCESS);
    CHECK(range.is_precise_whole_range() == !test.native);
    if (test.native) {
      if (range.get_range_exprs().count() != 1 || range.is_precise_get() != test.point)
        std::cerr << "Rust native range expressions=" << range.get_range_exprs().count()
                  << " precise_get=" << range.is_precise_get() << " sql=" << sql << std::endl;
      CHECK(range.get_range_exprs().count() == 1 && range.is_precise_get() == test.point);
      if (test.in_values) {
        const auto *head = range.get_range_head();
        CHECK(head && head->contain_in_ && !head->always_true_ && !head->always_false_);
        CHECK(!head->and_next_ && !head->or_next_ && head->min_offset_ == 0 && head->max_offset_ == 0);
        CHECK(head->in_param_count_ == test.in_values && head->include_start_ && head->include_end_);
      }
    }
    else CHECK(range.get_range_exprs().empty()); // No storage-byte predicate may be consumed.
    CHECK(provider.resolves_ == resolves);
    CHECK(provider.functions_ + provider.casts_ + provider.decodes_ + provider.comparisons_ == callbacks);
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
} // namespace rust_stored_type_test
#endif
