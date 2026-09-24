// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// Real SQL expression codegen/evaluation and GIS DSO; no server or storage.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <map>
#include "lib/charset/ob_charset.h"
#include "share/rc/ob_module_provider.h"
#include "share/ob_i_lob_read_service.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_sql_init.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/engine/expr/ob_plugin_expr_utils.h"
#include "sql/engine/expr/plugin_function_expr.h"

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)
#include "native_activation_fixture.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::share::plugin;

// Real in-row decoding, but no storage backend. Out-of-row requests propagate
// an injected error instead of being mistaken for raw geometry bytes.
class LobService final : public ObILobReadService {
public:
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
  { return OB_TIMEOUT; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, ObObjType, ObCollationType,
      ObLobLocatorV2 &, ObIAllocator *, ObString &) override { return OB_TIMEOUT; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObIAllocator *, uint32_t) override { return OB_TIMEOUT; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
      ObString &, ObTextStringIterState &) override { return OB_TIMEOUT; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObString &, ObTextStringIterState &) override { return OB_TIMEOUT; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
  { return OB_TIMEOUT; }
  void free_lob_query_iter(ObLobTextIterCtx &) override {}
};

class GisProvider final : public ObIModuleProvider {
public:
  explicit GisProvider(ObPluginLoader &loader) : loader_(loader), saved_(g_mp) { g_mp = this; }
  ~GisProvider() { g_mp = saved_; }
  int calls_ = 0;
  int legacy_calls_ = 0;
  int batch_calls_ = 0;
  std::map<std::string, int> function_calls_;
  ObSQLSessionInfo *native_session_ = nullptr;
  uint64_t native_user_ = 0;
  int native_calls_ = 0;
  int native_failure_ = OB_SUCCESS;
  bool change_native_epoch_on_expansion_ = false;
  int resolve_plugin_native_function(const char *module, const char *implementation,
      const char *const *types, uint32_t count, seekdb_plugin_sql_binding_v1_t *binding) override
  {
    const int ret = loader_.resolve_native_function(module, implementation, types, count, *binding);
    if (ret == OB_SUCCESS && change_native_epoch_on_expansion_ && count > 1) ++binding->catalog_epoch;
    return ret;
  }
  void check_native_context() {
    if (native_session_) {
      CHECK(native_session_->get_database_id() == 100);
      CHECK(native_session_->get_priv_user_id() == native_user_);
      CHECK(native_session_->get_db_priv_set() == 0);
      ++native_calls_;
    }
  }
  int execute_plugin_function(const char *name, uint32_t major, uint32_t minor,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  { ++legacy_calls_; ++calls_; return loader_.execute_function(name, major, minor, ctx, args, count); }
  int execute_plugin_extension(seekdb_plugin_extension_kind_t kind, const char *name,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  { ++legacy_calls_; ++calls_; return loader_.execute_extension(kind, name, ctx, args, count); }
  int resolve_plugin_sql_object(seekdb_plugin_extension_kind_t kind, const char *name,
      const char *const *types, uint32_t count, seekdb_plugin_sql_binding_v1_t *binding) override
  {
    const int ret = loader_.resolve_sql_extension(kind, name, types, count, *binding);
    if (ret != OB_SUCCESS && ret != OB_ENTRY_NOT_EXIST) {
      std::cerr << "resolve " << name << " failed: " << ret;
      for (uint32_t i = 0; i < count; ++i) std::cerr << " " << (types[i] ? types[i] : "NULL");
      std::cerr << std::endl;
    }
    return ret;
  }
  int execute_bound_plugin_function(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  { ++calls_; ++function_calls_[binding->object_id]; check_native_context(); return native_failure_ == OB_SUCCESS ?
      loader_.execute_bound_function(*binding, ctx, args, count) : native_failure_; }
  int execute_bound_plugin_function_batch(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_batch_context_v1_t *ctx,
      const seekdb_plugin_batch_row_v1_t *rows, uint32_t count) override
  { ++batch_calls_; check_native_context(); return native_failure_ == OB_SUCCESS ?
      loader_.execute_bound_function_batch(*binding, ctx, rows, count) : native_failure_; }
  int describe_plugin_sql_column(const seekdb_plugin_sql_binding_v1_t *, uint32_t,
      seekdb_plugin_sql_column_v1_t *) override { return OB_NOT_SUPPORTED; }
  int decode_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t *,
      const seekdb_plugin_execution_context_v1 *, const uint8_t *, uint64_t) override { return OB_NOT_SUPPORTED; }
  int encode_bound_plugin_type(const seekdb_plugin_sql_binding_v1_t *,
      const seekdb_plugin_execution_context_v1 *, const seekdb_plugin_execution_value_v1 *) override
  { return OB_NOT_SUPPORTED; }
  int resolve_plugin_common_type(const char *const *, uint32_t, std::string &, uint64_t &) override
  { return OB_NOT_SUPPORTED; }
  int resolve_plugin_cast(const char *, const char *, seekdb_plugin_cast_context_t,
      seekdb_plugin_sql_cast_binding_v1_t *, uint64_t) override { return OB_NOT_SUPPORTED; }
  int execute_bound_plugin_cast(const seekdb_plugin_sql_cast_binding_v1_t *,
      const seekdb_plugin_execution_context_v1 *, const seekdb_plugin_execution_value_v1 *) override
  { return OB_NOT_SUPPORTED; }
  int open_bound_plugin_table_function(const seekdb_plugin_sql_binding_v1_t *,
      const seekdb_plugin_table_execution_context_v1_t *, const seekdb_plugin_execution_value_v1_t *,
      uint32_t, std::unique_ptr<IPluginTableCursor> &) override { return OB_NOT_SUPPORTED; }
  int mutate_plugin_type_dependency(ObISQLClient &, const seekdb_plugin_sql_binding_v1_t &,
      uint64_t, uint64_t, bool) override { return OB_NOT_SUPPORTED; }
private:
  ObPluginLoader &loader_;
  ObIModuleProvider *saved_;
};

#include "native_routine_call_fixture.h"

// Real SQL resolution over database-owned declarations. Only storage and
// authentication are controlled; there is no registry-name fallback here.
class GisCatalogContext {
public:
  ObArenaAllocator arena;
  std::unique_ptr<ObSQLSessionInfo> session = std::make_unique<ObSQLSessionInfo>();
  std::unique_ptr<schema::ObSchemaMgr> manager = std::make_unique<schema::ObSchemaMgr>();
  std::unique_ptr<schema::MockSchemaService> service = std::make_unique<schema::MockSchemaService>();
  schema::ObDatabaseSchema installed, empty;
  schema::ObUserInfo owner;
  schema::ObSchemaGetterGuard guard;
  std::shared_ptr<schema::RoutineSchemaOverlay> overlay = std::make_shared<schema::RoutineSchemaOverlay>();
  ObRawExprFactory factory{arena};
  ObStmtFactory statements{arena};
  ObMySQLProxy proxy;
  std::unique_ptr<ObPlanCache> cache = std::make_unique<ObPlanCache>();
  std::unique_ptr<oceanbase::pl::ObPL> engine = std::make_unique<oceanbase::pl::ObPL>();
  ObSql runtime;
  ObSchemaChecker checker;
  ObResolverParams params;
  ObSqlCtx sql;
  LobService lob;
  ObExecContext execution{arena};

  explicit GisCatalogContext(const std::string &package_root)
  {
    using namespace oceanbase::share::schema;
    CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
    CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    CHECK(manager->init() == OB_SUCCESS);
    CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
    ObSimpleServerRuntimeSchema runtime_schema;
    runtime_schema.set_schema_version(42); runtime_schema.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
    runtime_schema.set_status(SERVER_RUNTIME_STATUS_NORMAL);
    CHECK(runtime_schema.set_runtime_name(ObString::make_string("gis_fixture")) == OB_SUCCESS);
    CHECK(manager->add_runtime_schema(runtime_schema) == OB_SUCCESS);
    CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
    installed.set_database_id(100); empty.set_database_id(101);
    CHECK(installed.set_database_name("gis_db") == OB_SUCCESS);
    CHECK(empty.set_database_name("empty_db") == OB_SUCCESS);
    for (auto *db : {&installed, &empty}) {
      db->set_schema_version(42);
      CHECK(MockSchemaService::cache_database(guard, *db) == OB_SUCCESS);
      ObSimpleDatabaseSchema simple;
      simple.set_database_id(db->get_database_id()); simple.set_schema_version(42);
      CHECK(simple.set_database_name(db->get_database_name_str()) == OB_SUCCESS);
      CHECK(manager->add_database(simple) == OB_SUCCESS);
    }
    owner.set_user_id(123); owner.set_schema_version(42);
    CHECK(owner.set_user_name("owner") == OB_SUCCESS); CHECK(owner.set_host("localhost") == OB_SUCCESS);
    ObSimpleUserSchema simple;
    simple.set_user_id(123); simple.set_schema_version(42);
    CHECK(simple.set_user_name("owner") == OB_SUCCESS); CHECK(simple.set_host("localhost") == OB_SUCCESS);
    CHECK(manager->add_user(simple) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, owner) == OB_SUCCESS);
    CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
    CHECK(checker.init(guard) == OB_SUCCESS);
    params.allocator_ = &arena; params.session_info_ = session.get(); params.schema_checker_ = &checker;
    params.expr_factory_ = &factory; params.stmt_factory_ = &statements;
    params.query_ctx_ = statements.get_query_ctx(); params.sql_proxy_ = &proxy;
    params.plan_cache_ = cache.get(); params.pl_engine_ = engine.get(); params.pl_sql_runtime_ = &runtime;
    sql.session_info_ = session.get(); sql.schema_guard_ = &guard;
    execution.set_my_session(session.get()); execution.set_sql_ctx(&sql); execution.set_lob_read_service(&lob);
    execution.set_sql_proxy(&proxy); execution.set_plan_cache(cache.get());
    execution.set_pl_engine(engine.get()); execution.set_pl_sql_runtime(&runtime);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    use_database(true);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
    if (!package_root.empty()) stage_gis_catalog(package_root, params, guard, *manager, *overlay, 123);
    // Evaluation uses the fixture's object EXECUTE grants, not cached SUPER.
    session->set_user_priv_set(0);
  }

  void use_database(bool has_gis)
  {
    CHECK(session->set_default_database(ObString::make_string(has_gis ? "gis_db" : "empty_db")) == OB_SUCCESS);
    session->set_database_id(has_gis ? 100 : 101);
  }

  int resolve(const char *expression, ObRawExpr *&raw)
  {
    const std::string text = std::string("SELECT ") + expression;
    ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(params);
    const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
    raw = nullptr;
    if (status == OB_SUCCESS) {
      CHECK(resolver.get_select_stmt() && resolver.get_select_stmt()->get_select_item_size() == 1);
      raw = resolver.get_select_stmt()->get_select_item(0).expr_;
    }
    return status;
  }
};

static int plugin_nodes(const ObRawExpr &raw)
{
  const auto *udf = dynamic_cast<const ObUDFRawExpr *>(&raw);
  int count = udf && udf->get_udf_id() >= 340000 && udf->get_udf_id() < 340106 ? 1 : 0;
  CHECK(raw.get_expr_type() != T_FUN_SYS_PLUGIN_FUNCTION);
  CHECK(raw.get_expr_type() != T_FUN_SYS_ST_AREA);
  for (int64_t i = 0; i < raw.get_param_count(); ++i) count += plugin_nodes(*raw.get_param_expr(i));
  return count;
}

static void expression(GisProvider &provider, GisCatalogContext &catalog, const char *sql, bool invokes_plugin = true,
                       bool expect_error = false)
{
  std::cout << "CHECK: " << sql << std::endl;
  auto &arena = catalog.arena;
  auto &session = catalog.session;
  ObExecContext execution(arena);
  LobService lob_service;
  execution.set_lob_read_service(&lob_service);
  execution.set_my_session(session.get());
  execution.set_sql_ctx(&catalog.sql); execution.set_sql_proxy(&catalog.proxy);
  execution.set_runtime_services(catalog.execution.get_runtime_services());
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(*session, &execution);
  ObRawExpr *raw = nullptr;
  int status = catalog.resolve(sql, raw);
  if (status == OB_SUCCESS) {
    CHECK(raw);
    status = raw->formalize(session.get());
  }
  if (status != OB_SUCCESS) {
    std::cerr << "resolution status " << status << ": " << sql << std::endl;
    CHECK(expect_error);
    std::cout << "PASS: rejected during resolution (" << status << "): " << sql << std::endl;
    return;
  }
  CHECK(plugin_nodes(*raw) > 0);
  ObRawExprUniqueSet roots(false);
  CHECK(roots.append(raw) == OB_SUCCESS);
  ObStaticEngineExprCG generator(arena, session.get(), &catalog.guard, 0, 0);
  ObExprFrameInfo frame(arena);
  CHECK(generator.generate(roots, frame) == OB_SUCCESS);
  CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
  CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObExpr *root = nullptr;
  ObSEArray<ObRawExpr *, 1> outputs;
  CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS);
  ObEvalCtx eval(execution);
  ObDatum *value = nullptr;
  const int before = provider.calls_;
  status = root->eval(eval, value);
  if (status != OB_SUCCESS && !expect_error) std::cerr << "GIS expression error " << status << ": " << sql << std::endl;
  if (expect_error) CHECK(status != OB_SUCCESS);
  else {
    CHECK(status == OB_SUCCESS && value && !value->is_null() && value->get_int() == 1);
    if (invokes_plugin) CHECK(provider.calls_ > before);
    else CHECK(provider.calls_ == before);
  }
  CHECK(provider.legacy_calls_ == 0);
  std::cout << "PASS: " << sql << std::endl;
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(*session, nullptr);
}

static void batch_area(GisProvider &provider, GisCatalogContext &catalog)
{
  constexpr int MAX = 1031;
  auto &arena = catalog.arena;
  auto &session = catalog.session;
  ObExecContext execution(arena);
  execution.set_my_session(session.get()); execution.set_sql_ctx(&catalog.sql);
  execution.set_sql_proxy(&catalog.proxy);
  execution.set_runtime_services(catalog.execution.get_runtime_services());
  LobService lob_service; execution.set_lob_read_service(&lob_service);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
  ObRawExpr *raw = nullptr;
  CHECK(catalog.resolve("ST_Area(NULL)", raw) == OB_SUCCESS);
  auto *udf = dynamic_cast<ObUDFRawExpr *>(raw); CHECK(udf);
  ObColumnRefRawExpr *column = nullptr;
  CHECK(catalog.factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
  oceanbase::share::schema::ObColumnSchemaV2 schema;
  schema.set_table_id(123); schema.set_column_id(456); schema.set_data_type(ObGeometryType);
  schema.set_collation_type(CS_TYPE_BINARY);
  CHECK(schema.set_column_name("payload") == OB_SUCCESS);
  CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
  column->set_ref_id(123, 456);
  CHECK(udf->replace_param_expr(0, column) == OB_SUCCESS);
  CHECK(raw->formalize(session.get()) == OB_SUCCESS && plugin_nodes(*raw) == 1);
  ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
  ObStaticEngineExprCG generator(arena, session.get(), &catalog.guard, 0, 0);
  generator.set_batch_size(MAX);
  ObExprFrameInfo frame(arena);
  CHECK(generator.generate(roots, frame) == OB_SUCCESS);
  CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
  CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObExpr *root = nullptr, *input = nullptr;
  ObSEArray<ObRawExpr *, 1> outputs;
  CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS);
  CHECK(root->is_batch_result() && root->eval_batch_func_ == ObExprUDF::eval_native_batch);
  for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) input = &expr;
  CHECK(input && input->is_batch_result());
  ObEvalCtx eval(execution);
  auto *skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(MAX))); CHECK(skip);
  std::vector<std::vector<char>> wire(MAX);
  for (int size : {6, MAX}) {
    for (auto &expr : frame.rt_exprs_) {
      expr.get_eval_info(eval).evaluated_ = false;
      if (expr.is_batch_result()) expr.get_evaluated_flags(eval).reset(MAX);
    }
    for (int i = 0; i < size; ++i) {
      // An in-row geometry column, not a constructor's plugin-tagged result.
      wire[i].assign(sizeof(ObLobCommon) + 98, 0);
      auto *data = (new (wire[i].data()) ObLobCommon())->buffer_;
      data[4] = 1; data[5] = 1; data[6] = 3; data[10] = 1; data[14] = 5;
      const double width = i + 1;
      const double points[] = {0, 0, width, 0, width, 2, 0, 2, 0, 0};
      std::memcpy(data + 18, points, sizeof(points));
      auto &value = input->locate_batch_datums(eval)[i];
      if (i == 2) value.set_null();
      else value.set_string(ObString(wire[i].size(), wire[i].data()));
      input->get_evaluated_flags(eval).set(i);
    }
    input->get_eval_info(eval).evaluated_ = true;
    input->get_eval_info(eval).projected_ = true;
    input->get_eval_info(eval).cnt_ = size;
    skip->reset(MAX); skip->set(1);
    const int calls = provider.batch_calls_;
    CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS);
    CHECK(provider.batch_calls_ > calls && provider.legacy_calls_ == 0);
    for (int i = 0; i < size; ++i) if (!skip->at(i)) {
      const auto &value = root->locate_batch_datums(eval)[i];
      if (i == 2) CHECK(value.is_null());
      else CHECK(!value.is_null() && value.get_double() == 2 * (i + 1));
    }
    const int cached = provider.batch_calls_;
    CHECK(root->eval_batch(eval, *skip, size) == OB_SUCCESS && provider.batch_calls_ == cached);
  }
  std::cout << "PASS: catalog-bound ST_Area batch, geometry column LOBs, casts, skips, NULLs and cached rows" << std::endl;
}

static void payload_materialization()
{
  ObArenaAllocator arena;
  ObExecContext execution(arena);
  LobService lob_service;
  execution.set_lob_read_service(&lob_service);
  ObEvalCtx eval(execution);
  const std::string payload("data\0with-bytes", 15);
  for (ObObjType type : {ObGeometryType, ObLongTextType, ObVarcharType}) {
    for (bool header : {false, true}) {
      if (header && type == ObVarcharType) continue;
      ObExpr argument;
      argument.datum_meta_.type_ = type;
      argument.datum_meta_.cs_type_ = CS_TYPE_BINARY;
      argument.obj_meta_.set_type(type);
      argument.obj_meta_.set_collation_type(CS_TYPE_BINARY);
      if (header) argument.obj_meta_.set_has_lob_header();
      else CHECK(!argument.obj_meta_.has_lob_header());
      std::vector<char> wire((header ? sizeof(ObLobCommon) : 0) + payload.size());
      char *data = wire.data();
      if (header) data = (new (wire.data()) ObLobCommon())->buffer_;
      std::memcpy(data, payload.data(), payload.size());
      ObDatum datum;
      datum.set_string(ObString(wire.size(), wire.data()));
      ObString bytes;
      CHECK(read_plugin_expr_bytes(argument, eval, datum, arena, bytes) == OB_SUCCESS);
      CHECK(bytes.length() == payload.size() && std::memcmp(bytes.ptr(), payload.data(), payload.size()) == 0);
      std::fill(wire.begin(), wire.end(), 'x');
      CHECK(std::memcmp(bytes.ptr(), payload.data(), payload.size()) == 0);
    }
  }
  std::cout << "PASS: raw/in-row geometry and text payloads, embedded NUL and copied ownership" << std::endl;
}

int main(int argc, char **argv)
{
  CHECK(argc == 3);
  OB_LOGGER.set_file_name("gis_sql.log", true);
  OB_LOGGER.set_enable_async_log(false);
  OB_LOGGER.set_log_level("WARN");
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  CHECK(init_sql_factories() == OB_SUCCESS);
  CHECK(ObSysVariables::init_default_values() == OB_SUCCESS);
  CHECK(ObBasicSessionInfo::init_sys_vars_cache_base_values() == OB_SUCCESS);
  for (const char *name : {"point", "st_area", "st_distance", "st_length", "st_x", "st_y",
                           "st_geomfromtext", "st_geomfromwkb", "geometrycollection", "area"}) {
    CHECK(ObExprOperatorFactory::get_type_by_name(ObString::make_string(name)) == T_INVALID);
  }
  native_activation_test::Observation observation;
  observation.gis = true;
  auto guard = std::make_shared<native_activation_test::TestGuard>(observation);
  ObPluginLoader loader;
  const std::string path(argv[1]);
  const auto slash = path.rfind('/'); CHECK(slash != std::string::npos);
  CHECK(loader.init(path.substr(0, slash),
      std::make_shared<native_activation_test::TestVerifier>(false, true, false),
      guard, guard, observation.registry) == OB_SUCCESS);
  {
    GisProvider missing(loader);
    GisCatalogContext empty("");
    expression(missing, empty, "ST_Area(NULL)", false, true);
  }
  CHECK(loader.load(path.substr(slash + 1)) == OB_SUCCESS);
  {
    GisProvider provider(loader);
    for (bool definer : {false, true}) for (bool batch : {false, true})
      native_routine_call_test::run(provider, definer, batch, argv[2]);
    GisCatalogContext catalog(argv[2]);
    catalog.use_database(false);
    for (const char *sql : {"ST_Area(NULL)", "POINT(1,2)", "GEOMETRYCOLLECTION(NULL)", "AREA(NULL)"})
      expression(provider, catalog, sql, false, true);
    expression(provider, catalog, "gis_db.ST_X(gis_db.POINT(3,4)) = 3");
    catalog.use_database(true);
    payload_materialization();
    for (const char *sql : {
        "ST_X(POINT(1,2)) = 1 AND ST_Y(POINT(1,2)) = 2",
        "ST_Distance(POINT(0,0),POINT(3,4)) = 5",
        "ST_Distance_Sphere(POINT(0,0),POINT(0,0)) = 0",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(2 49)',4326),3857))-6274861.394006576) < 0.000001",
        "ABS(ST_X(ST_Transform(ST_GeomFromText('POINT(2 49)',4326),3857))-222638.98158654713) < 0.000001",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(222638.98158654713 6274861.394006576)',3857),4326))-49) < 0.000000001",
        "ABS(ST_Y(_ST_Transform(ST_GeomFromText('POINT(120 -45)',4326),3857))+5621521.486192066) < 0.000001",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(0 89)',4326),3857))-30240971.95838615) < 0.000001",
        "ABS(ST_Area(ST_Transform(ST_GeomFromText('POLYGON((0 0,2 0,2 49,0 49,0 0))',4326),3857))"
          "/(222638.98158654713*6274861.394006576)-1) < 0.000000000001",
        "ABS(ST_Length(ST_Transform(ST_GeomFromText('GEOMETRYCOLLECTION(LINESTRING(0 0,0 49),LINESTRING(2 0,2 49))',4326),3857))"
          "/(2*6274861.394006576)-1) < 0.000000000001",
        "ST_SRID(ST_Transform(ST_GeomFromText('POINT(2 49)',4326),3857)) = 3857",
        "ST_Area(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) = 12",
        "ST_Length(ST_GeomFromText('LINESTRING(0 0,3 4)')) = 5",
        "ST_X(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)))) = 3",
        "ST_Y(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)))) = 4",
        "ST_AsText(POINT(3,4)) = 'POINT(3 4)'",
        "ST_SRID(POINT(3,4)) = 0",
        "ST_SRID(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)),4326)) = 4326",
        "ST_IsValid(POINT(3,4)) = 1",
        "_ST_GeometryType(POINT(3,4)) = 'POINT'",
        "ST_Contains(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'),POINT(2,1)) = 1",
        "ST_X(ST_Centroid(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4)))) = 2",
        "ST_AsText(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4))) = 'GEOMETRYCOLLECTION (POINT(1 2), POINT(3 4))'",
        "ST_AsText(ST_GeomFromText(ST_AsText(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4))))) = 'GEOMETRYCOLLECTION (POINT(1 2), POINT(3 4))'",
        "ST_X(ST_Centroid(POINT(3,4))) = 3",
        "ST_X(POINT(1.25,2.5)) = 1.25",
        "ST_Y(POINT('1.25','2.5')) = 2.5",
        "ST_X(POINT(ST_X(POINT(3,4)),4)) = 3",
        "ST_SRID(ST_GeomFromText('POINT(3 4)','4326')) = 4326",
        "ST_SRID(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)),ST_SRID(POINT(3,4)))) = 0",
        "AREA(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) = 12",
        "ST_X(CENTROID(POINT(3,4))) = 3",
        "ST_Length(LINESTRING(POINT(0,0),POINT(3,4))) = 5",
        "ST_X(_ST_MakePoint(3,4)) = 3",
        "LENGTH(ST_AsText(ST_GeomFromText(CONCAT('LINESTRING(',REPEAT('0 0,',20000),'1 1)')))) > 65535",
    }) expression(provider, catalog, sql);
    expression(provider, catalog, "CHARSET(ST_AsWKB(POINT(3,4))) = 'binary'");
    expression(provider, catalog, "COLLATION(ST_AsText(POINT(3,4))) = 'utf8mb4_general_ci'");
    const int distance_calls = provider.function_calls_["org.seekdb.gis.function.st_distance"];
    const int point_calls = provider.function_calls_["org.seekdb.gis.function.st_point"];
    // Native UDFs evaluate their arguments before the strict NULL check. The
    // nested POINT may run, but the distance implementation must not run.
    expression(provider, catalog, "ST_Distance(NULL,POINT(0,0)) IS NULL");
    CHECK(provider.function_calls_["org.seekdb.gis.function.st_distance"] == distance_calls);
    CHECK(provider.function_calls_["org.seekdb.gis.function.st_point"] == point_calls + 1);
    expression(provider, catalog, "ST_Area(NULL) IS NULL", false);
    expression(provider, catalog, "POINT(NULL,2) IS NULL", false);
    expression(provider, catalog, "ST_GeomFromText(NULL) IS NULL", false);
    expression(provider, catalog, "ST_Transform(NULL,3857) IS NULL", false);
    expression(provider, catalog, "ST_GeomFromText('POINT(3 4)',NULL) IS NULL", false);
    for (const char *sql : {"ST_Area()", "ST_Area(POINT(1,2),POINT(3,4))",
         "ST_Area('not geometry')", "POINT('not a number',2)",
         "ST_GeomFromText('POINT(3 4)',-1)", "ST_GeomFromText('POINT(3 4)',4294967296)",
         "ST_Transform(ST_GeomFromText('POINT(2 90)',4326),3857)",
         "ST_Transform(ST_GeomFromText('LINESTRING(2 49,2 -90)',4326),3857)",
         "ST_Transform(ST_GeomFromText('POLYGON((0 0,2 0,2 90,0 90,0 0))',4326),3857)",
         "ST_Transform(ST_GeomFromText('GEOMETRYCOLLECTION(POINT(2 49),POINT(2 90))',4326),3857)",
         "ST_Transform(POINT(2,49),3857)",
         "ST_Transform(ST_GeomFromText('POINT(2 49)',4326),32631)"}) {
      expression(provider, catalog, sql, false, true);
    }
    batch_area(provider, catalog);
  }
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.gis", status) == OB_SUCCESS && status.lease_count_ == 0);
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  std::cout << "PASS: GIS SQL/LOB expressions through real plugin; no live-server/storage claims" << std::endl;
}
