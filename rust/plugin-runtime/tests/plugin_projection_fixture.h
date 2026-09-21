// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real projection resolver and raw-expression ownership. No SQL server/storage.
#ifndef SEEKDB_TEST_PLUGIN_PROJECTION_FIXTURE_H_
#define SEEKDB_TEST_PLUGIN_PROJECTION_FIXTURE_H_
#include "sql/resolver/expr/plugin_expr_type.h"
#include "sql/resolver/expr/ob_raw_expr_copier.h"
#include "sql/resolver/expr/ob_raw_expr_deduce_type.h"
#include "sql/resolver/expr/ob_raw_expr_type_demotion.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/ob_schema_checker.h"
#include "share/plugin/plugin_sql_type.h"
#include "share/ob_i_lob_read_service.h"

namespace plugin_projection_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace plugin_expression_test;

// The real text iterator decodes in-row headers; this fixture supplies no
// backing storage and fails if it attempts an out-of-row read.
class InrowOnlyLobService final : public ObILobReadService {
public:
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, ObObjType, ObCollationType,
      ObLobLocatorV2 &, ObIAllocator *, ObString &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObIAllocator *, uint32_t) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
      ObString &, ObTextStringIterState &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObString &, ObTextStringIterState &) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
  { CHECK(false); return OB_ERR_UNEXPECTED; }
  void free_lob_query_iter(ObLobTextIterCtx &) override {}
};

class ProjectionResolver final : public ObSelectResolver {
public:
  explicit ProjectionResolver(ObResolverParams &params) : ObSelectResolver(params) {}
  using ObDMLResolver::resolve_generated_table_column_item;
  using ObDMLResolver::add_additional_function_according_to_type;
};

inline void ownership()
{
  ObArenaAllocator destination;
  ObRawExprFactory destination_factory(destination);
  ObRawExpr *copy = nullptr;
  {
    ObArenaAllocator source;
    ObRawExprFactory factory(source);
    ObColumnRefRawExpr *column = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
    column->set_ref_id(123, 456); column->set_data_type(ObVarcharType);
    PluginExprType identity;
    std::string local = CUSTOM;
    identity.logical_id_ = ObString(local.size(), local.data());
    identity.physical_type_ = ObVarcharType;
    identity.catalog_epoch_ = 11;
    CHECK(column->set_plugin_type(identity) == OB_SUCCESS);
    local.assign("destroyed source string");
    CHECK(column->get_plugin_type()->logical_id_ == ObString::make_string(CUSTOM));
    CHECK(ObRawExprCopier::copy_expr(destination_factory, column, copy) == OB_SUCCESS);
    CHECK(copy && copy->get_plugin_type() && copy->get_plugin_type() != column->get_plugin_type());
    CHECK(copy->get_plugin_type()->logical_id_.ptr() != column->get_plugin_type()->logical_id_.ptr());
    CHECK(copy->same_as(*column));
    identity = *column->get_plugin_type(); // local's original string storage was invalidated above.
    identity.catalog_epoch_ = 12;
    CHECK(column->set_plugin_type(identity) == OB_SUCCESS);
    CHECK(!copy->same_as(*column)); // Same identity from another query binding is different.
    identity.logical_id_ = ObString::make_string("org.test.other");
    CHECK(column->set_plugin_type(identity) == OB_SUCCESS);
    CHECK(copy->calc_hash() == OB_SUCCESS && column->calc_hash() == OB_SUCCESS);
    CHECK(!copy->same_as(*column)); // Same table/column physical identity is not enough.
  }
  CHECK(copy->get_plugin_type()->logical_id_ == ObString::make_string(CUSTOM));
  CHECK(copy->get_plugin_type()->catalog_epoch_ == 11);
  ObColumnRefRawExpr *plain = nullptr;
  CHECK(destination_factory.create_raw_expr(T_REF_COLUMN, plain) == OB_SUCCESS);
  plain->set_ref_id(123, 456); plain->set_data_type(ObVarcharType);
  CHECK(!plain->get_plugin_type() && !copy->same_as(*plain));
  CHECK(copy->assign(*plain) == OB_SUCCESS && !copy->get_plugin_type());
  PluginExprType bad; bad.logical_id_ = ObString::make_string(CUSTOM);
  CHECK(copy->set_plugin_type(bad) == OB_INVALID_ARGUMENT);
  bad.physical_type_ = static_cast<ObObjType>(-1);
  CHECK(copy->set_plugin_type(bad) == OB_INVALID_ARGUMENT);
  bad.physical_type_ = ObVarcharType;
  CHECK(copy->set_plugin_type(bad) == OB_INVALID_ARGUMENT); // Runtime value needs a query epoch.
  bad.catalog_epoch_ = 11;
  CHECK(copy->set_plugin_type(bad) == OB_SUCCESS);
  const auto *owned = copy->get_plugin_type();
  CHECK(copy->set_plugin_type(bad) == OB_SUCCESS && copy->get_plugin_type() == owned);
  bad.logical_id_ = ObString(3, "a\0b");
  CHECK(copy->set_plugin_type(bad) == OB_INVALID_ARGUMENT && copy->get_plugin_type() == owned);
  copy->reset(); CHECK(!copy->get_plugin_type());
}

inline void execution_parameter_types()
{
  ObArenaAllocator arena;
  ObRawExprFactory factory(arena);
  ObQueryCtx query;
  for (int kind = 0; kind < 3; ++kind) {
    ObColumnRefRawExpr *source = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, source) == OB_SUCCESS);
    source->set_ref_id(123, 456);
    source->set_data_type(kind == 2 ? ObLongTextType : ObVarcharType);
    PluginExprType type;
    type.logical_id_ = ObString::make_string(CUSTOM); type.physical_type_ = source->get_data_type();
    type.catalog_epoch_ = 11;
    if (kind == 2) {
      type.stored_ = true; type.sql_name_ = ObString::make_string("test_type");
      type.owner_ = ObString::make_string("org.test.owner");
      type.format_ = ObString::make_string("org.test.wire.v1"); type.format_version_ = 1;
    }
    if (kind) CHECK(source->set_plugin_type(type) == OB_SUCCESS);
    const auto check = [&](ObRawExpr *raw) {
      CHECK(raw && raw->is_exec_param_expr() && raw->get_data_type() == source->get_data_type());
      CHECK(static_cast<ObExecParamRawExpr *>(raw)->get_ref_expr() == source);
      CHECK(bool(raw->get_plugin_type()) == bool(kind));
      if (kind) {
        CHECK(*raw->get_plugin_type() == type && raw->get_plugin_type() != source->get_plugin_type());
        CHECK(raw->get_plugin_type()->logical_id_.ptr() != source->get_plugin_type()->logical_id_.ptr());
      }
    };
    // No formalize/type-deduction pass: planner helpers must return a complete
    // logical type immediately, including stored codec identity and epoch.
    for (bool onetime : {false, true}) {
      ObExecParamRawExpr *direct = nullptr;
      CHECK(ObRawExprUtils::create_new_exec_param(factory, source, direct, onetime) == OB_SUCCESS);
      check(direct); CHECK(direct->is_onetime() == onetime);
      ObRawExpr *indexed = source;
      CHECK(ObRawExprUtils::create_new_exec_param(&query, factory, indexed, onetime) == OB_SUCCESS);
      check(indexed); CHECK(static_cast<ObExecParamRawExpr *>(indexed)->is_onetime() == onetime);
    }
    ObSEArray<ObExecParamRawExpr *, 1> params;
    ObRawExpr *cached = nullptr, *reused = nullptr;
    CHECK(ObRawExprUtils::get_exec_param_expr(factory, &params, source, cached) == OB_SUCCESS);
    check(cached);
    CHECK(ObRawExprUtils::get_exec_param_expr(factory, &params, source, reused) == OB_SUCCESS);
    CHECK(reused == cached && params.count() == 1);
    source->clear_plugin_type();
    CHECK(bool(cached->get_plugin_type()) == bool(kind)); // Owned copy, not a source alias.
    if (kind) CHECK(*cached->get_plugin_type() == type);
  }
}

inline void stored_column(ObRawExprFactory &factory)
{
  ObColumnRefRawExpr *column = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
  for (const char *marker : {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER, SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2}) {
    oceanbase::share::schema::ObColumnSchemaV2 schema;
    schema.set_table_id(123); schema.set_column_id(456); schema.set_data_type(ObLongTextType);
    CHECK(schema.set_column_name("payload") == OB_SUCCESS);
    schema.set_collation_type(CS_TYPE_BINARY);
    ObSEArray<ObString, 7> fields;
    const char *generation = std::strcmp(marker, SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER) == 0 ? "99" : "0";
    for (const char *field : {marker, "payload", CUSTOM, "org.test", generation, "org.test.format", "1"}) {
      CHECK(fields.push_back(ObString::make_string(field)) == OB_SUCCESS);
    }
    CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
    CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
    const auto *identity = column->get_plugin_type();
    CHECK(identity && identity->stored_ && identity->logical_id_ == ObString::make_string(CUSTOM));
    CHECK(identity->format_ == ObString::make_string("org.test.format") && identity->format_version_ == 1);
    auto *consumer = call(factory, "consume", column);
    seekdb_plugin_sql_binding_v1_t binding = {};
    std::vector<std::string> arguments;
    CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_SUCCESS);
    CHECK(arguments == std::vector<std::string>{CUSTOM});
    auto wrong_format = *identity; ++wrong_format.format_version_;
    CHECK(column->set_plugin_type(wrong_format) == OB_SUCCESS);
    CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_STATE_NOT_MATCH);
    fields.at(6) = ObString::make_string("0");
    CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
    CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_INVALID_DATA);
    CHECK(!column->get_plugin_type());
    fields.reset();
    CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
    CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
    CHECK(!column->get_plugin_type());
  }
}

inline void explicit_sql_casts(ObRawExprFactory &factory, ObArenaAllocator &arena,
    ObSQLSessionInfo &session, Provider &provider, ObColumnRefRawExpr &stored_column,
    ObExecContext &outer_execution, InrowOnlyLobService &lob_service)
{
  provider.allow_numeric_casts_ = true;
  const auto parse = [&](const char *sql, ObRawExpr *source) {
    const ParseNode *node = nullptr;
    CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(sql),
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
        variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS);
    CHECK(raw && raw->get_expr_type() == T_FUN_SYS_CAST && raw->get_param_count() == 2);
    CHECK(CM_IS_EXPLICIT_CAST(raw->get_cast_mode()) && columns.count() == 1);
    // The expression resolver produces a column placeholder. Supply the
    // schema-resolved column before the real postorder type-deduction pass.
    CHECK(raw->get_param_expr(0) == columns.at(0).ref_expr_);
    raw->get_param_expr(0) = source;
    return raw;
  };
  for (bool stored : {false, true}) {
    for (const char *sql : {"CAST(payload AS BINARY(5))", "CONVERT(payload, BINARY(5))", "CAST(payload AS BINARY(3))",
                           "CAST(payload AS SIGNED)", "CAST(payload AS UNSIGNED)", "CAST(payload AS DOUBLE)"}) {
      ObColumnRefRawExpr *input = nullptr;
      CHECK(factory.create_raw_expr(T_REF_COLUMN, input) == OB_SUCCESS);
      CHECK(input->assign(stored_column) == OB_SUCCESS);
      if (!stored) {
        input->set_data_type(ObVarcharType);
        PluginExprType logical = *stored_column.get_plugin_type();
        logical.stored_ = false; logical.physical_type_ = ObVarcharType; logical.catalog_epoch_ = 11;
        CHECK(input->set_plugin_type(logical) == OB_SUCCESS);
        logical.catalog_epoch_ = 12;
        CHECK(input->set_plugin_type(logical) == OB_SUCCESS);
        ObRawExpr *stale_cast = parse(sql, input);
        CHECK(stale_cast->formalize(&session) == OB_STATE_NOT_MATCH);
        logical.catalog_epoch_ = 11;
        CHECK(input->set_plugin_type(logical) == OB_SUCCESS);
      }
      const int before_select = provider.cast_resolves_;
      ObRawExpr *raw = parse(sql, input);
      CHECK(provider.cast_resolves_ == before_select); // Not during name parsing.
      const int formalized = raw->formalize(&session);
      if (formalized != OB_SUCCESS) std::cerr << "SQL plugin cast formalize=" << formalized << " sql=" << sql << std::endl;
      CHECK(formalized == OB_SUCCESS);
      CHECK(provider.cast_resolves_ == before_select + 1);
      auto *converted = raw->get_param_expr(0);
      CHECK(converted && converted->get_expr_type() == T_FUN_SYS_PLUGIN_CAST);
      PluginCastExtraInfo selected(arena, T_FUN_SYS_PLUGIN_CAST);
      CHECK(PluginCastExpr::read_binding(*converted, selected) == OB_SUCCESS);
      CHECK(selected.binding_.requested_context == SEEKDB_PLUGIN_CAST_EXPLICIT);
      CHECK(selected.decode_source_ == (stored ? 1 : 0));
      CHECK(raw->deduce_type(&session) == OB_SUCCESS && raw->get_param_expr(0) == converted);
      CHECK(provider.cast_resolves_ == before_select + 1);
      ObExecContext execution(arena); execution.set_my_session(&session);
      execution.set_lob_read_service(&lob_service);
      CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
      ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
      ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
      ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
      ObExprFrameInfo frame(arena);
      CHECK(generator.generate(roots, frame) == OB_SUCCESS);
      CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
      CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
      ObEvalCtx eval(execution);
      ObExpr *root = nullptr, *column = nullptr;
      ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS);
      for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!column); column = &expr; }
      CHECK(root && column);
      std::vector<char> lob(sizeof(ObLobCommon) + 7);
      auto *header = new (lob.data()) ObLobCommon(); std::memcpy(header->buffer_, "E:hello", 7);
      if (stored) column->obj_meta_.set_has_lob_header();
      column->locate_expr_datum(eval).set_string(stored ? ObString(lob.size(), lob.data()) : ObString::make_string("hello"));
      column->get_eval_info(eval).evaluated_ = true;
      const int before_cast = provider.cast_calls_, before_decode = provider.codec_calls_;
      ObDatum *result = nullptr;
      CHECK(root->eval(eval, result) == OB_SUCCESS && result && !result->is_null());
      if (std::strstr(sql, "UNSIGNED")) {
        CHECK(root->datum_meta_.type_ == ObUInt64Type && result->get_uint() == UINT64_MAX);
      } else if (std::strstr(sql, "SIGNED")) {
        CHECK(root->datum_meta_.type_ == ObIntType && result->get_int() == -19);
      } else if (std::strstr(sql, "DOUBLE")) {
        CHECK(root->datum_meta_.type_ == ObDoubleType && result->get_double() == 3.25);
      } else {
        CHECK(result->get_string() == ObString::make_string(std::strstr(sql, "(3)") ? "hel" : "hello"));
      }
      CHECK(provider.cast_calls_ == before_cast + 1 && provider.codec_calls_ == before_decode + (stored ? 1 : 0));
      CHECK(provider.cast_resolves_ == before_select + 1);
      column->locate_expr_datum(eval).set_null();
      for (auto &expr : frame.rt_exprs_) {
        if (expr.type_ == T_FUN_SYS_CAST || expr.type_ == T_FUN_SYS_PLUGIN_CAST) expr.get_eval_info(eval).evaluated_ = false;
      }
      CHECK(root->eval(eval, result) == OB_SUCCESS && result->is_null());
      CHECK(provider.cast_calls_ == before_cast + 2 && provider.codec_calls_ == before_decode + (stored ? 1 : 0));
      CHECK(provider.cast_resolves_ == before_select + 1);
      ObSQLSessionInfo::ExecCtxSessionRegister restore(session, &outer_execution);
    }
  }
  provider.allow_assignment_cast_ = false;
  auto *unavailable = parse("CAST(payload AS BINARY(5))", &stored_column);
  CHECK(unavailable->formalize(&session) == OB_ERR_INVALID_TYPE_FOR_OP);
  CHECK(unavailable->get_param_expr(0) == &stored_column);
  provider.allow_assignment_cast_ = true;
  auto *unsupported = parse("CAST(payload AS DATE)", &stored_column);
  CHECK(unsupported->formalize(&session) == OB_NOT_SUPPORTED);
  CHECK(unsupported->get_param_expr(0) == &stored_column);
  auto *ordinary = parse("CAST(payload AS BINARY(5))", literal(factory, "hello"));
  const int before_builtin = provider.cast_resolves_;
  CHECK(ordinary->formalize(&session) == OB_SUCCESS && provider.cast_resolves_ == before_builtin);
}

inline void typed_values(ObRawExprFactory &factory, ObArenaAllocator &arena,
    ObSQLSessionInfo &session, Provider &provider, ObColumnRefRawExpr &stored_column,
    ObExecContext &outer_execution, InrowOnlyLobService &lob_service)
{
  // Unknown NULL, already-logical datum, encoded datum, and a registered
  // cross-type conversion all end in one typed, non-stored expression shape.
  for (int entry : {0, 1, 2, 3, 4}) {
  for (int mode : {0, 1, 2, 3}) {
    ObRawExpr *raw = nullptr;
    if (mode == 0) {
      ObConstRawExpr *null = nullptr;
      CHECK(factory.create_raw_expr(T_NULL, null) == OB_SUCCESS);
      ObObj value; value.set_null(); null->set_value(value); raw = null;
    } else {
      ObColumnRefRawExpr *column = nullptr;
      CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
      CHECK(column->assign(stored_column) == OB_SUCCESS);
      if (mode != 2) {
        column->set_data_type(ObVarcharType);
        if (mode == 1) {
          PluginExprType logical = *stored_column.get_plugin_type();
          logical.stored_ = false; logical.physical_type_ = ObVarcharType; logical.catalog_epoch_ = 11;
          CHECK(column->set_plugin_type(logical) == OB_SUCCESS);
        } else column->clear_plugin_type();
      }
      raw = column;
    }
    ObRawExpr *original = raw;
    const int casts_selected = provider.cast_resolves_;
    if (entry >= 2) {
      const int prior_types = provider.resolves_;
      const std::string source = mode == 0 ? "NULL" : "payload";
      const std::string sql = entry == 3 ? "CONVERT(" + source + ", payload)" :
          "CAST(" + source + " AS " + (entry == 4 ? "`payload`" : "payload") + ")";
      const ParseNode *node = nullptr;
      CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString(sql.size(), sql.data()),
          session.get_charsets4parser(), arena, node, session.get_sql_mode()) == OB_SUCCESS && node);
      CHECK(node->type_ == T_FUN_SYS_PLUGIN_TYPE_VALUE && node->num_child_ == 2 && node->children_[1]->type_ == T_IDENT);
      ObSEArray<ObQualifiedName, 1> columns;
      ObSEArray<ObVarInfo, 1> variables;
      ObSEArray<ObAggFunRawExpr *, 1> aggregates;
      ObSEArray<ObWinFunRawExpr *, 1> windows;
      ObSEArray<ObSubQueryInfo, 1> subqueries;
      ObSEArray<ObUDFInfo, 1> udfs;
      ObSEArray<ObOpRawExpr *, 1> operators;
      CHECK(ObRawExprUtils::build_raw_expr(factory, session, *node, raw, columns,
          variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS);
      CHECK(raw->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE && columns.count() == (mode == 0 ? 0 : 1));
      if (mode != 0) {
        CHECK(raw->get_param_expr(0) == columns.at(0).ref_expr_);
        raw->get_param_expr(0) = original;
      }
      CHECK(provider.resolves_ == prior_types && provider.cast_resolves_ == casts_selected);
      CHECK(raw->formalize(&session) == OB_SUCCESS);
    } else if (entry == 1) {
      const int prior_types = provider.resolves_;
      std::string name = "payload";
      CHECK(PluginTypeValueExpr::prepare(factory, ObString(name.size(), name.data()), raw) == OB_SUCCESS);
      name.assign("changed");
      CHECK(provider.resolves_ == prior_types && provider.cast_resolves_ == casts_selected);
      CHECK(raw->formalize(&session) == OB_SUCCESS);
    } else {
      CHECK(PluginTypeValueExpr::build(factory, ObString::make_string("payload"), raw, &session) == OB_SUCCESS);
    }
    CHECK(raw != original && raw->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE);
    CHECK(raw->get_data_type() == ObVarcharType && raw->get_plugin_type() && !raw->get_plugin_type()->stored_);
    CHECK(raw->get_plugin_type()->logical_id_ == ObString::make_string(CUSTOM));
    CHECK(provider.cast_resolves_ == casts_selected + (mode == 3 ? 1 : 0));
    const int type_resolves = provider.resolves_;
    CHECK(raw->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == type_resolves);
    CHECK(provider.cast_resolves_ == casts_selected + (mode == 3 ? 1 : 0));
    PluginTypeValueExtraInfo info(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE);
    CHECK(PluginTypeValueExpr::read_binding(*raw, info, 64) == OB_SIZE_OVERFLOW && !info.valid());
    CHECK(PluginTypeValueExpr::read_binding(*raw, info) == OB_SUCCESS && info.valid());
    CHECK(info.mode_ == (mode == 0 ? PluginTypeValueExtraInfo::TYPED_NULL :
        mode == 2 ? PluginTypeValueExtraInfo::DECODE : PluginTypeValueExtraInfo::IDENTITY));
    ObArenaAllocator plan_arena;
    ObRawExprFactory plan_factory(plan_arena);
    ObRawExpr *plan_copy = nullptr;
    CHECK(ObPLExprCopier::copy_expr(plan_factory, raw, plan_copy) == OB_SUCCESS);
    CHECK(PluginTypeValueExpr::read_binding(*plan_copy, info) == OB_SUCCESS);
    const auto &raw_wire = static_cast<ObConstRawExpr *>(raw->get_param_expr(1))->get_value().get_string();
    const auto &copy_wire = static_cast<ObConstRawExpr *>(plan_copy->get_param_expr(1))->get_value().get_string();
    CHECK(raw_wire == copy_wire && raw_wire.ptr() != copy_wire.ptr());
    std::vector<char> wire(info.get_serialize_size()); int64_t pos = 0;
    CHECK(info.serialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
    PluginTypeValueExtraInfo restored(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE); pos = 0;
    CHECK(restored.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS && restored.valid());
    for (size_t size = 0; size < wire.size(); ++size) {
      PluginTypeValueExtraInfo truncated(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE); int64_t cursor = 0;
      CHECK(truncated.deserialize(wire.data(), size, cursor) != OB_SUCCESS && !truncated.valid());
    }
    std::fill(wire.begin(), wire.end(), 'x');
    CHECK(restored.valid() && std::strcmp(restored.logical_id_, CUSTOM) == 0);
    ObIExprExtraInfo *extra_copy = nullptr;
    CHECK(restored.deep_copy(plan_arena, T_FUN_SYS_PLUGIN_TYPE_VALUE, extra_copy) == OB_SUCCESS);
    CHECK(static_cast<PluginTypeValueExtraInfo *>(extra_copy)->valid());
    restored.mode_ = 255; CHECK(!restored.valid());
    static_cast<PluginTypeValueExtraInfo *>(extra_copy)->~PluginTypeValueExtraInfo();

    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_lob_read_service(&lob_service);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr, *column = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS);
    for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) { CHECK(!column); column = &expr; }
    std::string bytes = "hello";
    std::vector<char> lob(sizeof(ObLobCommon) + 7);
    auto *header = new (lob.data()) ObLobCommon(); std::memcpy(header->buffer_, "E:hello", 7);
    CHECK((mode == 0) == (column == nullptr));
    if (column) {
      if (mode == 2) column->obj_meta_.set_has_lob_header();
      column->locate_expr_datum(eval).set_string(mode == 2 ? ObString(lob.size(), lob.data()) : ObString(bytes.size(), bytes.data()));
      column->get_eval_info(eval).evaluated_ = true;
    }
    const int calls = provider.cast_calls_, codecs = provider.codec_calls_;
    ObDatum *result = nullptr;
    CHECK(root->eval(eval, result) == OB_SUCCESS && result);
    CHECK((mode == 0) == result->is_null());
    if (mode != 0) {
      CHECK(result->get_string() == ObString::make_string("hello"));
      std::fill(bytes.begin(), bytes.end(), 'x'); std::fill(lob.begin(), lob.end(), 'x');
      CHECK(result->get_string() == ObString::make_string("hello")); // Own the result, not the child buffer.
    }
    CHECK(provider.cast_calls_ == calls + (mode == 3 ? 1 : 0));
    CHECK(provider.codec_calls_ == codecs + (mode == 2 ? 1 : 0));
    if (column) {
      column->locate_expr_datum(eval).set_null();
      for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_FUN_SYS_PLUGIN_TYPE_VALUE || expr.type_ == T_FUN_SYS_PLUGIN_CAST)
        expr.get_eval_info(eval).evaluated_ = false;
      CHECK(root->eval(eval, result) == OB_SUCCESS && result->is_null());
      CHECK(provider.cast_calls_ == calls + (mode == 3 ? 2 : 0));
      CHECK(provider.codec_calls_ == codecs + (mode == 2 ? 1 : 0));
    }
    if (mode == 2) {
      for (const std::string &payload : {std::string(), std::string("a\0b", 3)}) {
        const std::string encoded = "E:" + payload;
        lob.assign(sizeof(ObLobCommon) + encoded.size(), 0);
        header = new (lob.data()) ObLobCommon(); std::memcpy(header->buffer_, encoded.data(), encoded.size());
        column->locate_expr_datum(eval).set_string(ObString(lob.size(), lob.data()));
        root->get_eval_info(eval).evaluated_ = false;
        CHECK(root->eval(eval, result) == OB_SUCCESS && !result->is_null());
        CHECK(result->get_string() == ObString(payload.size(), payload.data()));
      }
      for (int failure = 1; failure <= 7; ++failure) {
        provider.codec_failure_ = failure; root->get_eval_info(eval).evaluated_ = false;
        CHECK(root->eval(eval, result) == (failure == 6 ? OB_STATE_NOT_MATCH : OB_INVALID_DATA));
      }
      provider.codec_failure_ = 0;
      CHECK(provider.cast_calls_ == calls); // No invented same-type cast callback.
    }
    CHECK(provider.resolves_ == type_resolves && provider.cast_resolves_ == casts_selected + (mode == 3 ? 1 : 0));
    ObSQLSessionInfo::ExecCtxSessionRegister restore(session, &outer_execution);
  }
  }
  ObRawExpr *bytes = literal(factory, "hello"), *unchanged = bytes;
  provider.mixed_epoch_ = true;
  CHECK(PluginTypeValueExpr::build(factory, ObString::make_string("payload"), bytes, &session) == OB_STATE_NOT_MATCH);
  CHECK(bytes == unchanged);
  provider.mixed_epoch_ = false;
  ObRawExpr *constructed = call(factory, "construct", literal(factory, "hello"));
  CHECK(constructed->deduce_type(&session) == OB_SUCCESS);
  CHECK(constructed->get_plugin_type()->catalog_epoch_ == 11);
  unchanged = constructed;
  provider.mixed_epoch_ = true; // TYPE is now epoch 12; the fixed function stays 11.
  CHECK(PluginTypeValueExpr::build(factory, ObString::make_string("payload"), constructed, &session) == OB_STATE_NOT_MATCH);
  CHECK(constructed == unchanged);
  provider.mixed_epoch_ = false;
  // Execution-only types do not need a durable codec merely to give NULL or
  // an existing logical datum a SQL type. A stored value still requires one.
  provider.nonpersistent_type_ = true;
  ObConstRawExpr *null = nullptr;
  CHECK(factory.create_raw_expr(T_NULL, null) == OB_SUCCESS);
  ObObj null_value; null_value.set_null(); null->set_value(null_value);
  ObRawExpr *typed_null = null;
  CHECK(PluginTypeValueExpr::build(factory, ObString::make_string("payload"), typed_null, &session) == OB_SUCCESS);
  CHECK(typed_null->get_data_type() == ObVarcharType && typed_null->get_plugin_type());
  ObRawExpr *stored = &stored_column;
  CHECK(PluginTypeValueExpr::build(factory, ObString::make_string("payload"), stored, &session) == OB_STATE_NOT_MATCH);
  CHECK(stored == &stored_column);
  provider.nonpersistent_type_ = false;
  ObCaseOpRawExpr *case_expr = nullptr;
  ObConstRawExpr *condition = nullptr;
  CHECK(factory.create_raw_expr(T_OP_CASE, case_expr) == OB_SUCCESS);
  CHECK(factory.create_raw_expr(T_INT, condition) == OB_SUCCESS);
  ObObj truth; truth.set_int(1); condition->set_value(truth);
  CHECK(case_expr->add_when_param_expr(condition) == OB_SUCCESS);
  CHECK(case_expr->add_then_param_expr(&stored_column) == OB_SUCCESS);
  case_expr->get_default_param_expr() = null;
  provider.allow_case_common_ = true;
  provider.case_epoch_ = 12;
  CHECK(case_expr->formalize(&session) == OB_STATE_NOT_MATCH);
  CHECK(case_expr->get_then_param_expr(0) == &stored_column && !case_expr->get_plugin_type());
  provider.case_epoch_ = 11;
  CHECK(case_expr->formalize(&session) == OB_SUCCESS);
  CHECK(case_expr->get_then_param_expr(0)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE);
  CHECK(case_expr->get_plugin_type() && !case_expr->get_plugin_type()->stored_ &&
      case_expr->get_plugin_type()->catalog_epoch_ == 11);
  const int bound_case_resolves = provider.resolves_;
  CHECK(case_expr->deduce_type(&session) == OB_SUCCESS && provider.resolves_ == bound_case_resolves);
  // Set preparation stages every projected column before publishing any.
  ObStmtFactory statements(arena);
  ObSelectStmt *left = nullptr, *right = nullptr;
  CHECK(statements.create_stmt(left) == OB_SUCCESS && statements.create_stmt(right) == OB_SUCCESS);
  SelectItem item; item.expr_ = &stored_column;
  CHECK(left->add_select_item(item) == OB_SUCCESS);
  item.expr_ = null; CHECK(right->add_select_item(item) == OB_SUCCESS);
  ObSEArray<ObSelectStmt *, 2> branches;
  CHECK(branches.push_back(left) == OB_SUCCESS && branches.push_back(right) == OB_SUCCESS);
  std::vector<PluginBranchType::Result> results;
  provider.case_epoch_ = 12;
  CHECK(PluginBranchType::prepare_set(&factory, &session, branches, false, results) == OB_STATE_NOT_MATCH);
  CHECK(results.empty() && left->get_select_item(0).expr_ == &stored_column);
  provider.case_epoch_ = 11;
  CHECK(PluginBranchType::prepare_set(&factory, &session, branches, true, results) == OB_NOT_SUPPORTED);
  CHECK(results.empty() && left->get_select_item(0).expr_ == &stored_column);
  CHECK(PluginBranchType::prepare_set(&factory, &session, branches, false, results, true) == OB_NOT_SUPPORTED);
  CHECK(results.empty() && left->get_select_item(0).expr_ == &stored_column);
  ObColumnRefRawExpr *stale = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, stale) == OB_SUCCESS);
  stale->set_data_type(ObVarcharType);
  PluginExprType stale_type = *case_expr->get_plugin_type(); stale_type.catalog_epoch_ = 12;
  CHECK(stale->set_plugin_type(stale_type) == OB_SUCCESS);
  item.expr_ = stale; CHECK(left->add_select_item(item) == OB_SUCCESS);
  item.expr_ = case_expr; CHECK(right->add_select_item(item) == OB_SUCCESS);
  CHECK(PluginBranchType::prepare_set(&factory, &session, branches, false, results) == OB_STATE_NOT_MATCH);
  CHECK(results.empty() && left->get_select_item(0).expr_ == &stored_column);
  CHECK(left->get_select_item(1).expr_ == stale && right->get_select_item(1).expr_ == case_expr);
  stale_type.catalog_epoch_ = 11; CHECK(stale->set_plugin_type(stale_type) == OB_SUCCESS);
  CHECK(PluginBranchType::prepare_set(&factory, &session, branches, false, results) == OB_SUCCESS);
  CHECK(results.size() == 2 && results[0].type_id_ == CUSTOM && results[0].epoch_ == 11);
  CHECK(left->get_select_item(0).expr_->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE);
  CHECK(!left->get_select_item(0).expr_->get_plugin_type()->stored_);
  provider.allow_case_common_ = false;
}

inline void named_sql_expressions(ObRawExprFactory &factory, ObArenaAllocator &arena,
    ObSQLSessionInfo &session, Provider &provider, ObExecContext &outer_execution,
    InrowOnlyLobService &lob_service)
{
  const auto parse = [&](const char *sql) {
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
        variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS && raw);
    CHECK(columns.empty());
    return raw;
  };
  // No substituted columns or prebuilt input datums: these literal and nested
  // expression trees are entirely produced by the real SQL parser/resolver.
  struct NamedCase {
    const char *sql;
    int casts;
    int functions;
    const char *bytes;
    bool length_result;
  };
  for (const NamedCase &test : {
      NamedCase{"CAST('hello' AS payload)", 1, 0, "hello", false},
      NamedCase{"CONVERT('hello', `payload`)", 1, 0, "hello", false},
      NamedCase{"CAST(CAST('hello' AS payload) AS payload)", 1, 0, "hello", false},
      NamedCase{"CAST(CAST('hello' AS payload) AS BINARY(5))", 2, 0, "hello", false},
      NamedCase{"CAST(CAST(NULL AS payload) AS payload)", 0, 0, nullptr, false},
      NamedCase{"consume(CAST(NULL AS payload))", 0, 1, nullptr, true},
      NamedCase{"consume(CAST('hello' AS payload))", 1, 1, "hello", true},
      NamedCase{"consume(CONVERT('', payload))", 1, 1, "", true},
      NamedCase{"CAST(construct('hello') AS payload)", 0, 1, "hello", false},
      NamedCase{"consume(CAST(construct('hello') AS payload))", 0, 2, "hello", true},
      NamedCase{"CONVERT(construct(NULL), payload)", 0, 1, nullptr, false}}) {
    const char *sql = test.sql;
    ObRawExpr *raw = parse(sql);
    const int selected = provider.cast_resolves_;
    const int formalized = raw->formalize(&session);
    if (formalized != OB_SUCCESS) std::cerr << "named SQL cast formalize=" << formalized << " sql=" << sql << std::endl;
    CHECK(formalized == OB_SUCCESS);
    const int cast_count = test.casts;
    CHECK(provider.cast_resolves_ == selected + cast_count);
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_lob_read_service(&lob_service);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
    ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
    ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
    ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
    CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
    ObEvalCtx eval(execution);
    ObExpr *root = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS);
    const int calls = provider.cast_calls_, codecs = provider.codec_calls_;
    const int functions = provider.calls_, resolved = provider.resolves_;
    const char *saved_bytes = provider.expected_bytes_;
    provider.expected_bytes_ = test.bytes;
    ObDatum *result = nullptr;
    CHECK(root->eval(eval, result) == OB_SUCCESS && result);
    if (!test.bytes) CHECK(result->is_null());
    else if (test.length_result) CHECK(!result->is_null() && result->get_int() == std::strlen(test.bytes));
    else CHECK(!result->is_null() && result->get_string() == ObString::make_string(test.bytes));
    CHECK(provider.cast_calls_ == calls + cast_count && provider.codec_calls_ == codecs);
    CHECK(provider.calls_ == functions + test.functions && provider.resolves_ == resolved);
    CHECK(provider.cast_resolves_ == selected + cast_count);
    provider.expected_bytes_ = saved_bytes;
    ObSQLSessionInfo::ExecCtxSessionRegister restore(session, &outer_execution);
  }
  CHECK(parse("CAST(NULL AS absent_type)")->formalize(&session) == OB_ERR_INVALID_DATATYPE);
  provider.allow_assignment_cast_ = false;
  CHECK(parse("CAST('hello' AS payload)")->formalize(&session) == OB_ERR_INVALID_TYPE_FOR_OP);
  CHECK(parse("CAST(CAST(NULL AS payload) AS payload)")->formalize(&session) == OB_SUCCESS);
  provider.allow_assignment_cast_ = true;
}

inline void table_functions(ObArenaAllocator &arena, ObRawExprFactory &factory,
                            ObSQLSessionInfo &session, Provider &provider)
{
  provider.table_enabled_ = true;
  ObExecContext execution(arena); execution.set_my_session(&session);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
  ObStmtFactory statements(arena);
  ObSchemaChecker checker;
  ObResolverParams params;
  params.allocator_ = &arena; params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
  params.query_ctx_ = statements.get_query_ctx(); params.session_info_ = &session; params.schema_checker_ = &checker;
  ObParser parser(arena, session.get_sql_mode()); ParseResult parsed{};
  CHECK(parser.parse(ObString::make_string("SELECT consume(payload), ordinal FROM TABLE(fixture_rows(construct('hello')))"), parsed) == OB_SUCCESS);
  ObSelectResolver resolver(params);
  const int resolved = resolver.resolve(*parsed.result_tree_->children_[0]);
  if (resolved != OB_SUCCESS) std::cerr << "table SELECT resolve=" << resolved
      << " descriptions=" << provider.table_describes_ << " last argument="
      << (provider.seen_.empty() ? "none" : provider.seen_.back()) << std::endl;
  CHECK(resolved == OB_SUCCESS);
  auto *stmt = resolver.get_select_stmt();
  CHECK(stmt && stmt->get_table_items().count() == 1);
  auto *raw = stmt->get_table_items().at(0)->function_table_expr_;
  CHECK(raw && raw->get_expr_type() == T_FUN_SYS_PLUGIN_TABLE_FUNCTION);
  PluginTableFunctionExtraInfo info(arena, T_FUN_SYS_PLUGIN_TABLE_FUNCTION);
  CHECK(PluginTableFunctionExpr::read_binding(*raw, info) == OB_SUCCESS && info.valid());
  CHECK(info.arguments_.at(0) == ObString::make_string(CUSTOM) && info.columns_.count() == 2);
  CHECK(provider.table_describes_ == 2);
  seekdb_plugin_sql_binding_v1_t binding = {};
  std::vector<std::string> arguments;
  auto *consumer = stmt->get_select_item(0).expr_;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*static_cast<ObSysFunRawExpr *>(consumer), binding, arguments) == OB_SUCCESS);
  CHECK(arguments == std::vector<std::string>{CUSTOM});
  const int lookups = provider.resolves_, descriptions = provider.table_describes_;
  auto *metadata = raw->get_param_expr(0);
  const auto wire = static_cast<ObConstRawExpr *>(metadata)->get_value().get_string();
  auto *bad_metadata = literal(factory, "bad");
  raw->get_param_expr(0) = bad_metadata;
  for (int64_t length = 0; length < wire.length(); ++length) {
    ObObj fragment; fragment.set_varchar(ObString(length, wire.ptr())); fragment.set_collation_type(CS_TYPE_BINARY);
    bad_metadata->set_value(fragment);
    CHECK(PluginTableFunctionExpr::read_binding(*raw, info) != OB_SUCCESS && !info.valid());
    CHECK(info.binding_.catalog_epoch == 0 && info.columns_.empty() && info.arguments_.empty());
  }
  raw->get_param_expr(0) = metadata;
  auto *input = raw->get_param_expr(1);
  raw->get_param_expr(1) = literal(factory, "hello");
  CHECK(PluginTableFunctionExpr::read_binding(*raw, info) == OB_STATE_NOT_MATCH);
  raw->get_param_expr(1) = input;
  ObArenaAllocator copied_arena;
  ObRawExprFactory copied_factory(copied_arena);
  ObRawExpr *copy = nullptr;
  CHECK(ObPLExprCopier::copy_expr(copied_factory, raw, copy) == OB_SUCCESS);
  CHECK(static_cast<ObConstRawExpr *>(copy->get_param_expr(0))->get_value().get_string().ptr() != wire.ptr());
  auto *saved_provider = oceanbase::share::g_mp; oceanbase::share::g_mp = nullptr;
  CHECK(copy->deduce_type(&session) == OB_SUCCESS);
  CHECK(PluginTableFunctionExpr::read_binding(*copy, info) == OB_SUCCESS && info.valid());
  oceanbase::share::g_mp = saved_provider;
  PluginExprType changed_table_type = *copy->get_plugin_type();
  changed_table_type.physical_type_ = ObIntType;
  CHECK(copy->set_plugin_type(changed_table_type) == OB_SUCCESS);
  CHECK(PluginTableFunctionExpr::read_binding(*copy, info) == OB_STATE_NOT_MATCH && !info.valid());
  copy->clear_plugin_type();
  CHECK(PluginTableFunctionExpr::read_binding(*copy, info) == OB_STATE_NOT_MATCH && !info.valid());
  provider.mixed_epoch_ = true;
  CHECK(raw->deduce_type(&session) == OB_SUCCESS);
  CHECK(PluginTableFunctionExpr::resolve_binding(*raw, binding) == OB_SUCCESS && binding.catalog_epoch == 11);
  CHECK(provider.resolves_ == lookups && provider.table_describes_ == descriptions);
  ObSEArray<ObString, 2> saved_names;
  CHECK(ObResolverUtils::get_all_function_table_column_names(*stmt->get_table_items().at(0), params, saved_names) == OB_SUCCESS);
  CHECK(saved_names.count() == 2 && saved_names.at(0) == ObString::make_string("payload") &&
      saved_names.at(1) == ObString::make_string("ordinal"));
  CHECK(ObResolverUtils::check_function_table_column_exist(*stmt->get_table_items().at(0), params,
      ObString::make_string("PAYLOAD")) == OB_SUCCESS);
  CHECK(ObResolverUtils::check_function_table_column_exist(*stmt->get_table_items().at(0), params,
      ObString::make_string("missing")) == OB_ERR_BAD_FIELD_ERROR);
  auto *saved_metadata = raw->get_param_expr(0);
  raw->get_param_expr(0) = bad_metadata;
  const int malformed_binding = PluginTableFunctionExpr::read_binding(*raw, info);
  CHECK(malformed_binding != OB_SUCCESS && malformed_binding != OB_ERR_BAD_FIELD_ERROR);
  CHECK(ObResolverUtils::check_function_table_column_exist(*stmt->get_table_items().at(0), params,
      ObString::make_string("payload")) == malformed_binding);
  raw->get_param_expr(0) = saved_metadata;
  CHECK(provider.resolves_ == lookups && provider.table_describes_ == descriptions);
  ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
  CHECK(roots.append(consumer) == OB_SUCCESS);
  CHECK(roots.append(stmt->get_select_item(1).expr_) == OB_SUCCESS);
  ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
  ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
  CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
  CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObEvalCtx eval(execution);
  const auto runtime_expr = [&](ObRawExpr *expr) {
    ObExpr *out = nullptr; ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*expr, outputs, out) == OB_SUCCESS && out);
    return out;
  };
  auto *table = runtime_expr(raw), *consume = runtime_expr(consumer);
  ObSEArray<ObExpr *, 2> columns;
  CHECK(columns.push_back(runtime_expr(consumer->get_param_expr(1))) == OB_SUCCESS);
  CHECK(columns.push_back(runtime_expr(stmt->get_select_item(1).expr_)) == OB_SUCCESS);
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_STATE_NOT_MATCH);
  CHECK(provider.table_opens_ == 0);
  provider.mixed_epoch_ = false;
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_STATE_NOT_MATCH);
  CHECK(provider.table_opens_ == 0);
  CHECK(PluginTableFunctionExpr::rescan(*table, eval) == OB_SUCCESS);
  for (int64_t expected = 1; expected <= 2; ++expected) {
    CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_SUCCESS);
    CHECK(columns.at(1)->locate_expr_datum(eval).get_int() == expected);
    consume->get_eval_info(eval).evaluated_ = false;
    ObDatum *result = nullptr;
    CHECK(consume->eval(eval, result) == OB_SUCCESS && result && result->get_int() == 5);
  }
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_ITER_END);
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_ITER_END);
  CHECK(provider.table_opens_ == 1 && provider.resolves_ == lookups && provider.table_describes_ == descriptions);
  CHECK(PluginTableFunctionExpr::rescan(*table, eval) == OB_SUCCESS && provider.table_closes_ == 1);
  provider.table_failure_ = 1;
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_INVALID_DATA);
  CHECK(provider.table_closes_ == 2);
  provider.table_failure_ = 0;
  const int failed_opens = provider.table_opens_;
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_INVALID_DATA);
  CHECK(provider.table_opens_ == failed_opens && provider.table_closes_ == 2);
  CHECK(PluginTableFunctionExpr::close(*table, eval) == OB_SUCCESS);
  CHECK(PluginTableFunctionExpr::close(*table, eval) == OB_SUCCESS && provider.table_closes_ == 2);
  provider.table_failure_ = 0; provider.table_enabled_ = false;
  CHECK(provider.resolves_ == lookups && provider.table_describes_ == descriptions);
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
}

inline void stored_table_function(ObArenaAllocator &arena, ObRawExprFactory &factory,
    ObSQLSessionInfo &session, Provider &provider, ObColumnRefRawExpr &stored,
    InrowOnlyLobService &lob_service)
{
  provider.table_enabled_ = true;
  ObExecContext execution(arena); execution.set_my_session(&session);
  execution.set_lob_read_service(&lob_service);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, &execution);
  ObColumnRefRawExpr *input = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, input) == OB_SUCCESS);
  CHECK(input->assign(stored) == OB_SUCCESS);
  ObSysFunRawExpr *raw = nullptr;
  CHECK(factory.create_raw_expr(T_FUN_SYS_PLUGIN_TABLE_FUNCTION, raw) == OB_SUCCESS);
  raw->set_func_name(ObString::make_string(PluginTableFunctionExpr::SQL_DISPATCH_NAME));
  CHECK(raw->init_param_exprs(2) == OB_SUCCESS);
  CHECK(raw->add_param_expr(literal(factory, "fixture_rows")) == OB_SUCCESS);
  CHECK(raw->add_param_expr(input) == OB_SUCCESS);
  CHECK(raw->formalize(&session) == OB_SUCCESS);
  CHECK(raw->get_param_expr(1)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE);
  CHECK(!raw->get_param_expr(1)->get_plugin_type()->stored_);
  PluginTableFunctionExtraInfo info(arena, T_FUN_SYS_PLUGIN_TABLE_FUNCTION);
  CHECK(PluginTableFunctionExpr::read_binding(*raw, info) == OB_SUCCESS);
  CHECK(info.arguments_.at(0) == ObString::make_string(CUSTOM));
  const int lookups = provider.resolves_, descriptions = provider.table_describes_;
  CHECK(raw->deduce_type(&session) == OB_SUCCESS);
  ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
  ObSEArray<ObColumnRefRawExpr *, 2> raw_columns;
  for (int64_t i = 0; i < info.columns_.count(); ++i) {
    ObColumnRefRawExpr *column = nullptr;
    CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
    column->set_ref_id(991, OB_APP_MIN_COLUMN_ID + i);
    ObExprResType physical;
    CHECK(PluginTableFunctionExpr::column_type(info.columns_.at(i), physical) == OB_SUCCESS);
    column->set_result_type(physical);
    CHECK(raw_columns.push_back(column) == OB_SUCCESS && roots.append(column) == OB_SUCCESS);
  }
  ObStaticEngineExprCG generator(arena, &session, nullptr, 0, 0);
  ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
  CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
  CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObEvalCtx eval(execution);
  const auto runtime_expr = [&](ObRawExpr *expr) {
    ObExpr *out = nullptr; ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*expr, outputs, out) == OB_SUCCESS && out);
    return out;
  };
  auto *table = runtime_expr(raw), *source = runtime_expr(input);
  ObSEArray<ObExpr *, 2> columns;
  for (int64_t i = 0; i < raw_columns.count(); ++i) CHECK(columns.push_back(runtime_expr(raw_columns.at(i))) == OB_SUCCESS);
  const std::string encoded = "E:hello";
  std::vector<char> lob_bytes(sizeof(ObLobCommon) + encoded.size());
  auto *lob = new (lob_bytes.data()) ObLobCommon();
  std::memcpy(lob->buffer_, encoded.data(), encoded.size());
  source->obj_meta_.set_has_lob_header();
  source->locate_expr_datum(eval).set_string(ObString(lob_bytes.size(), lob_bytes.data()));
  source->get_eval_info(eval).evaluated_ = true;
  const int codecs = provider.codec_calls_, opens = provider.table_opens_, closes = provider.table_closes_;
  CHECK(PluginTableFunctionExpr::fetch_row(*table, eval, columns) == OB_SUCCESS);
  CHECK(columns.at(0)->locate_expr_datum(eval).get_string() == ObString::make_string("hello"));
  CHECK(provider.codec_calls_ == codecs + 1 && provider.table_opens_ == opens + 1);
  CHECK(provider.resolves_ == lookups && provider.table_describes_ == descriptions);
  CHECK(PluginTableFunctionExpr::close(*table, eval) == OB_SUCCESS && provider.table_closes_ == closes + 1);
  provider.table_enabled_ = false;
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
}

inline void run()
{
  ownership();
  execution_parameter_types();
  Provider provider;
  ObArenaAllocator arena;
  ObRawExprFactory factory(arena);
  stored_column(factory);
  auto session = std::make_unique<ObSQLSessionInfo>();
  CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
  ObSchemaChecker schema_checker;
  ObStmtFactory statements(arena);
  ObResolverParams params;
  params.allocator_ = &arena; params.expr_factory_ = &factory;
  params.stmt_factory_ = &statements;
  params.session_info_ = session.get(); params.schema_checker_ = &schema_checker;
  ObQueryCtx query;
  params.query_ctx_ = &query;
  ProjectionResolver resolver(params);
  ObSelectStmt *source_ptr = nullptr, *projected_ptr = nullptr, *twice_ptr = nullptr;
  CHECK(statements.create_stmt(source_ptr) == OB_SUCCESS);
  CHECK(statements.create_stmt(projected_ptr) == OB_SUCCESS);
  CHECK(statements.create_stmt(twice_ptr) == OB_SUCCESS);
  auto &source = *source_ptr;
  auto &projected = *projected_ptr;
  auto &twice = *twice_ptr;
  source.set_query_ctx(&query); projected.set_query_ctx(&query); twice.set_query_ctx(&query);
  auto *constructor = call(factory, "construct", literal(factory, "hello"));
  SelectItem selected;
  selected.expr_ = constructor; selected.alias_name_ = ObString::make_string("typed_value");
  CHECK(source.add_select_item(selected) == OB_SUCCESS);
  TableItem table;
  table.type_ = TableItem::GENERATED_TABLE; table.table_id_ = 100;
  table.table_name_ = ObString::make_string("derived"); table.ref_query_ = &source;
  CHECK(projected.add_table_item(session.get(), &table) == OB_SUCCESS);
  ColumnItem *first = nullptr;
  CHECK(resolver.resolve_generated_table_column_item(table, selected.alias_name_, first, &projected) == OB_SUCCESS);
  CHECK(first && first->expr_->get_plugin_type());
  CHECK(first->expr_->get_plugin_type()->logical_id_ == ObString::make_string(CUSTOM));
  CHECK(first->expr_->get_plugin_type() != constructor->get_plugin_type());
  CHECK(first->expr_->get_plugin_type()->catalog_epoch_ == 11);
  SelectItem projected_item;
  projected_item.expr_ = first->expr_; projected_item.alias_name_ = selected.alias_name_;
  CHECK(projected.add_select_item(projected_item) == OB_SUCCESS);
  TableItem outer_table;
  outer_table.type_ = TableItem::GENERATED_TABLE; outer_table.table_id_ = 101;
  outer_table.table_name_ = ObString::make_string("twice"); outer_table.ref_query_ = &projected;
  CHECK(twice.add_table_item(session.get(), &outer_table) == OB_SUCCESS);
  ColumnItem *second = nullptr;
  CHECK(resolver.resolve_generated_table_column_item(outer_table, selected.alias_name_, second, &twice) == OB_SUCCESS);
  CHECK(second && second->expr_->get_plugin_type()->logical_id_ == ObString::make_string(CUSTOM));
  CHECK(second->expr_->get_plugin_type()->catalog_epoch_ == 11);
  auto *consumer = call(factory, "consume", second->expr_);
  seekdb_plugin_sql_binding_v1_t binding = {};
  std::vector<std::string> arguments;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_SUCCESS);
  CHECK(arguments == std::vector<std::string>{CUSTOM});
  CHECK(std::strcmp(binding.object_id, "org.test.consume.typed") == 0);
  ObRawExprTypeDemotion demotion(session.get(), &factory);
  ObRawExprDeduceType deducer(session.get(), &factory, false, nullptr, OB_INVALID_INDEX_INT64, demotion);
  ObQueryRefRawExpr *scalar = nullptr;
  CHECK(factory.create_raw_expr(T_REF_QUERY, scalar) == OB_SUCCESS);
  scalar->set_ref_stmt(&source); scalar->set_output_column(1);
  CHECK(deducer.visit(*scalar) == OB_ERR_UNEXPECTED); // Missing physical column metadata.
  CHECK(scalar->add_column_type(constructor->get_result_type()) == OB_SUCCESS);
  const int before_scalar = provider.resolves_;
  CHECK(deducer.visit(*scalar) == OB_SUCCESS && scalar->get_plugin_type());
  CHECK(*scalar->get_plugin_type() == *constructor->get_plugin_type());
  CHECK(scalar->get_plugin_type() != constructor->get_plugin_type());
  CHECK(provider.resolves_ == before_scalar);
  auto *scalar_consumer = call(factory, "consume", scalar);
  CHECK(PluginFunctionExpr::resolve_raw_binding(*scalar_consumer, binding, arguments) == OB_SUCCESS);
  CHECK(arguments == std::vector<std::string>{CUSTOM});
  PluginExprType changed_scalar_type = *scalar->get_plugin_type(); changed_scalar_type.catalog_epoch_ = 12;
  CHECK(scalar->set_plugin_type(changed_scalar_type) == OB_SUCCESS);
  CHECK(PluginFunctionExpr::resolve_raw_binding(*scalar_consumer, binding, arguments) == OB_STATE_NOT_MATCH);
  CHECK(deducer.visit(*scalar) == OB_SUCCESS && scalar->get_plugin_type()->catalog_epoch_ == 11);
  ObRawExpr *scalar_copy = nullptr;
  CHECK(ObRawExprCopier::copy_expr(factory, scalar, scalar_copy) == OB_SUCCESS);
  CHECK(scalar_copy->get_plugin_type() && *scalar_copy->get_plugin_type() == *scalar->get_plugin_type());
  CHECK(scalar_copy->get_plugin_type() != scalar->get_plugin_type());
  scalar->set_ref_stmt(nullptr);
  CHECK(deducer.visit(*scalar) == OB_STATE_NOT_MATCH);
  scalar->set_ref_stmt(&source);
  scalar->get_column_types().at(0).set_int();
  CHECK(deducer.visit(*scalar) == OB_STATE_NOT_MATCH); // A stale physical carrier must not be relabeled.
  scalar->get_column_types().at(0) = constructor->get_result_type();
  CHECK(deducer.visit(*scalar) == OB_SUCCESS);
  scalar->set_is_set(true);
  CHECK(deducer.visit(*scalar) == OB_SUCCESS && !scalar->get_plugin_type());
  scalar->set_is_set(false);
  CHECK(deducer.visit(*scalar) == OB_SUCCESS && scalar->get_plugin_type());
  auto *saved_output = source.get_select_item(0).expr_;
  auto *plain_output = literal(factory, "native");
  CHECK(plain_output->deduce_type(session.get()) == OB_SUCCESS);
  source.get_select_item(0).expr_ = plain_output;
  scalar->get_column_types().at(0) = plain_output->get_result_type();
  CHECK(deducer.visit(*scalar) == OB_SUCCESS && !scalar->get_plugin_type());
  source.get_select_item(0).expr_ = saved_output;
  ObAliasRefRawExpr *alias = nullptr;
  CHECK(factory.create_raw_expr(T_REF_ALIAS_COLUMN, alias) == OB_SUCCESS);
  alias->set_ref_expr(second->expr_);
  CHECK(deducer.visit(*alias) == OB_SUCCESS && alias->get_plugin_type());
  CHECK(alias->get_plugin_type()->catalog_epoch_ == 11);
  consumer->get_param_expr(1) = alias;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_SUCCESS);
  CHECK(arguments == std::vector<std::string>{CUSTOM});
  auto stale_alias_type = *alias->get_plugin_type();
  stale_alias_type.catalog_epoch_ = 12;
  CHECK(alias->set_plugin_type(stale_alias_type) == OB_SUCCESS);
  CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_STATE_NOT_MATCH);
  CHECK(binding.struct_size == 0);
  CHECK(deducer.visit(*alias) == OB_SUCCESS && alias->get_plugin_type()->catalog_epoch_ == 11);
  ObExecParamRawExpr *parameter = nullptr;
  CHECK(factory.create_raw_expr(T_QUESTIONMARK, parameter) == OB_SUCCESS);
  parameter->set_ref_expr(second->expr_);
  CHECK(deducer.visit(*parameter) == OB_SUCCESS && parameter->get_plugin_type());
  CHECK(parameter->get_plugin_type()->catalog_epoch_ == 11);
  consumer->get_param_expr(1) = parameter;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_SUCCESS);
  CHECK(arguments == std::vector<std::string>{CUSTOM});

  // Use the compiled logical type when the child is a projected datum, not a
  // nested plugin ObExpr. The controlled row below is not a storage/SQL scan.
  CHECK(consumer->deduce_type(session.get()) == OB_SUCCESS);
  CHECK(consumer->get_data_type() == ObIntType);
  PluginFunctionExpr op(arena);
  ObExprCGCtx cg(arena, nullptr, nullptr);
  ObExpr runtime, name, value;
  CHECK(op.cg_expr(cg, *consumer, runtime) == OB_SUCCESS);
  auto *info = dynamic_cast<PluginFunctionExtraInfo *>(runtime.extra_info_);
  CHECK(info && info->arguments().at(0) == ObString::make_string(CUSTOM));
  InrowOnlyLobService lob_service;
  ObExecContext execution(arena);
  execution.set_lob_read_service(&lob_service);
  CHECK(execution.init_expr_op(1) == OB_SUCCESS);
  ObEvalCtx eval(execution);
  alignas(16) char frame_data[3][256] = {};
  char *frames[3];
  ObExpr *nodes[] = {&name, &value, &runtime};
  for (uint32_t i = 0; i < 3; ++i) {
    frames[i] = frame_data[i]; nodes[i]->frame_idx_ = i;
    nodes[i]->datum_off_ = 0; nodes[i]->eval_info_off_ = 64;
    nodes[i]->res_buf_off_ = 128; nodes[i]->res_buf_len_ = 128;
    nodes[i]->datum_meta_.type_ = i == 2 ? ObIntType : ObVarcharType;
    new (frames[i]) ObDatum(); new (frames[i] + 64) ObEvalInfo();
  }
  eval.frames_ = frames;
  name.locate_expr_datum(eval).set_string(ObString::make_string("consume"));
  value.locate_expr_datum(eval).set_string(ObString::make_string("hello"));
  ObExpr *runtime_args[] = {&name, &value};
  runtime.args_ = runtime_args; runtime.arg_cnt_ = 2; runtime.expr_ctx_id_ = 0;
  const int resolves_before = provider.resolves_;
  ObDatum *result = nullptr;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result && result->get_int() == 5);
  CHECK(provider.resolves_ == resolves_before && provider.calls_ == 1);
  value.locate_expr_datum(eval).set_null();
  runtime.get_eval_info(eval).evaluated_ = false;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->is_null());
  CHECK(provider.resolves_ == resolves_before);
  info->~PluginFunctionExtraInfo(); runtime.extra_info_ = nullptr;

  // A stored argument has a real encoding: passing the raw seven bytes would
  // yield 7, while the codec removes E: and the consumer must observe 5.
  ObColumnRefRawExpr *stored_value = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, stored_value) == OB_SUCCESS);
  stored_value->set_ref_id(200, 456); stored_value->set_data_type(ObLongTextType);
  stored_value->set_collation_type(CS_TYPE_BINARY); stored_value->set_collation_level(CS_LEVEL_IMPLICIT);
  PluginExprType stored_type;
  stored_type.logical_id_ = ObString::make_string(CUSTOM); stored_type.physical_type_ = ObLongTextType;
  stored_type.stored_ = true; stored_type.sql_name_ = ObString::make_string("payload");
  stored_type.owner_ = ObString::make_string("org.test");
  stored_type.format_ = ObString::make_string("org.test.format"); stored_type.format_version_ = 1;
  CHECK(stored_value->set_plugin_type(stored_type) == OB_SUCCESS);
  consumer->get_param_expr(1) = stored_value;
  // A stored/logical representation change needs a new binding, not a silent
  // re-selection of the already compiled function's codec requirements.
  CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_STATE_NOT_MATCH);
  consumer = call(factory, "consume", stored_value);
  provider.mixed_epoch_ = true;
  CHECK(PluginFunctionExpr::resolve_raw_binding(*consumer, binding, arguments) == OB_STATE_NOT_MATCH);
  provider.mixed_epoch_ = false;
  CHECK(consumer->deduce_type(session.get()) == OB_SUCCESS);
  CHECK(op.cg_expr(cg, *consumer, runtime) == OB_SUCCESS);
  info = dynamic_cast<PluginFunctionExtraInfo *>(runtime.extra_info_);
  CHECK(info && info->stored().empty());
  CHECK(consumer->get_param_expr(1)->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_VALUE);
  PluginTypeValueExtraInfo explicit_decoder(arena, T_FUN_SYS_PLUGIN_TYPE_VALUE);
  CHECK(PluginTypeValueExpr::read_binding(*consumer->get_param_expr(1), explicit_decoder) == OB_SUCCESS);
  CHECK(explicit_decoder.mode_ == PluginTypeValueExtraInfo::DECODE);
  CHECK(info->binding(binding) == OB_SUCCESS);
  // The manually supplied frame below predates explicit decoder nodes. Keep
  // its legacy wire/copy/marshalling checks instead of silently dropping that
  // path when new bindings stop carrying sparse decoder metadata.
  CHECK(info->initialize(binding, {CUSTOM}, {explicit_decoder.source_}) == OB_SUCCESS);
  ObIExprExtraInfo *copied_base = nullptr;
  CHECK(info->deep_copy(arena, T_FUN_SYS_PLUGIN_FUNCTION, copied_base) == OB_SUCCESS);
  auto *copied = dynamic_cast<PluginFunctionExtraInfo *>(copied_base);
  CHECK(copied && copied->stored().count() == 1);
  CHECK(copied->stored().at(0).binding_.object_id != info->stored().at(0).binding_.object_id);
  std::vector<char> wire(info->get_serialize_size());
  int64_t pos = 0;
  CHECK(info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
  PluginFunctionExtraInfo decoded(arena, T_FUN_SYS_PLUGIN_FUNCTION);
  pos = 0;
  CHECK(decoded.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
  for (size_t size = 0; size < wire.size(); ++size) {
    PluginFunctionExtraInfo truncated(arena, T_FUN_SYS_PLUGIN_FUNCTION);
    pos = 0; CHECK(truncated.deserialize(wire.data(), size, pos) != OB_SUCCESS);
    CHECK(truncated.binding(binding) != OB_SUCCESS);
  }
  std::fill(wire.begin(), wire.end(), 'x');
  CHECK(decoded.stored().count() == 1 && decoded.binding(binding) == OB_SUCCESS);
  CHECK(std::strcmp(decoded.stored().at(0).binding_.object_id, CUSTOM) == 0);
  auto bad_stored = decoded.stored().at(0); bad_stored.index_ = 1;
  CHECK(copied->initialize(binding, {CUSTOM}, {bad_stored}) == OB_INVALID_DATA);
  CHECK(copied->binding(binding) != OB_SUCCESS);
  runtime.extra_info_ = &decoded;
  value.datum_meta_.type_ = ObLongTextType; value.datum_meta_.cs_type_ = CS_TYPE_BINARY;
  value.obj_meta_.set_type(ObLongTextType); value.obj_meta_.set_has_lob_header();
  const std::string encoded = "E:hello";
  std::vector<char> lob_bytes(sizeof(ObLobCommon) + encoded.size());
  auto *lob = new (lob_bytes.data()) ObLobCommon();
  std::memcpy(lob->buffer_, encoded.data(), encoded.size());
  value.locate_expr_datum(eval).set_string(ObString(lob_bytes.size(), lob_bytes.data()));
  runtime.get_eval_info(eval).evaluated_ = false;
  const int prior_resolves = provider.resolves_;
  const int prior_calls = provider.calls_;
  provider.expected_bytes_ = "hello";
  const int decoded_ret = runtime.eval(eval, result);
  if (decoded_ret != OB_SUCCESS || !result || result->is_null() || result->get_int() != 5) {
    std::cerr << "stored decode ret=" << decoded_ret << " codec calls=" << provider.codec_calls_
              << " function calls=" << provider.calls_ << std::endl;
  }
  CHECK(decoded_ret == OB_SUCCESS && result && !result->is_null() && result->get_int() == 5);
  CHECK(provider.codec_calls_ == 1 && provider.calls_ == prior_calls + 1);
  CHECK(provider.resolves_ == prior_resolves);
  for (int failure = 1; failure <= 7; ++failure) {
    provider.codec_failure_ = failure;
    runtime.get_eval_info(eval).evaluated_ = false;
    const int expected = failure == 6 ? OB_STATE_NOT_MATCH : OB_INVALID_DATA;
    CHECK(runtime.eval(eval, result) == expected);
    CHECK(provider.calls_ == prior_calls + 1 && provider.resolves_ == prior_resolves);
  }
  provider.codec_failure_ = 0;
  value.locate_expr_datum(eval).set_null(); runtime.get_eval_info(eval).evaluated_ = false;
  const int codecs_before_null = provider.codec_calls_;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->is_null());
  CHECK(provider.codec_calls_ == codecs_before_null && provider.resolves_ == prior_resolves);
  info->~PluginFunctionExtraInfo(); copied->~PluginFunctionExtraInfo();
  runtime.extra_info_ = nullptr;

  // Exercise the common assignment builder and the per-row INSERT VALUES
  // hook, not just a hand-created runtime encoder node.
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(*session, &execution);
  execution.set_my_session(session.get());
  ObRawExpr *assignment = call(factory, "construct", literal(factory, "hello"));
  const int assignment_ret = PluginTypeEncodeExpr::build(factory, *stored_value, assignment, session.get());
  if (assignment_ret != OB_SUCCESS) {
    std::cerr << "plugin assignment build ret=" << assignment_ret
              << " input type=" << assignment->get_data_type()
              << " logical=" << (assignment->get_plugin_type() != nullptr) << std::endl;
  }
  CHECK(assignment_ret == OB_SUCCESS);
  CHECK(assignment->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  CHECK(assignment->get_plugin_type() && assignment->get_plugin_type()->stored_);
  auto *once = assignment;
  CHECK(PluginTypeEncodeExpr::build(factory, *stored_value, assignment, session.get()) == OB_SUCCESS && assignment == once);
  ObRawExpr *raw_bytes = literal(factory, "hello");
  CHECK(PluginTypeEncodeExpr::build(factory, *stored_value, raw_bytes, session.get()) == OB_ERR_INVALID_TYPE_FOR_OP);
  CHECK(raw_bytes->get_expr_type() == T_VARCHAR);
  ObConstRawExpr *null_literal = nullptr;
  CHECK(factory.create_raw_expr(T_NULL, null_literal) == OB_SUCCESS);
  ObObj null_value; null_value.set_null(); null_literal->set_value(null_value);
  ObRawExpr *null_assignment = null_literal;
  CHECK(PluginTypeEncodeExpr::build(factory, *stored_value, null_assignment, session.get()) == OB_SUCCESS);
  CHECK(null_assignment->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  ObRawExpr *stored_copy = stored_value;
  CHECK(PluginTypeEncodeExpr::build(factory, *stored_value, stored_copy, session.get()) == OB_SUCCESS && stored_copy == stored_value);
  ColumnItem target_column; target_column.expr_ = stored_value;
  ObRawExpr *insert_value = call(factory, "construct", literal(factory, "hello"));
  CHECK(resolver.add_additional_function_according_to_type(&target_column, insert_value,
      T_INSERT_SCOPE, false, true) == OB_SUCCESS);
  CHECK(insert_value->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  const int column_conv_ret = ObRawExprUtils::build_column_conv_expr(factory, arena, *stored_value, assignment, session.get());
  if (column_conv_ret != OB_SUCCESS) std::cerr << "plugin column conversion ret=" << column_conv_ret << std::endl;
  CHECK(column_conv_ret == OB_SUCCESS);
  CHECK(assignment->get_expr_type() == T_FUN_COLUMN_CONV && assignment->get_plugin_type()->stored_);
  CHECK(assignment->get_plugin_type()->physical_type_ == ObLongTextType);
  const auto encoder_below_column_conversion = [](ObRawExpr *column_conversion) {
    ObRawExpr *input = column_conversion->get_param_expr(4);
    // The ordinary column conversion inserts its own physical varchar-to-LOB
    // cast. Keep that path: it is responsible for storage representation.
    for (int depth = 0; input && input->get_expr_type() == T_FUN_SYS_CAST && depth < 8; ++depth) {
      CHECK(input->has_flag(IS_OP_OPERAND_IMPLICIT_CAST));
      input = input->get_param_expr(0);
    }
    CHECK(input && input->get_expr_type() == T_FUN_SYS_PLUGIN_TYPE_ENCODE);
    return input;
  };
  CHECK(encoder_below_column_conversion(assignment) == once);
  for (const char *marker : {SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER, SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER_V2}) {
    oceanbase::share::schema::ObColumnSchemaV2 schema;
    schema.set_table_id(200); schema.set_column_id(456); schema.set_data_type(ObLongTextType);
    schema.set_collation_type(CS_TYPE_BINARY);
    CHECK(schema.set_column_name("payload") == OB_SUCCESS);
    ObSEArray<ObString, 7> fields;
    const char *generation = std::strcmp(marker, SEEKDB_PLUGIN_SQL_TYPE_METADATA_MARKER) == 0 ? "99" : "0";
    for (const char *field : {marker, "payload", CUSTOM, "org.test", generation, "org.test.format", "1"}) {
      CHECK(fields.push_back(ObString::make_string(field)) == OB_SUCCESS);
    }
    CHECK(schema.set_extended_type_info(fields) == OB_SUCCESS);
    ObRawExpr *schema_assignment = call(factory, "construct", literal(factory, "hello"));
    CHECK(ObRawExprUtils::build_column_conv_expr(factory, &schema, schema_assignment, session.get()) == OB_SUCCESS);
    CHECK(schema_assignment->get_expr_type() == T_FUN_COLUMN_CONV && schema_assignment->get_plugin_type()->stored_);
    CHECK(encoder_below_column_conversion(schema_assignment)->get_param_count() == 2);
    CHECK(schema_assignment->get_plugin_type()->physical_type_ == ObLongTextType);
    CHECK(schema_assignment->get_plugin_type()->catalog_epoch_ == 11);
  }

  PluginTypeEncodeExpr encode_op(arena);
  CHECK(insert_value->get_plugin_type()->catalog_epoch_ == 11);
  PluginTypeEncodeExtraInfo fixed_encoder(arena, T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  CHECK(PluginTypeEncodeExpr::read_binding(*insert_value, fixed_encoder) == OB_SUCCESS && fixed_encoder.valid());
  const int bound_encoder_resolves = provider.resolves_;
  provider.mixed_epoch_ = true;
  // A new encoder must reject a TYPE version different from its bound input.
  ObRawExpr *conflicting_input = insert_value->get_param_expr(0);
  auto *unchanged_input = conflicting_input;
  CHECK(PluginTypeEncodeExpr::build(factory, *stored_value, conflicting_input, session.get()) == OB_STATE_NOT_MATCH);
  CHECK(conflicting_input == unchanged_input && provider.resolves_ == bound_encoder_resolves + 1);
  const int after_conflict = provider.resolves_;
  // Existing binding remains readable even if the registry would choose a new
  // epoch, or the provider is unavailable. Execution still needs admission.
  CHECK(insert_value->deduce_type(session.get()) == OB_SUCCESS && provider.resolves_ == after_conflict);
  provider.mixed_epoch_ = false;
  auto *saved_provider = oceanbase::share::g_mp;
  oceanbase::share::g_mp = nullptr;
  CHECK(PluginTypeEncodeExpr::read_binding(*insert_value, fixed_encoder) == OB_SUCCESS);
  CHECK(encode_op.cg_expr(cg, *insert_value, runtime) == OB_SUCCESS);
  oceanbase::share::g_mp = saved_provider;
  CHECK(provider.resolves_ == after_conflict);
  auto *wire_expr = insert_value->get_param_expr(1);
  const ObString bound_wire = static_cast<ObConstRawExpr *>(wire_expr)->get_value().get_string();
  ObConstRawExpr *truncated = nullptr;
  CHECK(factory.create_raw_expr(T_VARCHAR, truncated) == OB_SUCCESS);
  insert_value->get_param_expr(1) = truncated;
  for (int64_t length = 0; length < bound_wire.length(); ++length) {
    ObObj fragment; fragment.set_varchar(ObString(length, bound_wire.ptr())); fragment.set_collation_type(CS_TYPE_BINARY);
    truncated->set_value(fragment);
    CHECK(PluginTypeEncodeExpr::read_binding(*insert_value, fixed_encoder) != OB_SUCCESS && !fixed_encoder.valid());
  }
  insert_value->get_param_expr(1) = wire_expr;
  PluginExprType saved_target = *insert_value->get_plugin_type();
  PluginExprType wrong_target = saved_target; wrong_target.catalog_epoch_ = 12;
  CHECK(insert_value->set_plugin_type(wrong_target) == OB_SUCCESS);
  CHECK(PluginTypeEncodeExpr::read_binding(*insert_value, fixed_encoder) == OB_STATE_NOT_MATCH && !fixed_encoder.valid());
  CHECK(insert_value->set_plugin_type(saved_target) == OB_SUCCESS);
  ObRawExpr *saved_child = insert_value->get_param_expr(0);
  insert_value->get_param_expr(0) = stored_value;
  CHECK(PluginTypeEncodeExpr::read_binding(*insert_value, fixed_encoder) == OB_STATE_NOT_MATCH && !fixed_encoder.valid());
  insert_value->get_param_expr(0) = saved_child;
  CHECK(PluginTypeEncodeExpr::read_binding(*insert_value, fixed_encoder) == OB_SUCCESS);
  ObRawExpr *encoder_raw_copy = nullptr;
  CHECK(ObRawExprCopier::copy_expr(factory, insert_value, encoder_raw_copy) == OB_SUCCESS);
  CHECK(PluginTypeEncodeExpr::read_binding(*encoder_raw_copy, fixed_encoder) == OB_SUCCESS);
  CHECK(fixed_encoder.target_.binding_.catalog_epoch == 11 && provider.resolves_ == after_conflict);
  encoder_raw_copy->clear_plugin_type();
  CHECK(PluginTypeEncodeExpr::read_binding(*encoder_raw_copy, fixed_encoder) == OB_STATE_NOT_MATCH);
  CHECK(!fixed_encoder.valid()); // Binary user input alone cannot forge an encoder.
  ObArenaAllocator owned_encoder_arena;
  ObRawExprFactory owned_encoder_factory(owned_encoder_arena);
  ObRawExpr *owned_encoder = nullptr;
  {
    ObArenaAllocator transient_arena;
    ObRawExprFactory transient_factory(transient_arena);
    ObRawExpr *transient = call(transient_factory, "construct", literal(transient_factory, "owned"));
    CHECK(PluginTypeEncodeExpr::build(transient_factory, *stored_value, transient, session.get()) == OB_SUCCESS);
    CHECK(ObPLExprCopier::copy_expr(owned_encoder_factory, transient, owned_encoder) == OB_SUCCESS);
    const auto &original = static_cast<ObConstRawExpr *>(transient->get_param_expr(1))->get_value();
    const auto &copied = static_cast<ObConstRawExpr *>(owned_encoder->get_param_expr(1))->get_value();
    CHECK(copied.is_varbinary() && copied.get_string().ptr() != original.get_string().ptr());
  }
  const int owned_resolves = provider.resolves_;
  oceanbase::share::g_mp = nullptr;
  CHECK(owned_encoder->deduce_type(session.get()) == OB_SUCCESS);
  CHECK(PluginTypeEncodeExpr::read_binding(*owned_encoder, fixed_encoder) == OB_SUCCESS);
  CHECK(fixed_encoder.valid() && fixed_encoder.target_.binding_.catalog_epoch == 11);
  oceanbase::share::g_mp = saved_provider;
  CHECK(provider.resolves_ == owned_resolves);
  auto *encode_info = dynamic_cast<PluginTypeEncodeExtraInfo *>(runtime.extra_info_);
  CHECK(encode_info && encode_info->target_.valid());
  ObIExprExtraInfo *encode_copy_base = nullptr;
  CHECK(encode_info->deep_copy(arena, T_FUN_SYS_PLUGIN_TYPE_ENCODE, encode_copy_base) == OB_SUCCESS);
  PluginTypeEncodeExtraInfo restored(arena, T_FUN_SYS_PLUGIN_TYPE_ENCODE);
  wire.assign(encode_info->get_serialize_size(), 0); pos = 0;
  CHECK(encode_info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS);
  pos = 0; CHECK(restored.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS);
  std::fill(wire.begin(), wire.end(), 'x');
  CHECK(restored.target_.valid() && std::strcmp(restored.target_.binding_.object_id, CUSTOM) == 0);
  runtime.extra_info_ = &restored;
  runtime.datum_meta_.type_ = ObVarcharType; runtime.datum_meta_.cs_type_ = CS_TYPE_BINARY;
  value.datum_meta_.type_ = ObVarcharType; value.obj_meta_.reset(); value.obj_meta_.set_varchar();
  value.locate_expr_datum(eval).set_string(ObString::make_string("hello"));
  // The hidden binding is compile-time metadata and must never be evaluated.
  ObExpr *encode_args[] = {&value, nullptr}; runtime.args_ = encode_args; runtime.arg_cnt_ = 2;
  runtime.get_eval_info(eval).evaluated_ = false;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->get_string() == ObString::make_string("E:hello"));
  CHECK(provider.encode_calls_ == 1);
  const std::string round_trip_bytes(result->get_string().ptr(), result->get_string().length());
  for (int failure = 1; failure <= 7; ++failure) {
    provider.codec_failure_ = failure; runtime.get_eval_info(eval).evaluated_ = false;
    CHECK(runtime.eval(eval, result) == (failure == 6 ? OB_STATE_NOT_MATCH : OB_INVALID_DATA));
  }
  provider.codec_failure_ = 0;
  const int encoded_before_null = provider.encode_calls_;
  value.locate_expr_datum(eval).set_null(); runtime.get_eval_info(eval).evaluated_ = false;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->is_null());
  CHECK(provider.encode_calls_ == encoded_before_null);
  encode_info->~PluginTypeEncodeExtraInfo();
  static_cast<PluginTypeEncodeExtraInfo *>(encode_copy_base)->~PluginTypeEncodeExtraInfo();
  runtime.extra_info_ = nullptr;

  // Feed the actual encoder result back through the real in-row LOB reader and
  // bound decoder. Neither a raw-byte pass-through nor double encoding passes.
  CHECK(op.cg_expr(cg, *consumer, runtime) == OB_SUCCESS);
  auto *round_trip_info = dynamic_cast<PluginFunctionExtraInfo *>(runtime.extra_info_);
  CHECK(round_trip_info && round_trip_info->binding(binding) == OB_SUCCESS);
  CHECK(round_trip_info->initialize(binding, {CUSTOM}, {explicit_decoder.source_}) == OB_SUCCESS);
  runtime.datum_meta_.type_ = ObIntType;
  runtime.args_ = runtime_args; runtime.arg_cnt_ = 2;
  value.datum_meta_.type_ = ObLongTextType;
  value.obj_meta_.set_type(ObLongTextType); value.obj_meta_.set_has_lob_header();
  lob_bytes.assign(sizeof(ObLobCommon) + round_trip_bytes.size(), 0);
  lob = new (lob_bytes.data()) ObLobCommon();
  std::memcpy(lob->buffer_, round_trip_bytes.data(), round_trip_bytes.size());
  value.locate_expr_datum(eval).set_string(ObString(lob_bytes.size(), lob_bytes.data()));
  runtime.get_eval_info(eval).evaluated_ = false;
  const int before_round_trip = provider.codec_calls_;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && !result->is_null() && result->get_int() == 5);
  CHECK(provider.codec_calls_ == before_round_trip + 1);
  static_cast<PluginFunctionExtraInfo *>(runtime.extra_info_)->~PluginFunctionExtraInfo();
  runtime.extra_info_ = nullptr;

  // Cast selection belongs to planning. Inference, copies, serialization and
  // evaluation must retain that binding rather than consult the catalog again.
  provider.allow_assignment_cast_ = true;
  PluginCastExpr cast_op(arena);
  ObRawExpr *cast_raw = nullptr;
  {
    ObArenaAllocator source_arena;
    ObRawExprFactory source_factory(source_arena);
    ObRawExpr *source = literal(source_factory, "hello");
    const int selections = provider.cast_resolves_;
    const int cast_build_ret = PluginCastExpr::build(source_factory, ObString::make_string(CUSTOM),
        SEEKDB_PLUGIN_CAST_ASSIGNMENT, source, session.get());
    if (cast_build_ret != OB_SUCCESS) {
      std::cerr << "plugin cast build ret=" << cast_build_ret << " selections=" << provider.cast_resolves_ - selections
                << " source type=" << source->get_data_type() << std::endl;
    }
    CHECK(cast_build_ret == OB_SUCCESS);
    CHECK(source->get_expr_type() == T_FUN_SYS_PLUGIN_CAST && source->has_flag(IS_STATE_FUNC));
    CHECK(provider.cast_resolves_ == selections + 1);
    CHECK(source->deduce_type(session.get()) == OB_SUCCESS);
    // The ordinary optimizer copier intentionally shares constant attributes
    // inside one query arena; the PL copier owns attributes across arenas.
    ObRawExpr *local_copy = nullptr;
    CHECK(ObRawExprCopier::copy_expr(source_factory, source, local_copy) == OB_SUCCESS);
    PluginCastExtraInfo local_binding(source_arena, T_FUN_SYS_PLUGIN_CAST);
    CHECK(PluginCastExpr::read_binding(*local_copy, local_binding) == OB_SUCCESS);
    CHECK(ObPLExprCopier::copy_expr(factory, source, cast_raw) == OB_SUCCESS);
    const auto &original_wire = static_cast<ObConstRawExpr *>(source->get_param_expr(1))->get_value();
    const auto &copied_wire = static_cast<ObConstRawExpr *>(cast_raw->get_param_expr(1))->get_value();
    CHECK(original_wire.get_string() == copied_wire.get_string());
    CHECK(original_wire.get_string().ptr() != copied_wire.get_string().ptr());
    CHECK(provider.cast_resolves_ == selections + 1);
  }
  const int selections = provider.cast_resolves_;
  CHECK(cast_raw->deduce_type(session.get()) == OB_SUCCESS);
  CHECK(cast_raw->get_plugin_type()->logical_id_ == ObString::make_string(CUSTOM));
  CHECK(cast_op.cg_expr(cg, *cast_raw, runtime) == OB_SUCCESS);
  auto *cast_info = dynamic_cast<PluginCastExtraInfo *>(runtime.extra_info_);
  CHECK(cast_info && cast_info->valid() && !cast_info->decode_source_);
  ObIExprExtraInfo *cast_copy_base = nullptr;
  CHECK(cast_info->deep_copy(arena, T_FUN_SYS_PLUGIN_CAST, cast_copy_base) == OB_SUCCESS);
  auto *cast_copy = dynamic_cast<PluginCastExtraInfo *>(cast_copy_base);
  CHECK(cast_copy && cast_copy->valid() && cast_copy->binding_.object_id != cast_info->binding_.object_id);
  wire.assign(cast_info->get_serialize_size(), 0); pos = 0;
  CHECK(cast_info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
  PluginCastExtraInfo cast_restored(arena, T_FUN_SYS_PLUGIN_CAST);
  pos = 0;
  CHECK(cast_restored.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == int64_t(wire.size()));
  for (size_t size = 0; size < wire.size(); ++size) {
    PluginCastExtraInfo truncated(arena, T_FUN_SYS_PLUGIN_CAST);
    int64_t cursor = 0;
    CHECK(truncated.deserialize(wire.data(), size, cursor) != OB_SUCCESS);
    CHECK(!truncated.valid());
  }
  std::fill(wire.begin(), wire.end(), 'x');
  CHECK(cast_restored.valid() && std::strcmp(cast_restored.binding_.target_type_id, CUSTOM) == 0);
  cast_copy->binding_.reserved[0] = 1; CHECK(!cast_copy->valid());
  cast_copy->binding_.reserved[0] = 0;
  cast_copy->binding_.declared_context = SEEKDB_PLUGIN_CAST_EXPLICIT; CHECK(!cast_copy->valid());
  auto *cast_child = cast_raw->get_param_expr(0);
  cast_raw->get_param_expr(0) = stored_value;
  PluginCastExtraInfo invalid_cast(arena, T_FUN_SYS_PLUGIN_CAST);
  CHECK(PluginCastExpr::read_binding(*cast_raw, invalid_cast) == OB_STATE_NOT_MATCH && !invalid_cast.valid());
  cast_raw->get_param_expr(0) = cast_child;
  auto *cast_metadata = static_cast<ObConstRawExpr *>(cast_raw->get_param_expr(1));
  const ObObj saved_metadata = cast_metadata->get_value();
  CHECK(saved_metadata.is_varbinary());
  ObObj text_metadata = saved_metadata; text_metadata.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  cast_metadata->set_value(text_metadata);
  CHECK(PluginCastExpr::read_binding(*cast_raw, invalid_cast) == OB_INVALID_DATA && !invalid_cast.valid());
  cast_metadata->set_value(saved_metadata);
  CHECK(PluginCastExpr::read_binding(*cast_raw, invalid_cast) == OB_SUCCESS);
  runtime.extra_info_ = &cast_restored;
  runtime.datum_meta_.type_ = ObVarcharType; runtime.datum_meta_.cs_type_ = CS_TYPE_UTF8MB4_BIN;
  value.datum_meta_.type_ = ObVarcharType; value.obj_meta_.reset(); value.obj_meta_.set_varchar();
  value.locate_expr_datum(eval).set_string(ObString::make_string("hello"));
  ObExpr *cast_args[] = {&value, &name}; runtime.args_ = cast_args; runtime.arg_cnt_ = 2;
  runtime.get_eval_info(eval).evaluated_ = false;
  const int casts_before = provider.cast_calls_;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->get_string() == ObString::make_string("hello"));
  CHECK(provider.cast_calls_ == casts_before + 1 && provider.cast_resolves_ == selections);
  for (int failure = 1; failure <= 7; ++failure) {
    provider.cast_failure_ = failure; runtime.get_eval_info(eval).evaluated_ = false;
    CHECK(runtime.eval(eval, result) == (failure == 6 ? OB_STATE_NOT_MATCH : OB_INVALID_DATA));
  }
  provider.cast_failure_ = 0;
  value.locate_expr_datum(eval).set_null(); runtime.get_eval_info(eval).evaluated_ = false;
  const int casts_before_null = provider.cast_calls_;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->is_null());
  CHECK(provider.cast_calls_ == casts_before_null + 1 && provider.cast_resolves_ == selections);
  ObRawExpr *unknown_null = null_literal;
  CHECK(PluginCastExpr::build(factory, ObString::make_string(CUSTOM), SEEKDB_PLUGIN_CAST_ASSIGNMENT,
      unknown_null, session.get()) == OB_SUCCESS && unknown_null == null_literal);
  CHECK(provider.cast_resolves_ == selections);
  cast_info->~PluginCastExtraInfo(); cast_copy->~PluginCastExtraInfo(); runtime.extra_info_ = nullptr;

  // A persistent source must be decoded before casting. Bind codec and cast
  // from one catalog epoch, including when the resulting plan is serialized.
  ObRawExpr *stored_cast = stored_value;
  provider.mixed_epoch_ = true;
  CHECK(PluginCastExpr::build(factory, ObString::make_string("core.type.bytes"),
      SEEKDB_PLUGIN_CAST_EXPLICIT, stored_cast, session.get()) == OB_STATE_NOT_MATCH);
  CHECK(stored_cast == stored_value);
  provider.mixed_epoch_ = false;
  CHECK(PluginCastExpr::build(factory, ObString::make_string("core.type.bytes"),
      SEEKDB_PLUGIN_CAST_EXPLICIT, stored_cast, session.get()) == OB_SUCCESS);
  CHECK(cast_op.cg_expr(cg, *stored_cast, runtime) == OB_SUCCESS);
  cast_info = dynamic_cast<PluginCastExtraInfo *>(runtime.extra_info_);
  CHECK(cast_info && cast_info->decode_source_ == 1 && cast_info->valid());
  wire.assign(cast_info->get_serialize_size(), 0); pos = 0;
  CHECK(cast_info->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS);
  pos = 0; CHECK(cast_restored.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS);
  std::fill(wire.begin(), wire.end(), 'x');
  CHECK(cast_restored.valid() && cast_restored.decode_source_ == 1);
  runtime.extra_info_ = &cast_restored;
  value.datum_meta_.type_ = ObLongTextType; value.obj_meta_.set_type(ObLongTextType);
  value.obj_meta_.set_has_lob_header();
  value.locate_expr_datum(eval).set_string(ObString(lob_bytes.size(), lob_bytes.data()));
  runtime.get_eval_info(eval).evaluated_ = false;
  const int before_cast_decode = provider.codec_calls_, before_stored_cast = provider.cast_calls_;
  const int stored_selections = provider.cast_resolves_, codec_selections = provider.resolves_;
  CHECK(runtime.eval(eval, result) == OB_SUCCESS && result->get_string() == ObString::make_string("hello"));
  CHECK(provider.codec_calls_ == before_cast_decode + 1 && provider.cast_calls_ == before_stored_cast + 1);
  for (int failure = 1; failure <= 7; ++failure) {
    provider.codec_failure_ = failure; runtime.get_eval_info(eval).evaluated_ = false;
    CHECK(runtime.eval(eval, result) == (failure == 6 ? OB_STATE_NOT_MATCH : OB_INVALID_DATA));
    CHECK(provider.cast_calls_ == before_stored_cast + 1);
  }
  provider.codec_failure_ = 0;
  CHECK(provider.cast_resolves_ == stored_selections && provider.resolves_ == codec_selections);
  cast_info->~PluginCastExtraInfo(); runtime.extra_info_ = nullptr;

  // Generate and execute the entire constructor/cast -> encoder -> physical cast
  // -> column-convert tree with production frame allocation/code generation.
  // The codec provider is controlled; this is not a storage write or SQL server.
  ObAccuracy column_accuracy; column_accuracy.set_length(1024);
  stored_value->set_accuracy(column_accuracy);
  // Mode 2 composes stored decode -> explicit cast -> assignment cast ->
  // encode, using a runtime column datum, not a pre-decoded fake literal.
  for (int mode : {0, 1, 2}) {
  const bool use_assignment_cast = mode != 0;
  for (const std::string &payload : {std::string("hello"), std::string(), std::string("a\0b", 3), std::string(600, 'x')}) {
    ObExecContext generated_execution(arena);
    generated_execution.set_my_session(session.get());
    generated_execution.set_lob_read_service(&lob_service);
    CHECK(generated_execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister register_generated(*session, &generated_execution);
    auto *input_literal = literal(factory, "");
    ObObj input_object;
    input_object.set_varchar(ObString(payload.size(), payload.data()));
    input_object.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    input_literal->set_value(input_object);
    ObRawExpr *generated_assignment = use_assignment_cast ? static_cast<ObRawExpr *>(input_literal)
                                                         : call(factory, "construct", input_literal);
    if (mode == 2) {
      generated_assignment = stored_value;
      CHECK(PluginCastExpr::build(factory, ObString::make_string("core.type.bytes"),
          SEEKDB_PLUGIN_CAST_EXPLICIT, generated_assignment, session.get()) == OB_SUCCESS);
    }
    const int before_selection = provider.cast_resolves_;
    CHECK(ObRawExprUtils::build_column_conv_expr(factory, arena, *stored_value,
        generated_assignment, session.get()) == OB_SUCCESS);
    CHECK(provider.cast_resolves_ == before_selection + (use_assignment_cast ? 1 : 0));
    CHECK(encoder_below_column_conversion(generated_assignment)->get_param_expr(0)->get_expr_type() ==
        (use_assignment_cast ? T_FUN_SYS_PLUGIN_CAST : T_FUN_SYS_PLUGIN_FUNCTION));
    const int fixed_assignment_resolves = provider.resolves_;
    CHECK(generated_assignment->deduce_type(session.get()) == OB_SUCCESS);
    CHECK(provider.resolves_ == fixed_assignment_resolves);
    ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0);
    // generate() owns the IS_MARKED-based flattening pass; do not mark the root
    // as already flattened while merely assembling its input list.
    ObRawExprUniqueSet roots(false);
    CHECK(roots.append(generated_assignment) == OB_SUCCESS);
    ObExprFrameInfo frame(arena);
    const int generated_ret = generator.generate(roots, frame);
    if (generated_ret != OB_SUCCESS) std::cerr << "plugin assignment full codegen ret=" << generated_ret << std::endl;
    CHECK(generated_ret == OB_SUCCESS);
    CHECK(provider.resolves_ == fixed_assignment_resolves);
    CHECK(generated_execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(frame.pre_alloc_exec_memory(generated_execution) == OB_SUCCESS);
    ObEvalCtx generated_eval(generated_execution);
    ObExpr *column_conversion = nullptr;
    ObExpr *stored_input = nullptr;
    for (auto &node : frame.rt_exprs_) {
      if (node.type_ == T_FUN_COLUMN_CONV) {
        CHECK(!column_conversion);
        column_conversion = &node;
      }
      if (node.type_ == T_REF_COLUMN) {
        CHECK(mode == 2 && !stored_input);
        stored_input = &node;
      }
    }
    std::vector<char> generated_lob;
    if (mode == 2) {
      CHECK(stored_input && stored_input->datum_meta_.type_ == ObLongTextType);
      const std::string bytes = "E:" + payload;
      generated_lob.resize(sizeof(ObLobCommon) + bytes.size());
      auto *header = new (generated_lob.data()) ObLobCommon();
      std::memcpy(header->buffer_, bytes.data(), bytes.size());
      stored_input->obj_meta_.set_has_lob_header();
      stored_input->locate_expr_datum(generated_eval).set_string(ObString(generated_lob.size(), generated_lob.data()));
      stored_input->get_eval_info(generated_eval).evaluated_ = true;
    }
    CHECK(column_conversion && column_conversion->datum_meta_.type_ == ObLongTextType);
    const int before_encode = provider.encode_calls_;
    const int before_cast = provider.cast_calls_;
    const int before_decode = provider.codec_calls_;
    ObDatum *stored_result = nullptr;
    const int evaluated_ret = column_conversion->eval(generated_eval, stored_result);
    if (evaluated_ret != OB_SUCCESS) std::cerr << "plugin assignment full evaluation ret=" << evaluated_ret << std::endl;
    CHECK(evaluated_ret == OB_SUCCESS && stored_result && !stored_result->is_null());
    CHECK(provider.encode_calls_ == before_encode + 1);
    CHECK(provider.cast_calls_ == before_cast + mode);
    CHECK(provider.codec_calls_ == before_decode + (mode == 2 ? 1 : 0));
    CHECK(provider.cast_resolves_ == before_selection + (use_assignment_cast ? 1 : 0));
    CHECK(provider.resolves_ == fixed_assignment_resolves);
    ObString stored_bytes;
    CHECK(ObTextStringHelper::read_real_string_data_with_copy(generated_execution, arena,
        *stored_result, column_conversion->datum_meta_, column_conversion->obj_meta_.has_lob_header(),
        stored_bytes) == OB_SUCCESS);
    const std::string expected = "E:" + payload;
    CHECK(stored_bytes == ObString(expected.size(), expected.data()));
    ObSQLSessionInfo::ExecCtxSessionRegister restore_execution(*session, &execution);
  }
  }
  explicit_sql_casts(factory, arena, *session, provider, *stored_value, execution, lob_service);
  typed_values(factory, arena, *session, provider, *stored_value, execution, lob_service);
  named_sql_expressions(factory, arena, *session, provider, execution, lob_service);
  table_functions(arena, factory, *session, provider);
  stored_table_function(arena, factory, *session, provider, *stored_value, lob_service);
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(*session, nullptr);
}
} // namespace plugin_projection_test
#endif
