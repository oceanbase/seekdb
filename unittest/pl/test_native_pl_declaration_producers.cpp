/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "common/datum/sql_type.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "lib/rc/context.h"
#include "pl/ob_pl.h"
#include "pl/ob_pl_build.h"
#include "pl/ob_pl_package_manager.h"
#include "pl/ob_pl_resolver.h"
#include "pl/ob_pl_user_type.h"
#include "pl/parser/ob_pl_parser.h"
#include "query/plan_cache/ob_plan_cache_access_service.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/ob_result_set.h"
#include "sql/ob_spi.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/session/ob_sql_session_info.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>

namespace oceanbase
{
namespace share
{
namespace schema
{
// Existing schema-guard friend seam. This supplies only the final anonymous
// compilation's snapshot query. It cannot resolve a table or declaration:
// schema_service_ and schema_mgr_ remain null. TableRow tests pass the real
// ObTableSchema to its production builder, not through a lookup substitute.
class MockSchemaService
{
public:
  static int set_snapshot_only(ObSchemaGetterGuard &guard, int64_t version)
  {
    int ret = guard.init();
    if (ret == common::OB_SUCCESS) {
      ObSchemaMgrHandle handle;
      ObRefreshSchemaStatus status;
      const ObSchemaMgr *manager = nullptr;
      ret = guard.schema_mgr_infos_.push_back(ObSchemaMgrInfo(version, manager, handle, status));
    }
    return ret;
  }
};
}
}
}

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::pl;
using namespace oceanbase::share::schema;
namespace lib = oceanbase::lib;

namespace
{
constexpr int64_t SNAPSHOT = 701;
constexpr uint64_t TABLE_ID = 600123;
constexpr int64_t SCHEMA_V1 = 101;
constexpr int64_t SCHEMA_V2 = 102;
constexpr const char *ONE_CURSOR = "BEGIN DECLARE c CURSOR FOR SELECT 17 AS x; END";
constexpr const char *TWO_CURSORS =
    "BEGIN DECLARE c CURSOR FOR SELECT 17 AS x; DECLARE d CURSOR FOR SELECT 29 AS y; END";

class PlanAccess final : public oceanbase::query::ObIPlanCacheAccessService
{
public:
  void enter_access() override {}
  void leave_access() override {}
  void check_current_thread() override {}
  int get_global_safe_timestamp(int64_t &timestamp) const override
  { timestamp = 0; return OB_SUCCESS; }
};

// Only the existing embedded-SQL service is replaced. The PL parser, SQL
// prepare parser, resolver, declaration allocation, scope binding, CG and
// compile_type_table below are production implementations. In particular this
// adapter never receives or edits a Record definition, field or identity.
class PrepareSqlOnly final : public ObIPLSqlRuntime
{
public:
  int prepare_pl_sql(const ObString &sql, ObSPIService::PLPrepareCtx &ctx,
                    ObSPIService::PLPrepareResult &result, ParamStore * = nullptr) override
  {
    ++prepare_calls_;
    int ret = OB_SUCCESS;
    if (!sql.trim().prefix_match_ci("SELECT ")) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(result.init(ctx.sess_info_, access_))) {
    } else {
      result.result_set_->set_stmt_type(stmt::T_SELECT);
      ret = ob_write_string(*result.get_allocator(), sql, result.result_set_->get_stmt_ps_sql());
    }
    return ret;
  }
  int execute_pl_sql(const ObString &, ObSQLSessionInfo &, ParamStore &, ObResultSet &,
                    ObSqlCtx &, bool, bool) override
  {
    ++execute_calls_;
    return OB_NOT_SUPPORTED;
  }
  int64_t prepare_calls_ = 0;
  int64_t execute_calls_ = 0;
private:
  PlanAccess access_;
};

// Use the actual executable unit's context ownership. ASTs and parse trees are
// intentionally arena objects, as in ObPLBuilder::compile; the unit is destroyed
// before its context, including on ASSERT failures in a test.
class CompiledUnit
{
public:
  CompiledUnit() : context_(nullptr), function_(nullptr) {}
  ~CompiledUnit() { reset(); }
  int init()
  {
    int ret = OB_SUCCESS;
    lib::ContextParam param;
    param.set_mem_attr("PlProdUnit").set_properties(lib::USE_TL_PAGE_OPTIONAL);
    if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(context_, param))) {
    } else if (void *memory = context_->get_safe_arena_allocator().alloc(sizeof(ObPLFunction))) {
      function_ = new (memory) ObPLFunction(context_);
    } else {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    }
    return ret;
  }
  void reset()
  {
    if (function_ != nullptr) { function_->~ObPLFunction(); function_ = nullptr; }
    if (context_ != nullptr) { DESTROY_CONTEXT(context_); context_ = nullptr; }
  }
  ObPLFunction &function() { return *function_; }
private:
  lib::MemoryContext context_;
  ObPLFunction *function_;
};

ObRawExprResType record_result_type(uint64_t declaration_id)
{
  ObRawExprResType type;
  type.set_ext();
  type.set_extend_type(PL_RECORD_TYPE);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  type.set_udt_id(declaration_id);
  return type;
}

std::string text(const ObString &value)
{ return std::string(value.ptr(), value.length()); }

const ObRecordType *ast_record(const ObPLFunction &function, int64_t index)
{
  const ObPLFunctionAST *ast = function.get_ast();
  if (ast == nullptr || index < 0 || index >= ast->get_user_type_table().get_count()) { return nullptr; }
  const ObUserDefinedType *definition = ast->get_user_type_table().get_type(index);
  return definition != nullptr && definition->is_record_type()
      ? static_cast<const ObRecordType *>(definition) : nullptr;
}

class PreparedRecord
{
public:
  PreparedRecord(ObSQLSessionInfo &session, ObSchemaGetterGuard &schema)
    : owner_("PlProdCG"), info_(owner_), factory_(owner_), cg_(owner_, &session, &schema, 0, 0),
      exec_(owner_), expr_(nullptr)
  { exec_.set_my_session(&session); }
  int prepare(const ObRecordType &definition, const ObPLINS &declarations)
  {
    int ret = OB_SUCCESS;
    ObColumnRefRawExpr *column = nullptr;
    ObRawExprUniqueSet roots(false);
    if (OB_FAIL(factory_.create_raw_expr(T_REF_COLUMN, column))) {
    } else {
      column->set_result_type(record_result_type(definition.get_user_type_id()));
      if (OB_FAIL(column->add_flag(IS_COLUMN))) {
      } else if (OB_FAIL(column->add_flag(CNT_COLUMN))) {
      } else if (OB_FAIL(roots.append(column))) {
      } else {
        cg_.set_pl_declarations(&declarations);
        if (OB_FAIL(cg_.generate(roots, info_))) {
        } else if ((expr_ = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*column))) == nullptr) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(exec_.create_physical_plan_ctx())) {
        } else if (OB_FAIL(info_.pre_alloc_exec_memory(exec_))) {
        }
      }
    }
    // The compile source is not retained by the test's CG either. Runtime
    // type/operations must own their metadata before the source is destroyed.
    cg_.set_pl_declarations(nullptr);
    return ret;
  }
  int capture_null(DatumParameter &out)
  {
    if (expr_ == nullptr) { return OB_NOT_INIT; }
    ObEvalCtx eval(exec_);
    expr_->locate_expr_datum(eval).set_null();
    expr_->get_eval_info(eval).evaluated_ = true;
    return capture_datum_parameter(*expr_, eval, out);
  }
  const SqlType &type() const { return expr_->get_sql_type(); }
private:
  ObArenaAllocator owner_;
  ObExprFrameInfo info_;
  ObRawExprFactory factory_;
  ObStaticEngineExprCG cg_;
  ObExecContext exec_;
  ObExpr *expr_;
};

class NativePLDeclarationProducers : public testing::Test
{
protected:
  ObSQLSessionInfo session_;
  ObPlanCache plan_cache_; // Not initialized: no eviction thread or global host.
  ObSchemaGetterGuard schema_;
  ObPLPackageGuard package_guard_;
  ObMySQLProxy sql_proxy_;
  PrepareSqlOnly sql_;

  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session_.load_default_sys_variable(false, false));
    ASSERT_EQ(OB_SUCCESS, MockSchemaService::set_snapshot_only(schema_, SNAPSHOT));
  }
  void TearDown() override { EXPECT_EQ(0, sql_.execute_calls_); }

  int compile(CompiledUnit &owner, const char *body = ONE_CURSOR)
  {
    int ret = owner.init();
    if (ret == OB_SUCCESS) {
      ObPLFunction &function = owner.function();
      ObIAllocator &allocator = function.get_allocator();
      ObPLParser parser(allocator, session_.get_charsets4parser(), session_.get_sql_mode());
      ObStmtNodeTree *tree = nullptr;
      if (OB_FAIL(parser.parse_routine_body(ObString::make_string(body), tree, false))) {
      } else {
        ObPLBuilder builder(allocator, session_, plan_cache_, &sql_, nullptr, nullptr, nullptr,
                            schema_, package_guard_, sql_proxy_);
        ret = builder.compile(tree, 19, function, nullptr, false);
      }
    }
    return ret;
  }

  int prepare_local_type(ObPLFunction &function, const ObRecordType &record,
                         ObIAllocator &owner, const SqlType *&out)
  {
    return ObExprUtil::resolve_raw_sql_type(record_result_type(record.get_user_type_id()),
        &session_, &schema_, owner, out, &function.get_ast()->get_body()->get_namespace());
  }

  int build_schema(ObTableSchema &table, int64_t version)
  {
    int ret = OB_SUCCESS;
    table.set_table_id(TABLE_ID);
    table.set_database_id(500123);
    table.set_schema_version(version);
    table.set_table_type(USER_TABLE);
    if (OB_FAIL(table.set_table_name("producer_row"))) { return ret; }
    const char *names[] = {"key_value", "label", "amount"};
    const SqlValueType kinds[] = {ObIntType, ObVarcharType, ObDecimalIntType};
    for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
      ObColumnSchemaV2 column;
      column.set_table_id(TABLE_ID);
      column.set_column_id(16 + i);
      column.set_data_type(kinds[i]);
      column.set_nullable(true);
      column.set_collation_type(i == 1 ? CS_TYPE_UTF8MB4_UNICODE_CI : CS_TYPE_BINARY);
      if (i == 1) {
        column.set_data_length(int32_t(24));
        column.set_length_semantics(LS_BYTE);
      } else {
        column.set_data_precision(20);
        column.set_data_scale(i == 2 ? 7 : 0);
      }
      if (OB_FAIL(column.set_column_name(names[i]))) {
      } else if (OB_FAIL(table.add_column(column))) {
      }
    }
    return ret;
  }

  int prepare_table_version(int64_t version, PreparedRecord &prepared)
  {
    int ret = OB_SUCCESS;
    CompiledUnit source;
    ObTableSchema table;
    if (OB_FAIL(compile(source))) {
    } else if (OB_FAIL(build_schema(table, version))) {
    } else {
      ObPLFunction &function = source.function();
      ObPLFunctionAST &ast = *function.get_ast();
      ObPLResolveCtx context(function.get_allocator(), session_, schema_, package_guard_, sql_proxy_, false);
      context.enum_set_ctx_ = &ast.get_enum_set_ctx();
      ObRecordType *definition = nullptr;
      if (OB_FAIL(ObPLResolver::build_record_type_by_schema(context, &table, definition))) {
      } else if (definition == nullptr) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ast.get_user_type_table().add_external_type(definition))) {
      } else {
        ret = prepared.prepare(*definition, ast.get_body()->get_namespace());
      }
    }
    // Both the actual table schema and the real parser/builder's definition
    // owner are gone before the caller inspects/captures the runtime type.
    return ret;
  }

  void expect_local(const ObRecordType &record, const SqlPLCompilationScope &scope)
  {
    const SqlPLDeclarationIdentity &id = record.get_sql_declaration_identity();
    EXPECT_TRUE(record.is_local_type());
    EXPECT_TRUE(id.is_specified());
    EXPECT_TRUE(id.valid_with(record.get_sql_nominal_identity()));
    EXPECT_EQ(SqlPLDeclarationSource::Local, id.source);
    EXPECT_EQ(OB_SERVER_RUNTIME_ID, id.tenant_id);
    EXPECT_EQ(record.get_user_type_id(), id.declaration_id);
    EXPECT_TRUE(id.compilation_scope.equals(scope));
    EXPECT_FALSE(record.get_sql_nominal_identity().specified);
    // Current spi_parse_prepare does not populate cursor record fields. Do not
    // synthesize them here or claim coverage of SQL SELECT field preparation.
    EXPECT_EQ(0, record.get_record_member_count());
  }

  void expect_table(const SqlType &type, int64_t version)
  {
    EXPECT_EQ(SqlTypeForm::Record, type.form());
    EXPECT_EQ(ObExtendType, type.kind());
    EXPECT_EQ(PL_RECORD_TYPE, type.legacy_meta().get_extend_type());
    EXPECT_EQ(TABLE_ID, static_cast<uint64_t>(type.accuracy().get_accuracy()));
    EXPECT_EQ(SqlPLDeclarationSource::TableRow, type.pl_declaration().source);
    EXPECT_EQ(TABLE_ID, type.pl_declaration().declaration_id);
    EXPECT_TRUE(type.pl_declaration().valid_with(type.nominal()));
    EXPECT_FALSE(type.pl_declaration().compilation_scope.is_valid());
    EXPECT_TRUE(type.nominal().specified);
    EXPECT_EQ(OB_SERVER_RUNTIME_ID, type.nominal().tenant_id);
    EXPECT_EQ(TABLE_ID, type.nominal().object_id);
    EXPECT_EQ(version, type.nominal().schema_version);
    ASSERT_EQ(3, type.member_count());
    const char *names[] = {"key_value", "label", "amount"};
    const SqlValueType kinds[] = {ObIntType, ObVarcharType, ObDecimalIntType};
    for (int64_t i = 0; i < 3; ++i) {
      EXPECT_EQ(names[i], text(type.member(i).name));
      EXPECT_TRUE(type.member(i).nullable);
      ASSERT_NE(nullptr, type.member(i).type);
      EXPECT_EQ(kinds[i], type.member(i).type->kind());
    }
    EXPECT_EQ(24, type.member(1).type->length());
    EXPECT_EQ(CS_TYPE_UTF8MB4_UNICODE_CI, type.member(1).type->collation());
    EXPECT_EQ(20, type.member(2).type->precision());
    EXPECT_EQ(7, type.member(2).type->scale());
  }
};

TEST_F(NativePLDeclarationProducers, parser_builder_binds_one_scope_to_two_actual_local_declarations)
{
  CompiledUnit unit;
  ASSERT_EQ(OB_SUCCESS, compile(unit, TWO_CURSORS));
  ASSERT_EQ(2, sql_.prepare_calls_);
  ObPLFunction &function = unit.function();
  ASSERT_NE(nullptr, function.get_ast());
  const ObPLUserTypeTable &table = function.get_ast()->get_user_type_table();
  ASSERT_EQ(2, table.get_count());
  const SqlPLCompilationScope scope = table.get_sql_compilation_scope();
  ASSERT_TRUE(scope.is_valid());
  const ObRecordType *first = ast_record(function, 0);
  const ObRecordType *second = ast_record(function, 1);
  ASSERT_NE(nullptr, first);
  ASSERT_NE(nullptr, second);
  expect_local(*first, scope);
  expect_local(*second, scope);
  EXPECT_NE(first->get_user_type_id(), second->get_user_type_id());
  ASSERT_EQ(2, function.get_type_table().count());
  for (int64_t i = 0; i < 2; ++i) {
    const auto *compiled = static_cast<const ObRecordType *>(function.get_type_table().at(i));
    ASSERT_NE(nullptr, compiled);
    EXPECT_NE(ast_record(function, i), compiled);
    expect_local(*compiled, scope);
  }
}

TEST_F(NativePLDeclarationProducers, independent_compilations_with_reused_local_ids_have_distinct_types)
{
  CompiledUnit left;
  CompiledUnit right;
  ASSERT_EQ(OB_SUCCESS, compile(left));
  ASSERT_EQ(OB_SUCCESS, compile(right));
  const ObRecordType *a = ast_record(left.function(), 0);
  const ObRecordType *b = ast_record(right.function(), 0);
  ASSERT_NE(nullptr, a);
  ASSERT_NE(nullptr, b);
  EXPECT_EQ(a->get_user_type_id(), b->get_user_type_id());
  const auto &ia = a->get_sql_declaration_identity();
  const auto &ib = b->get_sql_declaration_identity();
  ASSERT_TRUE(ia.compilation_scope.is_valid());
  ASSERT_TRUE(ib.compilation_scope.is_valid());
  EXPECT_FALSE(ia.compilation_scope.equals(ib.compilation_scope));
  ObArenaAllocator owner("PlProdTypes");
  const SqlType *ta = nullptr;
  const SqlType *tb = nullptr;
  ASSERT_EQ(OB_SUCCESS, prepare_local_type(left.function(), *a, owner, ta));
  ASSERT_EQ(OB_SUCCESS, prepare_local_type(right.function(), *b, owner, tb));
  ASSERT_NE(nullptr, ta);
  ASSERT_NE(nullptr, tb);
  EXPECT_FALSE(ta->equals(*tb));
  EXPECT_EQ(2, sql_.prepare_calls_);
}

TEST_F(NativePLDeclarationProducers, actual_compile_type_table_and_full_type_outlive_the_source_unit)
{
  CompiledUnit source;
  CompiledUnit retained;
  ASSERT_EQ(OB_SUCCESS, compile(source));
  ASSERT_EQ(OB_SUCCESS, retained.init());
  const ObRecordType *record = ast_record(source.function(), 0);
  ASSERT_NE(nullptr, record);
  const uint64_t id = record->get_user_type_id();
  const SqlPLDeclarationIdentity identity = record->get_sql_declaration_identity();
  const std::string name = text(record->get_name());
  ObArenaAllocator type_owner("PlProdTypes");
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, prepare_local_type(source.function(), *record, type_owner, type));
  ASSERT_NE(nullptr, type);
  ASSERT_EQ(OB_SUCCESS, ObPLBuilder::compile_type_table(
      source.function().get_ast()->get_user_type_table(), retained.function()));
  ASSERT_EQ(1, retained.function().get_type_table().count());
  const auto *copy = static_cast<const ObRecordType *>(retained.function().get_type_table().at(0));
  ASSERT_NE(nullptr, copy);
  ASSERT_NE(record, copy);
  ASSERT_NE(record->get_name().ptr(), copy->get_name().ptr());
  // Poison the source's actual allocated name before destroying all parse/AST
  // storage, so an accidental declaration string borrow is observable.
  std::memset(const_cast<char *>(record->get_name().ptr()), '!', record->get_name().length());
  source.reset();
  EXPECT_EQ(id, copy->get_user_type_id());
  EXPECT_EQ(name, text(copy->get_name()));
  EXPECT_TRUE(identity.equals(copy->get_sql_declaration_identity()));
  EXPECT_TRUE(identity.equals(type->pl_declaration()));
  EXPECT_EQ(id, static_cast<uint64_t>(type->accuracy().get_accuracy()));
  EXPECT_EQ(0, type->member_count());
}

TEST_F(NativePLDeclarationProducers, actual_table_row_to_cg_keeps_fields_and_typed_null_after_source_destruction)
{
  PreparedRecord prepared(session_, schema_);
  ASSERT_EQ(OB_SUCCESS, prepare_table_version(SCHEMA_V1, prepared));
  expect_table(prepared.type(), SCHEMA_V1);
  DatumParameter captured;
  ASSERT_EQ(OB_SUCCESS, prepared.capture_null(captured));
  EXPECT_TRUE(captured.is_null());
  EXPECT_EQ(ObExtendType, captured.get_type());
  EXPECT_EQ(PL_RECORD_TYPE, captured.get_type_meta().get_extend_type());
  EXPECT_EQ(TABLE_ID, captured.get_udt_id());
  EXPECT_EQ(TABLE_ID, static_cast<uint64_t>(captured.get_accuracy().get_accuracy()));
  EXPECT_EQ(0, captured.datum_.length());
  EXPECT_EQ(Datum::State::Null, captured.datum_.state());
  EXPECT_EQ(captured.get_type_meta(), captured.get_param_meta());
  ObArenaAllocator retained("PlProdRetain");
  const SqlType *clone = nullptr;
  ASSERT_EQ(OB_SUCCESS, prepared.type().clone_to(retained, clone));
  ASSERT_NE(nullptr, clone);
  EXPECT_TRUE(clone->equals(prepared.type()));
  expect_table(*clone, SCHEMA_V1);
}

TEST_F(NativePLDeclarationProducers, table_row_recompile_preserves_old_version_and_distinguishes_new_version)
{
  PreparedRecord old_version(session_, schema_);
  PreparedRecord new_version(session_, schema_);
  ASSERT_EQ(OB_SUCCESS, prepare_table_version(SCHEMA_V1, old_version));
  ObArenaAllocator retained("PlProdRetain");
  const SqlType *old_type = nullptr;
  ASSERT_EQ(OB_SUCCESS, old_version.type().clone_to(retained, old_type));
  ASSERT_NE(nullptr, old_type);
  ASSERT_EQ(OB_SUCCESS, prepare_table_version(SCHEMA_V2, new_version));
  expect_table(*old_type, SCHEMA_V1);
  expect_table(new_version.type(), SCHEMA_V2);
  EXPECT_EQ(old_type->pl_declaration().declaration_id, new_version.type().pl_declaration().declaration_id);
  EXPECT_FALSE(old_type->equals(new_version.type()));
  EXPECT_TRUE(old_type->equals(old_version.type()));
  DatumParameter captured;
  ASSERT_EQ(OB_SUCCESS, new_version.capture_null(captured));
  EXPECT_TRUE(captured.is_null());
  EXPECT_EQ(TABLE_ID, captured.get_udt_id());
  EXPECT_EQ(2, sql_.prepare_calls_);
}
}

int main(int argc, char **argv)
{
  if (ObCharset::init_charset() != OB_SUCCESS) { return 2; }
  if (oceanbase::share::ObSysVariables::init_default_values() != OB_SUCCESS) { return 2; }
  ObExprOperatorFactory::register_expr_operators();
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
