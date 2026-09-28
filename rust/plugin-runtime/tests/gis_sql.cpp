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
#include "share/rc/ob_server_runtime.h"
#include "share/ob_i_lob_read_service.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_sql_init.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/engine/expr/ob_plugin_expr_utils.h"
#include "sql/engine/expr/plugin_function_expr.h"
#include "sql/engine/expr/ob_expr_spatial_cellid.h"
#include "sql/engine/expr/ob_expr_spatial_mbr.h"
#include "share/geo/ob_s2adapter.h"
#include "share/geo/ob_srs_info.h"
#include "share/geo/ob_srs_wkt_parser.h"
#include "share/geo/ob_geo_utils.h"
#include "observer/omt/ob_srs_service.h"
#include "seekdb/plugin/spatial_index_spi.h"
#include "seekdb/geo/axis_order.hpp"
#include "sql/engine/expr/ob_geo_expr_utils.h"
#include "sql/das/ob_das_domain_utils.h"
#include "sql/das/iter/ob_das_spatial_scan_iter.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/rewrite/ob_range_generator.h"
#include "data_plane/access/ob_tablet_scan.h"
#include "data_plane/access/ob_table_scan_param.h"

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)
#include "gis_catalog_fixture.h"
#include "native_activation_fixture.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::share::plugin;
#include "legacy_axis_order_probe.h"
#include "gis_spatial_fixture.h"

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
  int spatial_calls_ = 0;
  int spatial_fault_ = 0;
  int cover_calls_ = 0;
  int cell_calls_ = 0;
  int index_fault_ = 0;
  int buffer_calls_ = 0;
  int buffer_fault_ = 0;
  int srs_calls_ = 0;
  int srs_fault_ = 0;
  std::map<std::string, int> function_calls_;
  ObSQLSessionInfo *native_session_ = nullptr;
  uint64_t native_user_ = 0;
  int native_calls_ = 0;
  int native_failure_ = OB_SUCCESS;
  bool change_native_epoch_on_expansion_ = false;
  uint64_t native_support_xor_ = 0;
  int resolve_plugin_native_function(const char *module, const char *implementation,
      const char *const *types, uint32_t count, seekdb_plugin_sql_binding_v1_t *binding) override
  {
    const int ret = loader_.resolve_native_function(module, implementation, types, count, *binding);
    if (ret == OB_SUCCESS && change_native_epoch_on_expansion_ && count > 1) ++binding->catalog_epoch;
    if (ret == OB_SUCCESS && std::strcmp(implementation, "org.seekdb.gis.function.st_intersects") == 0)
      binding->flags ^= native_support_xor_;
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
  {
    if (std::strcmp(name, SEEKDB_PLUGIN_SPATIAL_BUFFER_SERVICE) == 0) {
      ++buffer_calls_;
      if (buffer_fault_ == 1) return OB_TIMEOUT;
      if (buffer_fault_ == 2) return OB_SUCCESS;
      struct Proxy {
        const seekdb_plugin_execution_context_v1 *original;
        int fault;
        static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
            const seekdb_plugin_execution_result_v1_t *input) {
          const auto &p = *reinterpret_cast<Proxy *>(host);
          auto result = *input;
          std::vector<uint8_t> bytes(input->data, input->data + input->data_size);
          result.data = bytes.data();
          if (p.fault == 5) result.type_id = "core.type.bytes";
          if (p.fault == 6) result.data_size = 1;
          if (p.fault == 7) result.reserved[0] = 1;
          if (p.fault == 8) result.is_null = 1;
          if (p.fault == 9) bytes[0] ^= 1;
          (void)p.original->emit_result(p.original->host, &result);
          if (p.fault == 4) (void)p.original->emit_result(p.original->host, &result);
          return SEEKDB_PLUGIN_STATUS_OK;
        }
      } proxy{ctx, buffer_fault_};
      auto context = *ctx;
      context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&proxy);
      context.emit_result = Proxy::emit;
      const int ret = loader_.execute_function(name, major, minor, buffer_fault_ ? &context : ctx, args, count);
      return buffer_fault_ == 3 ? OB_TIMEOUT : ret;
    }
    if (std::strcmp(name, SEEKDB_PLUGIN_SRS_DESCRIBE_SERVICE) == 0) {
      ++srs_calls_;
      if (srs_fault_ == 1) return OB_TIMEOUT;
      if (srs_fault_ == 2) return OB_SUCCESS;
      struct Proxy {
        const seekdb_plugin_execution_context_v1 *original;
        int fault;
        static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
            const seekdb_plugin_execution_result_v1_t *input) {
          const auto &p = *reinterpret_cast<Proxy *>(host);
          auto result = *input;
          uint8_t bytes[sizeof(seekdb_plugin_srs_metadata_v1_t) +
              SEEKDB_PLUGIN_SRS_MAX_PARAMETERS * sizeof(seekdb_plugin_srs_parameter_v1_t)];
          CHECK(input->data_size <= sizeof(bytes));
          std::memcpy(bytes, input->data, input->data_size);
          result.data = bytes;
          seekdb_plugin_srs_metadata_v1_t m;
          std::memcpy(&m, bytes, sizeof(m));
          if (p.fault == 5) result.type_id = "core.type.bytes";
          if (p.fault == 6) --result.data_size;
          if (p.fault == 7) result.reserved[0] = 1;
          if (p.fault == 8) m.semi_major = NAN;
          if (p.fault == 9) m.parameter_count = SEEKDB_PLUGIN_SRS_MAX_PARAMETERS + 1;
          if (p.fault == 10) ++m.srid;
          if (p.fault == 11) m.axis0 = 6;
          if (p.fault == 12) m.angular_unit = 0; // Contradicts WGS84 classification.
          if (p.fault == 13) m.reserved[0] = 1;
          if (p.fault == 14) m.flags |= SEEKDB_PLUGIN_SRS_HAS_TOWGS84;
          std::memcpy(bytes, &m, sizeof(m));
          (void)p.original->emit_result(p.original->host, &result);
          if (p.fault == 4) (void)p.original->emit_result(p.original->host, &result);
          return SEEKDB_PLUGIN_STATUS_OK;
        }
      } proxy{ctx, srs_fault_};
      auto context = *ctx;
      context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&proxy);
      context.emit_result = Proxy::emit;
      const int ret = loader_.execute_function(name, major, minor, srs_fault_ ? &context : ctx, args, count);
      return srs_fault_ == 3 ? OB_TIMEOUT : ret;
    }
    if (std::strcmp(name, SEEKDB_PLUGIN_SPATIAL_COVER_SERVICE) == 0 ||
        std::strcmp(name, SEEKDB_PLUGIN_SPATIAL_CELLS_SERVICE) == 0) {
      const bool cover = std::strcmp(name, SEEKDB_PLUGIN_SPATIAL_COVER_SERVICE) == 0;
      if (cover) ++cover_calls_; else ++cell_calls_;
      if (index_fault_ == 1) return OB_TIMEOUT;
      if (index_fault_ == 2) return OB_SUCCESS;
      struct Proxy {
        const seekdb_plugin_execution_context_v1 *original;
        int fault;
        bool cover;
        static seekdb_plugin_status_t SEEKDB_PLUGIN_CALL emit(seekdb_plugin_host_handle_t *host,
            const seekdb_plugin_execution_result_v1_t *input) {
          const auto &p = *reinterpret_cast<Proxy *>(host);
          auto result = *input;
          std::vector<uint8_t> bytes(input->data, input->data + input->data_size);
          result.data = bytes.data();
          if (p.fault == 5) result.type_id = "core.type.bytes";
          if (p.fault == 6) --result.data_size;
          if (p.fault == 7) result.reserved[0] = 1;
          if (p.fault == 8) { uint32_t invalid = UINT32_MAX; std::memcpy(bytes.data(), &invalid, sizeof(invalid)); }
          if (p.fault == 9) {
            if (p.cover) {
              seekdb_plugin_spatial_cover_result_v2_t h;
              std::memcpy(&h, bytes.data(), sizeof(h)); h.v1.xmin = NAN;
              std::memcpy(bytes.data(), &h, sizeof(h));
            } else {
              seekdb_plugin_spatial_cell_v1_t cell;
              auto *ptr = bytes.data() + sizeof(seekdb_plugin_spatial_cells_result_v1_t);
              std::memcpy(&cell, ptr, sizeof(cell)); cell.ancestor_count = 31;
              std::memcpy(ptr, &cell, sizeof(cell));
            }
          }
          (void)p.original->emit_result(p.original->host, &result);
          if (p.fault == 4) (void)p.original->emit_result(p.original->host, &result);
          return SEEKDB_PLUGIN_STATUS_OK; // Deliberately ignore callback rejection.
        }
      } proxy{ctx, index_fault_, cover};
      auto context = *ctx;
      context.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&proxy);
      context.emit_result = Proxy::emit;
      const int ret = loader_.execute_function(name, major, minor, index_fault_ ? &context : ctx, args, count);
      return index_fault_ == 3 ? OB_TIMEOUT : ret;
    }
    if (std::strcmp(name, SEEKDB_PLUGIN_SPATIAL_FILTER_SERVICE) == 0) {
      ++spatial_calls_;
      if (spatial_fault_ == 1) return OB_TIMEOUT;
      if (spatial_fault_ == 2) return OB_SUCCESS; // Malformed producer: no output.
      if (spatial_fault_ >= 3) {
        uint8_t value = spatial_fault_ == 5 ? 2 : 0;
        seekdb_plugin_execution_result_v1_t result{};
        result.struct_size = sizeof(result);
        result.type_id = spatial_fault_ == 6 ? "core.type.int64" : "core.type.bool";
        result.data = &value; result.data_size = 1;
        if (spatial_fault_ == 7) result.is_null = 1;
        if (spatial_fault_ == 8) result.reserved[0] = 1;
        (void)ctx->emit_result(ctx->host, &result);
        if (spatial_fault_ == 4) (void)ctx->emit_result(ctx->host, &result);
        return spatial_fault_ == 3 ? OB_TIMEOUT : OB_SUCCESS;
      }
      return loader_.execute_function(name, major, minor, ctx, args, count);
    }
    ++legacy_calls_; ++calls_;
    return loader_.execute_function(name, major, minor, ctx, args, count);
  }
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
#include "share/ob_timezone_mgr.h"

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

#include "gis_srs_lookup_fixture.h"
#include "gis_srs_lifecycle_fixture.h"

static int plugin_nodes(const ObRawExpr &raw)
{
  const auto *udf = dynamic_cast<const ObUDFRawExpr *>(&raw);
  int count = udf && udf->get_udf_id() >= 340000 && udf->get_udf_id() < 340112 ? 1 : 0;
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
  execution.set_srs_provider(catalog.execution.get_srs_provider());
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  execution.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
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

static void spatial_index_placeholders()
{
  ObArenaAllocator arena;
  ObExecContext execution(arena);
  ObEvalCtx eval(execution);
  ObExpr expr;
  // No argument frame/session/plugin. The internal placeholder must never
  // evaluate an operand or dispatch to a GIS implementation.
  uint64_t storage = 123;
  ObDatum result(reinterpret_cast<const char *>(&storage), sizeof(storage), false);
  result.set_uint(123);
  CHECK(ObExprSpatialCellid::eval_spatial_cellid(expr, eval, result) == OB_SUCCESS);
  CHECK(result.is_null());
  result.set_uint(123);
  CHECK(ObExprSpatialMbr::eval_spatial_mbr(expr, eval, result) == OB_SUCCESS);
  CHECK(result.is_null());
  ObExprCtx old_context;
  ObObj old_result, argument;
  ObExprSpatialCellid cell(arena);
  ObExprSpatialMbr mbr(arena);
  old_result.set_int(123);
  CHECK(cell.calc_result1(old_result, argument, old_context) == OB_SUCCESS);
  CHECK(old_result.is_null());
  old_result.set_int(123);
  CHECK(mbr.calc_result1(old_result, argument, old_context) == OB_SUCCESS);
  CHECK(old_result.is_null());
  std::cout << "PASS: internal spatial index placeholders return NULL without plugin/operand evaluation" << std::endl;
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

#include "gis_das_write_fixture.h"
#include "gis_das_scan_fixture.h"
#include "gis_backfill_fixture.h"
#include "gis_range_fixture.h"

static void spatial_cover_bridge(GisProvider &provider)
{
  ObArenaAllocator allocator;
  ObSrsBoundsItem bounds;
  bounds.minX_ = bounds.minY_ = -100; bounds.maxX_ = bounds.maxY_ = 100;
  auto point = gis_spatial_test::geometry(1, {0, 0});
  const auto wkb = [](std::vector<uint8_t> &bytes) {
    return ObString(bytes.size(), reinterpret_cast<char *>(bytes.data()));
  };
  ObS2Adapter adapter(&allocator, false);
  ObS2Cellids cells, parents, vertices, query;
  ObSpatialMBR mbr(ObDomainOpType::T_GEO_COVERS);
  CHECK(adapter.get_cellids(cells, false) == OB_NOT_INIT && cells.size() == 0);
  CHECK(adapter.get_mbr(mbr) == OB_NOT_INIT && mbr.is_empty());
  CHECK(adapter.init(wkb(point)) == OB_INVALID_ARGUMENT);
  const int before = provider.cover_calls_;
  CHECK(adapter.init(wkb(point), &bounds) == OB_SUCCESS);
  CHECK(adapter.init(wkb(point), &bounds) == OB_INIT_TWICE);
  std::fill(point.begin(), point.end(), 0); // No borrowed input retained.
  CHECK(adapter.get_cellids_and_unrepeated_ancestors(cells, parents) == OB_SUCCESS);
  CHECK(cells.size() == 1 && cells[0] == UINT64_C(0x1000000000000001) && parents.size() == 30);
  CHECK(adapter.get_inner_cover_cellids(vertices) == OB_SUCCESS && vertices.size() == 1 && vertices[0] == cells[0]);
  CHECK(adapter.get_cellids(query, true) == OB_SUCCESS && query.size() == 31 && query[0] == cells[0]);
  for (int i = 0; i < 30; ++i) CHECK(query[i + 1] == parents[i]);
  CHECK(adapter.get_mbr(mbr) == OB_SUCCESS && mbr.is_point_ && !mbr.is_geog_ &&
        mbr.x_min_ == 0 && mbr.y_min_ == 0 && mbr.mbr_type_ == ObDomainOpType::T_GEO_COVERS);
  CHECK(provider.cover_calls_ == before + 1);
  CHECK(adapter.get_cellids_and_unrepeated_ancestors(cells, cells) == OB_INVALID_ARGUMENT && cells.size() == 1);
  ObS2Cellids metadata;
  CHECK(adapter.get_ancestors(cells[0], metadata) == OB_SUCCESS && metadata.size() == parents.size());
  for (int i = 0; i < 30; ++i) CHECK(metadata[i] == parents[i]);
  uint64_t start = 7, end = 9;
  CHECK(ObS2Adapter::get_child_of_cellid(cells[0], start, end) == OB_SUCCESS && start == cells[0] && end == cells[0]);
  CHECK(ObS2Adapter::get_child_of_cellid(parents[29], start, end) == OB_SUCCESS && start == 1 && end == UINT64_C(0x1fffffffffffffff));
  CHECK(ObS2Adapter::get_child_of_cellid(UINT64_MAX, start, end) == OB_SUCCESS && start == UINT64_MAX && end == UINT64_MAX);
  CHECK(adapter.get_ancestors(UINT64_MAX, metadata) == OB_SUCCESS && metadata.size() == 30);
  for (uint64_t id : {UINT64_C(0), UINT64_C(2), UINT64_C(0xc000000000000001)}) {
    CHECK(ObS2Adapter::get_child_of_cellid(id, start, end) == OB_INVALID_ARGUMENT);
    CHECK(start == UINT64_MAX && end == UINT64_MAX);
  }
  point = gis_spatial_test::geometry(1, {0, 0});
  for (int fault = 1; fault <= 9; ++fault) {
    provider.index_fault_ = fault;
    ObS2Adapter broken(&allocator, false);
    const int expected = (fault == 1 || fault == 3) ? OB_TIMEOUT : OB_ERR_UNEXPECTED;
    CHECK(broken.init(wkb(point), &bounds) == expected);
    CHECK(broken.get_cellids(cells, false) == OB_NOT_INIT && cells.size() == 1);
    start = 7; end = 9;
    CHECK(ObS2Adapter::get_child_of_cellid(cells[0], start, end) == expected && start == 7 && end == 9);
    CHECK(adapter.get_ancestors(cells[0], metadata) == expected && metadata.size() == 30);
    provider.index_fault_ = 0;
    CHECK(broken.init(wkb(point), &bounds) == OB_SUCCESS); // Failed init is retryable.
  }
  for (bool window : {false, true}) {
    auto line = gis_spatial_test::geometry(2, {-50, -20, 30, 40});
    ObS2Adapter covering(&allocator, false, window);
    CHECK(covering.init(wkb(line), &bounds) == OB_SUCCESS);
    ObS2Cellids cover, ancestors, all;
    CHECK(covering.get_cellids_and_unrepeated_ancestors(cover, ancestors) == OB_SUCCESS);
    CHECK(covering.get_cellids(all, true) == OB_SUCCESS && all.size() == cover.size() + ancestors.size());
    CHECK(cover.size() > 0 && (!window || cover.size() > 4));
  }
  auto outside = gis_spatial_test::geometry(1, {200, 200});
  ObS2Adapter clipped(&allocator, false);
  CHECK(clipped.init(wkb(outside), &bounds) == OB_SUCCESS);
  ObS2Cellids sentinel;
  CHECK(clipped.get_cellids(sentinel, false) == OB_SUCCESS && sentinel.size() == 1 && sentinel[0] == UINT64_MAX);
  auto empty = gis_spatial_test::geometry(7, {});
  gis_spatial_test::append_u32(empty, 0);
  ObS2Adapter empty_adapter(&allocator, false);
  CHECK(empty_adapter.init(wkb(empty), &bounds) == OB_SUCCESS);
  CHECK(empty_adapter.get_mbr(mbr) == OB_SUCCESS && mbr.is_empty());
  auto geographic = gis_spatial_test::geometry(1, {179.9, 0});
  ObS2Adapter buffered(&allocator, true, 0.02);
  CHECK(buffered.init(wkb(geographic)) == OB_SUCCESS);
  CHECK(buffered.get_mbr(mbr) == OB_SUCCESS && mbr.is_geog_ && mbr.x_min_ > mbr.x_max_);
  auto *saved = g_mp; g_mp = nullptr;
  ObS2Adapter missing(&allocator, false);
  CHECK(missing.init(wkb(point), &bounds) == OB_NOT_SUPPORTED);
  CHECK(ObS2Adapter::get_child_of_cellid(cells[0], start, end) == OB_NOT_SUPPORTED);
  g_mp = saved;
  std::cout << "PASS: core-GIS-off covering bridge, owned all-view snapshot, cell metadata and failed-result isolation" << std::endl;
}

static void spatial_mbr_bridge(GisProvider &provider)
{
  using Op = ObDomainOpType;
  ObSpatialMBR row(1, 2, 1, 2, Op::T_INVALID);
  ObSpatialMBR query(0, 3, 0, 3, Op::T_INVALID);
  bool reject = true;
  const int before = provider.spatial_calls_;
  CHECK(row.filter(query, Op::T_GEO_COVERS, reject) == OB_SUCCESS && !reject);
  CHECK(row.filter(query, Op::T_GEO_COVEREDBY, reject) == OB_SUCCESS && reject);
  CHECK(row.filter(query, Op::T_GEO_INTERSECTS, reject) == OB_SUCCESS && !reject);
  CHECK(row.filter(query, Op::T_GEO_DWITHIN, reject) == OB_SUCCESS && !reject);
  CHECK(provider.spatial_calls_ == before + 4); // Actual leased DSO, not a core approximation.
  CHECK(row.filter(query, Op::T_GEO_DFULLYWITHIN, reject) == OB_NOT_SUPPORTED && reject);
  CHECK(row.filter(query, Op::T_INVALID, reject) == OB_INVALID_ARGUMENT && reject);
  CHECK(provider.spatial_calls_ == before + 4);
  query.is_geog_ = true;
  CHECK(row.filter(query, Op::T_GEO_INTERSECTS, reject) == OB_INVALID_ARGUMENT && reject);
  CHECK(provider.spatial_calls_ == before + 4);
  row = ObSpatialMBR(170, -170, -20, 20, Op::T_INVALID);
  query = ObSpatialMBR(178, -178, -10, 10, Op::T_INVALID);
  row.is_geog_ = query.is_geog_ = true;
  CHECK(row.filter(query, Op::T_GEO_INTERSECTS, reject) == OB_SUCCESS && !reject);
  CHECK(row.filter(query, Op::T_GEO_COVERS, reject) == OB_SUCCESS && reject);
  CHECK(row.filter(query, Op::T_GEO_COVEREDBY, reject) == OB_SUCCESS && !reject);
  query.x_min_ = -5; query.x_max_ = 5;
  CHECK(row.filter(query, Op::T_GEO_INTERSECTS, reject) == OB_SUCCESS && reject);

  char bytes[OB_DEFAULT_MBR_SIZE + 1]{};
  int64_t size = 0;
  CHECK(row.to_char(bytes + 1, size) == OB_SUCCESS && size == 32);
  const double original_wire[] = {-20, 20, 170, -170};
  CHECK(std::memcmp(bytes + 1, original_wire, sizeof(original_wire)) == 0);
  ObString encoded(size, bytes + 1); // Unaligned storage bytes.
  ObSpatialMBR decoded;
  CHECK(ObSpatialMBR::from_string(encoded, Op::T_GEO_INTERSECTS, decoded, false) == OB_SUCCESS);
  CHECK(decoded.x_min_ == 170 && decoded.x_max_ == -170 && decoded.y_min_ == -20 && decoded.y_max_ == 20);
  CHECK(decoded.mbr_type_ == Op::T_GEO_INTERSECTS && !decoded.is_point_ && !decoded.is_geog_);
  for (int64_t length : {int64_t(0), int64_t(1), int64_t(16), int64_t(31), int64_t(33)}) {
    ObString bad(length, bytes);
    CHECK(ObSpatialMBR::from_string(bad, Op::T_INVALID, decoded, false) == OB_INVALID_ARGUMENT);
    CHECK(decoded.x_min_ == 170); // Failed decode leaves the output unchanged.
  }
  row = ObSpatialMBR(3, 3, 4, 4, Op::T_INVALID); row.is_point_ = true;
  CHECK(row.to_char(bytes + 1, size) == OB_SUCCESS && size == 16);
  const double point_wire[] = {3, 4};
  CHECK(std::memcmp(bytes + 1, point_wire, sizeof(point_wire)) == 0);
  encoded.assign_ptr(bytes + 1, size);
  CHECK(ObSpatialMBR::from_string(encoded, Op::T_GEO_COVERS, decoded, true) == OB_SUCCESS);
  CHECK(decoded.is_point_ && decoded.x_min_ == 3 && decoded.x_max_ == 3 && decoded.y_min_ == 4 && decoded.y_max_ == 4);
  CHECK(ObSpatialMBR::from_string(encoded, Op::T_INVALID, decoded, false) == OB_INVALID_ARGUMENT);
  const double bad_wire[] = {NAN, 4};
  std::memcpy(bytes + 1, bad_wire, sizeof(bad_wire));
  CHECK(ObSpatialMBR::from_string(encoded, Op::T_INVALID, decoded, true) == OB_INVALID_ARGUMENT);
  CHECK(decoded.x_min_ == 3);
  CHECK(row.to_char(nullptr, size) == OB_INVALID_ARGUMENT && size == 0);
  CHECK(ObSpatialMBR().to_char(bytes, size) == OB_INVALID_ARGUMENT && size == 0);

  query = row;
  for (int fault = 1; fault <= 8; ++fault) {
    provider.spatial_fault_ = fault;
    reject = false;
    CHECK(row.filter(query, Op::T_GEO_INTERSECTS, reject) ==
          ((fault == 1 || fault == 3) ? OB_TIMEOUT : OB_ERR_UNEXPECTED));
    CHECK(reject); // No partial result even if the producer ignored emit's error.
  }
  provider.spatial_fault_ = 0;
  auto *saved = g_mp;
  g_mp = nullptr;
  CHECK(row.filter(query, Op::T_GEO_INTERSECTS, reject) == OB_NOT_SUPPORTED && reject);
  g_mp = saved;
  std::cout << "PASS: core-GIS-off MBR codec and leased filter bridge, wrapped longitude and malformed-result/error fences" << std::endl;
}

static void srs_bridge(GisProvider &provider, ObArenaAllocator &arena,
                       ObSpatialReferenceSystemBase *&retained)
{
  const auto text = [](const ObString &s) { return std::string(s.ptr(), s.length()); };
  std::string wkt = gis_srs_test::geographic_wkt();
  const int before = provider.srs_calls_;
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 4326, ObString::make_string(wkt.c_str()), retained) == OB_SUCCESS);
  CHECK(provider.srs_calls_ == before + 1 && retained);
  wkt.assign(wkt.size(), 'x'); // Neither input nor callback bytes may escape.
  ObSrsItem item(retained);
  CHECK(item.get_srid() == 4326 && item.is_wgs84() && item.is_geographical_srs());
  CHECK(item.is_lat_long_order() && item.is_latitude_north() && item.is_longtitude_east());
  CHECK(std::abs(item.semi_minor_axis() - 6356752.314245179) < 1e-7);
  double value = 0;
  CHECK(item.latitude_convert_to_radians(90, value) == OB_SUCCESS && std::abs(value - M_PI / 2) < 1e-14);
  CHECK(item.longtitude_convert_from_radians(M_PI, value) == OB_SUCCESS && std::abs(value - 180) < 1e-12);
  CHECK(retained->axis_direction(2) == ObAxisDirection::INIT);
  CHECK(std::isnan(item.get_bounds()->minX_));
  retained->set_bounds(-180, -90, 180, 90);
  CHECK(item.get_bounds()->minX_ == -180 && item.get_bounds()->maxY_ == 90);
  ObString proj4;
  CHECK(item.get_proj4_param(&arena, proj4) == OB_SUCCESS);
  CHECK(text(proj4).find("+proj=lonlat +a=6378137") == 0);
  CHECK(text(proj4).find("+towgs84=0,0,0,0,0,0,0") != std::string::npos);
  CHECK(item.get_proj4_param(nullptr, proj4) == OB_INVALID_ARGUMENT);

  const char *sphere = R"(GEOGCS["sphere",DATUM["sphere",SPHEROID["sphere",6371000,0],TOWGS84[0,0,0,0,0,0,0]],PRIMEM["Greenwich",0],UNIT["degree",0.017453292519943278],AXIS["Lat",NORTH],AXIS["Lon",EAST]])";
  ObSpatialReferenceSystemBase *other = nullptr;
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 70000001, ObString::make_string(sphere), other) == OB_SUCCESS);
  proj4.reset();
  CHECK(other->get_proj4_param(&arena, proj4) == OB_SUCCESS);
  const auto minor = text(proj4).find(" +b=");
  CHECK(minor != std::string::npos && std::stod(text(proj4).substr(minor + 4)) == 6371000);
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 999000, ObString::make_string(WORLD_MERCATOR_WKT), other) == OB_SUCCESS);
  CHECK(other->srs_type() == ObSrsType::PROJECTED_SRS && other->semi_major_axis() == 0 && other->linear_unit() == 1);
  std::string fallback = "+proj=merc +datum=WGS84";
  CHECK(other->set_proj4text(arena, ObString::make_string(fallback.c_str())) == OB_SUCCESS);
  fallback.assign(fallback.size(), 'x');
  proj4.reset();
  CHECK(ObSrsItem(other).get_proj4_param(&arena, proj4) == OB_SUCCESS);
  CHECK(text(proj4) == "+proj=merc +datum=WGS84");

  wkt = gis_srs_test::geographic_wkt();
  const ObString input = ObString::make_string(wkt.c_str());
  for (int fault = 1; fault <= 14; ++fault) {
    provider.srs_fault_ = fault;
    other = retained;
    CHECK(ObSrsWktParser::parse_srs_wkt(arena, 4326, input, other) ==
          ((fault == 1 || fault == 3) ? OB_TIMEOUT : OB_ERR_UNEXPECTED));
    CHECK(other == retained);
  }
  provider.srs_fault_ = 0;
  class FailingAllocator final : public ObIAllocator {
  public:
    void *alloc(int64_t) override { return nullptr; }
    void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
    void free(void *) override {}
  } failing;
  CHECK(ObSrsWktParser::parse_srs_wkt(failing, 4326, input, other) == OB_ALLOCATE_MEMORY_FAILED && other == retained);
  for (uint64_t id : {uint64_t(UINT32_MAX), uint64_t(UINT32_MAX) + 1})
    CHECK(ObSrsWktParser::parse_srs_wkt(arena, id, input, other) == OB_INVALID_ARGUMENT && other == retained);
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 4326, ObString(), other) == OB_INVALID_ARGUMENT && other == retained);
  std::string oversized(SEEKDB_PLUGIN_SRS_MAX_WKT_BYTES + 1, 'x');
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 4326, ObString::make_string(oversized.c_str()), other) == OB_INVALID_ARGUMENT);
  CHECK(other == retained);
  auto *saved = g_mp;
  g_mp = nullptr;
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 4326, input, other) == OB_NOT_SUPPORTED && other == retained);
  proj4.reset();
  CHECK(item.get_proj4_param(&arena, proj4) == OB_SUCCESS && item.is_wgs84());
  g_mp = saved;
  CHECK(ObSrsWktParser::parse_srs_wkt(arena, 4326, input, other) == OB_SUCCESS && other != retained);

  unsigned reserved = 0;
  for (uint32_t id = 999000; id <= 999283; ++id) {
    const bool valid = id <= 999062 || (id >= 999101 && id <= 999161) || (id >= 999163 && id < 999283);
    proj4 = ObString::make_string("unchanged");
    CHECK(ObGeoTypeUtil::get_pg_reserved_prj4text(&arena, id, proj4) == (valid ? OB_SUCCESS : OB_ERR_UNEXPECTED));
    if (valid) { ++reserved; CHECK(text(proj4).find("+proj=") == 0); }
    else CHECK(text(proj4) == "unchanged");
  }
  CHECK(reserved == 244);
  CHECK(ObGeoTypeUtil::get_pg_reserved_prj4text(nullptr, 999000, proj4) == OB_INVALID_ARGUMENT);
  std::cout << "PASS: host-owned SRS bridge, axis/unit/proj4, 244 reserved SRIDs, allocation and 14 producer fault fences" << std::endl;
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
  // Generated-column type deduction merges local session variables through
  // the process time-zone map, even when those variables are empty. Initialize
  // the real manager without starting its refresh timer or reading SQL tables.
  ObMySQLProxy timezone_proxy;
  CHECK(OTTZ_MGR.init(timezone_proxy) == OB_SUCCESS);
  spatial_index_placeholders();
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
    ObSpatialMBR point(0, 0, 0, 0, ObDomainOpType::T_INVALID);
    bool reject = false;
    CHECK(point.filter(point, ObDomainOpType::T_GEO_INTERSECTS, reject) == OB_ENTRY_NOT_EXIST && reject);
    GisCatalogContext empty("");
    expression(missing, empty, "ST_Area(NULL)", false, true);
  }
  CHECK(loader.load(path.substr(slash + 1)) == OB_SUCCESS);
  ObArenaAllocator srs_arena;
  ObSpatialReferenceSystemBase *retained_srs = nullptr;
  oceanbase::omt::ObSrsCacheSnapShot snapshot;
  CHECK(snapshot.init() == OB_SUCCESS);
  {
    GisProvider provider(loader);
    srs_bridge(provider, srs_arena, retained_srs);
    CHECK(snapshot.add_pg_reserved_srs_item(ObString::make_string(WORLD_MERCATOR_WKT), 999000) == OB_SUCCESS);
    const ObSrsItem *cached = nullptr;
    CHECK(snapshot.get_srs_item(999000, cached) == OB_SUCCESS && cached && cached->get_srid() == 999000);
    spatial_mbr_bridge(provider);
    spatial_cover_bridge(provider);
    for (bool definer : {false, true}) for (bool batch : {false, true})
      native_routine_call_test::run(provider, definer, batch, argv[2]);
    GisCatalogContext catalog(argv[2]);
    gis_srs_lookup_test::run(catalog);
    gis_srs_lifecycle_test::run();
    for (const char *option : {"", "   ", "axis-order=long-lat", "AXIS-ORDER = LAT-LONG",
        "\taxis-order = srid-defined\r\n", "axis-order=", "=long-lat", "bad=long-lat",
        "axis-order=bad", "axis-order=long-lat,axis-order=lat-long", "axis-order=long-lat extra"}) {
      seekdb::geo::srs::AxisOrder parsed;
      ObGeoAxisOrder legacy = ObGeoAxisOrder::INVALID;
      const bool ok = seekdb::geo::srs::parse_axis_order(option, parsed);
      const int ret = legacy_parse_axis_order(ObString::make_string(option), "ST_GeomFromText", legacy);
      CHECK(ok == (ret == OB_SUCCESS));
      if (ok) {
        CHECK((parsed == seekdb::geo::srs::AxisOrder::long_lat) == (legacy == ObGeoAxisOrder::LONG_LAT));
        CHECK((parsed == seekdb::geo::srs::AxisOrder::lat_long) == (legacy == ObGeoAxisOrder::LAT_LONG));
      }
    }
    for (const auto &record : gis_catalog_test::records) {
      SrsDefinition raw;
      raw.srid = record.srid; raw.definition = ObString::make_string(record.wkt);
      raw.proj4text = ObString::make_string(record.proj4);
      CHECK(snapshot.add_srs_definition(raw) == OB_SUCCESS);
    }
    char utm_wkt[4096]{};
    std::snprintf(utm_wkt, sizeof(utm_wkt), NORTH_UTM_WKT, 3);
    CHECK(snapshot.add_pg_reserved_srs_item(ObString::make_string(utm_wkt), 999031) == OB_SUCCESS);
    class CatalogProvider final : public ObISrsProvider {
    public:
      ObISrsSnapshot &snapshot;
      explicit CatalogProvider(ObISrsSnapshot &s) : snapshot(s) {}
      int get_tenant_srs_guard(ObSrsCacheGuard &guard) override
      { guard.bind(snapshot); return OB_SUCCESS; }
      int get_srs_bounds(uint64_t, const ObSrsItem *, const ObSrsBoundsItem *&) override
      { return OB_NOT_SUPPORTED; }
    } catalog_provider(snapshot);
    spatial_das_write(provider);
    spatial_das_scan(provider);
    spatial_backfill(provider);
    spatial_ranges(provider, catalog);
    native_spatial_ranges(provider, catalog);
    catalog.execution.set_srs_provider(&catalog_provider);
    catalog.use_database(false);
    for (const char *sql : {"ST_Area(NULL)", "POINT(1,2)", "GEOMETRYCOLLECTION(NULL)", "AREA(NULL)"})
      expression(provider, catalog, sql, false, true);
    expression(provider, catalog, "gis_db.ST_X(gis_db.POINT(3,4)) = 3");
    catalog.use_database(true);
    payload_materialization();
    for (const char *sql : {
        "ST_X(POINT(1,2)) = 1 AND ST_Y(POINT(1,2)) = 2",
        "ST_X(_ST_GeomFromEWKT('SRID=4326;POINT(2 49)')) = 2",
        "ST_Y(_ST_GeomFromEWKT('srid=4326;POINT(2 49)')) = 49",
        "ST_SRID(_ST_GeomFromEWKT('POINT(2 49)')) = 0",
        "ST_SRID(_ST_GeogFromText('POINT(2 49)')) = 4326",
        "ST_SRID(_ST_GeographyFromText('SRID=0;POINT(2 49)')) = 4326",
        "ST_X(_ST_GeogFromText('POINT(190 100)')) = -170",
        "ST_Y(_ST_GeogFromText('POINT(190 100)')) = 80",
        "ST_X(_ST_GeogFromText('POINT(-180 0)')) = -180",
        "ST_X(_ST_GeogFromText('POINT Z(-180 0 7)')) = 180",
        "HEX(_ST_AsEWKB(_ST_GeomFromEWKT('SRID=4326;POINT(2 49)'))) = '0101000020E610000000000000000000400000000000804840'",
        "HEX(_ST_AsEWKB(_ST_SetSRID(ST_MakePoint(2,49,7),4326))) = '01010000A0E6100000000000000000004000000000008048400000000000001C40'",
        "_ST_AsEWKB(POINT(2,49)) = ST_AsWKB(POINT(2,49))",
        "ST_X(_ST_GeomFromEWKB(UNHEX('0101000020E610000000000000000000400000000000804840'))) = 2",
        // EWKB dimensions come from the root Z flag, not ISO type offsets.
        "ST_X(_ST_GeomFromEWKB(UNHEX('01E9030000000000000000F03F0000000000000040'))) = 1",
        "ST_X(_ST_GeomFromEWKB(UNHEX('01D1070000000000000000F03F0000000000000040'))) = 1",
        "_ST_AsEWKT(_ST_GeomFromEWKB(UNHEX('0101000080000000000000F03F00000000000000400000000000000840'))) = 'POINT Z (1 2 3)'",
        "ST_X(_ST_GeomFromEWKB(UNHEX('0101000020E610000000000000008048400000000000000040'),'axis-order=lat-long')) = 2",
        "ST_X(_ST_GeomFromEWKB(UNHEX('0101000020E610000000000000000000400000000000804840'),NULL)) = 2",
        "ST_X(_ST_GeomFromEWKB(UNHEX('0101000020E610000000000000000000400000000000804840'),'axis-order=srid-defined')) = 2",
        "ST_AsWKB(_ST_GeomFromEWKB(_ST_AsEWKB(_ST_GeomFromEWKT('SRID=4326;GEOMETRYCOLLECTION(POINT(2 49),POINT(3 50))'))),'axis-order=long-lat') = ST_AsWKB(_ST_GeomFromEWKT('SRID=4326;GEOMETRYCOLLECTION(POINT(2 49),POINT(3 50))'),'axis-order=long-lat')",
        "ST_X(ST_GeomFromText('POINT(49 2)',4326)) = 2",
        "ST_Y(ST_GeomFromText('POINT(49 2)',4326)) = 49",
        "ST_AsText(ST_GeomFromText('POINT(49 2)',4326)) = 'POINT(49 2)'",
        "ST_AsText(ST_GeomFromText('POINT(49 2)',4326),'axis-order=long-lat') = 'POINT(2 49)'",
        "ST_AsText(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat'),'axis-order=lat-long') = 'POINT(49 2)'",
        "ST_X(ST_GeomFromWKB(ST_AsWKB(ST_GeomFromText('POINT(49 2)',4326)),4326)) = 2",
        "ST_Y(ST_GeometryFromWKB(ST_AsBinary(ST_GeomFromText('POINT(49 2)',4326),'axis-order=long-lat'),4326,'axis-order=long-lat')) = 49",
        "ST_AsWKT(ST_GeometryFromText('POINT(49 2)',4326,'axis-order=lat-long'),'axis-order=long-lat') = 'POINT(2 49)'",
        "ST_AsText(ST_GeomFromText('POINT(2 49)',0,'axis-order=lat-long'),'axis-order=lat-long') = 'POINT(2 49)'",
        "ST_AsText(ST_GeomFromText('POINT(500000 0)',32631,'axis-order=lat-long')) = 'POINT(500000 0)'",
        "ST_AsText(ST_GeomFromText('LINESTRING(49 2,50 3)',4326),'axis-order=long-lat') = 'LINESTRING(2 49,3 50)'",
        "ST_AsText(ST_GeomFromText('POINT Z(49 2 123)',4326),'axis-order=long-lat') = 'POINT Z (2 49 123)'",
        "ST_X(ST_GeomFromText('POINT(49 2)',4326,'   ')) = 2",
        "ST_AsText(ST_GeomFromWKB(UNHEX('000000000140488000000000004000000000000000'),4326),'axis-order=long-lat') = 'POINT(2 49)'",
        "ST_AsWKB(ST_GeomFromText('GEOMETRYCOLLECTION EMPTY',4326)) = UNHEX('010700000000000000')",
        "ST_AsWKB(ST_GeomFromText('GEOMETRYCOLLECTION()',4326)) = UNHEX('010700000000000000')",
        "ST_Distance(POINT(0,0),POINT(3,4)) = 5",
        "ST_Equals(ST_GeomFromText('LINESTRING(0 0,2 2)'),ST_GeomFromText('LINESTRING(0 2,2 0)')) = 0",
        "ST_Intersects(ST_GeomFromText('LINESTRING(0 0,2 2)'),ST_GeomFromText('LINESTRING(0 1,1 2)')) = 0",
        "ST_Distance(POINT(1,1),ST_GeomFromText('LINESTRING(0 0,2 0)')) = 1",
        "ST_Area(ST_Union(ST_GeomFromText('POLYGON((0 0,1 0,1 1,0 1,0 0))'),ST_GeomFromText('POLYGON((2 0,3 0,3 1,2 1,2 0))'))) = 2",
        "ST_Contains(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))'),POINT(2,2)) = 0",
        "ST_Area(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))')) = 12",
        "ABS(ST_X(ST_Centroid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(2 2,3 2,3 3,2 3,2 2))')))-29.5/15) < 0.000000001",
        "ST_X(ST_Centroid(ST_GeomFromText('GEOMETRYCOLLECTION(LINESTRING(0 0,2 0),LINESTRING(0 10,6 10),POINT(1000 1000))'))) = 2.5",
        "ST_Centroid(ST_GeomFromWKB(UNHEX('010700000000000000'))) IS NULL",
        "ST_Area(ST_Buffer(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0))'),-1)) = 4",
        "ST_Area(ST_Buffer(POINT(1,1),1,ST_Buffer_Strategy('point_square'))) = 4",
        "ABS(ST_Area(ST_Buffer(POINT(1,1),1,ST_Buffer_Strategy('POINT_CIRCLE',4)))-2) < 0.000000001",
        "ST_Area(ST_Buffer(ST_GeomFromText('LINESTRING(0 0,2 0)'),1,ST_Buffer_Strategy('end_flat'))) = 4",
        "ST_Area(ST_Buffer(ST_GeomFromText('LINESTRING(0 0,2 0,2 2)'),1,ST_Buffer_Strategy('end_flat'),ST_Buffer_Strategy('join_miter',5))) = 8",
        "ST_X(_ST_PointOnSurface(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))'))) = 0.5",
        "ST_Y(_ST_PointOnSurface(ST_GeomFromText('POLYGON((0 0,4 0,4 4,3 4,3 1,1 1,1 4,0 4,0 0))'))) = 2.5",
        "ST_Contains(ST_GeomFromText('POLYGON((0 0,4 0,4 4,3 4,3 1,1 1,1 4,0 4,0 0))'),_ST_PointOnSurface(ST_GeomFromText('POLYGON((0 0,4 0,4 4,3 4,3 1,1 1,1 4,0 4,0 0))'))) = 1",
        "ST_Y(_ST_PointOnSurface(ST_GeomFromText('LINESTRING(0 0,1 5,10 0)'))) = 5",
        "ST_X(_ST_PointOnSurface(ST_GeomFromText('MULTIPOINT((0 0),(10 0),(11 0))'))) = 10",
        "ST_X(_ST_PointOnSurface(ST_GeomFromText('POLYGON((2 3,3 3,4 3,2 3))'))) = 2",
        "ST_AsWKB(_ST_PointOnSurface(ST_GeomFromWKB(UNHEX('010700000000000000')))) = UNHEX('010700000000000000')",
        "ST_SRID(_ST_PointOnSurface(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0))',3857))) = 3857",
        "ST_Distance_Sphere(POINT(0,0),POINT(0,0)) = 0",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat'),3857))-6274861.394006576) < 0.000001",
        "ABS(ST_X(ST_Transform(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat'),3857))-222638.98158654713) < 0.000001",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(222638.98158654713 6274861.394006576)',3857),4326))-49) < 0.000000001",
        "ABS(ST_Y(_ST_Transform(ST_GeomFromText('POINT(120 -45)',4326,'axis-order=long-lat'),3857))+5621521.486192066) < 0.000001",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(0 89)',4326,'axis-order=long-lat'),3857))-30240971.95838615) < 0.000001",
        "ABS(ST_Area(ST_Transform(ST_GeomFromText('POLYGON((0 0,2 0,2 49,0 49,0 0))',4326,'axis-order=long-lat'),3857))"
          "/(222638.98158654713*6274861.394006576)-1) < 0.000000000001",
        "ABS(ST_Length(ST_Transform(ST_GeomFromText('GEOMETRYCOLLECTION(LINESTRING(0 0,0 49),LINESTRING(2 0,2 49))',4326,'axis-order=long-lat'),3857))"
          "/(2*6274861.394006576)-1) < 0.000000000001",
        "ST_SRID(ST_Transform(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat'),3857)) = 3857",
        "ABS(ST_X(ST_Transform(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),32631))-500000) < 0.000001",
        "ABS(ST_Y(ST_Transform(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),32631))) < 0.000001",
        "ABS(ST_X(ST_Transform(ST_Transform(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),32631),4326))-3) < 0.000000001",
        "ABS(ST_X(ST_Transform(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),_ST_BestSRID(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'))))-500000) < 0.000001",
        "ST_SRID(ST_Transform(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),_ST_BestSRID(ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat'),ST_GeomFromText('POINT(3 0)',4326,'axis-order=long-lat')))) = 999031",
        "ST_Area(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) = 12",
        "ST_Length(ST_GeomFromText('LINESTRING(0 0,3 4)')) = 5",
        "ST_X(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)))) = 3",
        "ST_Y(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)))) = 4",
        "ST_AsText(POINT(3,4)) = 'POINT(3 4)'",
        "ST_AsText(POINT(0.1,0.2)) = 'POINT(0.1 0.2)'",
        "ST_AsText(ST_GeomFromText('POINT(1 2 3)')) = 'POINT Z (1 2 3)'",
        "ST_AsText(ST_GeomFromText('pointz(1 2 3)')) = 'POINT Z (1 2 3)'",
        "ST_AsText(ST_GeomFromText('MULTIPOINT(1 2 3,4 5 6)')) = 'MULTIPOINT Z ((1 2 3),(4 5 6))'",
        "_ST_AsEWKT(_ST_GeomFromEWKT('LINESTRING(0 0 1,1 1 2)')) = 'LINESTRING Z (0 0 1,1 1 2)'",
        "ST_AsText(ST_GeomFromText('POLYGON Z((0 0 1,1 0 2,1 1 3,0 0 4))')) = 'POLYGON Z ((0 0 1,1 0 2,1 1 3,0 0 4))'",
        "ST_AsText(ST_GeomFromText('GEOMETRYCOLLECTION EMPTY')) = 'GEOMETRYCOLLECTION EMPTY'",
        "ST_AsText(ST_GeomFromText('MULTIPOINT Z((1 2 3),(4 5 6))')) = 'MULTIPOINT Z ((1 2 3),(4 5 6))'",
        "ST_AsText(ST_GeomFromText('MULTILINESTRING Z((0 0 1,1 1 2),(2 2 3,3 3 4))')) = 'MULTILINESTRING Z ((0 0 1,1 1 2),(2 2 3,3 3 4))'",
        "ST_AsWKB(ST_GeomFromText(ST_AsText(ST_GeomFromText('LINESTRING Z(0 0 1,1 1 2)')))) = ST_AsWKB(ST_GeomFromText('LINESTRING Z(0 0 1,1 1 2)'))",
        "_ST_AsEWKT(POINT(1.2345,-9.8765),2) = 'POINT(1.23 -9.88)'",
        "_ST_AsEWKT(POINT(1.2345,2),0) = 'POINT(1.2345 2)'",
        "_ST_AsEWKT(POINT(1.2345,2),-1) = _ST_AsEWKT(POINT(1.2345,2),25)",
        "_ST_AsEWKT(POINT(0.0000000012345,10000000000000000),2) = 'POINT(1.23e-9 1e16)'",
        "_ST_AsEWKT(_ST_GeomFromEWKT('SRID=4326;POINT(2 49)')) = 'SRID=4326;POINT(2 49)'",
        "_ST_AsEWKT(_ST_GeomFromEWKT('POINT Z(1.2345 2.3456 3.4567)'),1) = 'POINT Z (1.2345 2.3456 3.4567)'",
        "_ST_AsEWKT(_ST_GeomFromEWKT('MULTIPOINT Z((1 2 3),(4 5 6))')) = 'MULTIPOINT Z ((1 2 3),(4 5 6))'",
        "_ST_AsEWKT(ST_GeomFromText('MULTILINESTRING((0 0,1 1),(2 2,3 3))')) = 'MULTILINESTRING((0 0,1 1),(2 2,3 3))'",
        "_ST_AsEWKT(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) = 'POLYGON((0 0,4 0,4 3,0 3,0 0))'",
        "_ST_AsEWKT(_ST_GeomFromEWKT('GEOMETRYCOLLECTION(POINT(2 3),GEOMETRYCOLLECTION EMPTY)')) = 'GEOMETRYCOLLECTION(POINT(2 3),GEOMETRYCOLLECTION EMPTY)'",
        "_ST_AsEWKT(_ST_GeomFromEWKT('SRID=4326;GEOMETRYCOLLECTION EMPTY')) = 'SRID=4326;GEOMETRYCOLLECTION EMPTY'",
        "ST_SRID(POINT(3,4)) = 0",
        "ST_SRID(ST_GeomFromWKB(ST_AsWKB(POINT(3,4)),4326)) = 4326",
        "ST_IsValid(POINT(3,4)) = 1",
        "ST_IsValid(ST_GeomFromText('POLYGON((0 0,4 4,0 4,4 0,0 0))')) = 0",
        "ST_IsValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))')) = 1",
        "ST_IsValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(5 5,6 5,6 6,5 6,5 5))')) = 0",
        "ST_IsValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1),(2 2,3.5 2,3.5 3.5,2 3.5,2 2))')) = 0",
        "ST_IsValid(ST_GeomFromText('MULTIPOLYGON(((0 0,4 0,4 4,0 4,0 0)),((2 0,6 0,6 4,2 4,2 0)))')) = 0",
        "ST_IsValid(ST_GeomFromText('GEOMETRYCOLLECTION(POLYGON((0 0,4 0,4 4,0 4,0 0)),POLYGON((2 0,6 0,6 4,2 4,2 0)))')) = 1",
        "ST_IsValid(ST_GeomFromText('GEOMETRYCOLLECTION(POINT(0 0),POLYGON((0 0,4 4,0 4,4 0,0 0)))')) = 0",
        "ST_IsValid(ST_GeomFromText('LINESTRING(1 1,1 1)')) = 0",
        "ST_IsValid(ST_GeomFromText('POLYGON((2 3,3 3,4 3,2 3))')) = 0",
        "ST_IsValid(ST_GeomFromWKB(UNHEX('010700000000000000'))) = 1",
        "ST_IsValid(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 4,0 4,4 0,0 0))'))) = 1",
        "ST_Area(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 4,0 4,4 0,0 0))'))) = 8",
        "_ST_GeometryType(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 4,0 4,4 0,0 0))'))) = 'MULTIPOLYGON'",
        "ST_Area(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(5 5,6 5,6 6,5 6,5 5))'))) = 17",
        "ST_Area(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1),(2 2,3.5 2,3.5 3.5,2 3.5,2 2))'))) = 10.75",
        "ST_Area(_ST_MakeValid(ST_GeomFromText('MULTIPOLYGON(((0 0,4 0,4 4,0 4,0 0)),((2 0,6 0,6 4,2 4,2 0)))'))) = 24",
        "ST_Area(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(3 1,5 1,5 3,3 3,3 1))'))) = 16",
        "ST_Area(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))'))) = 12",
        "ST_IsValid(_ST_MakeValid(ST_GeomFromText('POLYGON((2 3,3 3,4 3,2 3))'))) = 0",
        "ST_AsWKB(_ST_MakeValid(ST_GeomFromWKB(UNHEX('010700000000000000')))) = UNHEX('010700000000000000')",
        "ST_AsWKB(_ST_MakeValid(ST_GeomFromWKB(UNHEX('010300000000000000')))) = UNHEX('010300000000000000')",
        "ST_SRID(_ST_MakeValid(ST_GeomFromText('POLYGON((0 0,4 4,0 4,4 0,0 0))',3857))) = 3857",
        "ST_Length(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(-1 0.5,2 0.5)'),ST_MakeEnvelope(0,0,1,1))) = 1",
        "_ST_GeometryType(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(-1 0.5,2 0.5)'),ST_MakeEnvelope(0,0,1,1))) = 'LINESTRING'",
        "ABS(ST_Length(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(-1 -1,2 2)'),ST_MakeEnvelope(0,0,1,1)))-SQRT(2)) < 0.000000001",
        "_ST_GeometryType(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(-1 0.5,0.5 2)'),ST_MakeEnvelope(0,0,1,1))) = 'GEOMETRYCOLLECTION'",
        "_ST_GeometryType(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(-1 0,2 0)'),ST_MakeEnvelope(0,0,1,1))) = 'GEOMETRYCOLLECTION'",
        "ST_Length(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(0 0,1 0)'),ST_MakeEnvelope(0,0,1,1))) = 1",
        "ST_Area(_ST_ClipByBox2D(ST_GeomFromText('POLYGON((0 0,4 0,0 4,0 0))'),ST_MakeEnvelope(1,1,4,4))) = 2",
        "ST_Area(_ST_ClipByBox2D(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))'),ST_MakeEnvelope(0,0,2,4))) = 6",
        "ST_Area(_ST_ClipByBox2D(ST_GeomFromText('POLYGON((-1 -1,5 -1,5 5,-1 5,-1 -1),(1 1,3 1,3 3,1 3,1 1))'),ST_MakeEnvelope(0,0,4,4))) = 12",
        "ST_Area(_ST_ClipByBox2D(ST_GeomFromText('POLYGON((0 0,4 0,4 4,3 4,3 1,1 1,1 4,0 4,0 0))'),ST_MakeEnvelope(0,2,4,4))) = 4",
        "ABS(ST_Area(_ST_ClipByBox2D(ST_GeomFromText('POLYGON((0 0,4 0,4 4,3 4,3 1,1 1,1 4,0 4,0 0),(0.2 2.5,0.8 2.5,0.8 3.5,0.2 3.5,0.2 2.5))'),ST_MakeEnvelope(0,2,4,4)))-3.4) < 0.000000001",
        "_ST_GeometryType(_ST_ClipByBox2D(ST_GeomFromText('POLYGON((0 0,4 0,4 4,3 4,3 1,1 1,1 4,0 4,0 0))'),ST_MakeEnvelope(0,2,4,4))) = 'MULTIPOLYGON'",
        "_ST_ClipByBox2D(ST_MakeEnvelope(0,0,4,4),POINT(1,1)) IS NULL",
        "_ST_ClipByBox2D(POINT(1,1),ST_GeomFromWKB(UNHEX('010700000000000000'))) IS NULL",
        "ST_X(_ST_ClipByBox2D(POINT(0.5,0.5),ST_MakeEnvelope(0,0,1,1))) = 0.5",
        "ST_X(_ST_ClipByBox2D(ST_GeomFromText('MULTIPOINT((0 0.5),(0.5 0.5),(2 2))'),ST_MakeEnvelope(0,0,1,1))) = 0.5",
        "ST_SRID(_ST_ClipByBox2D(ST_GeomFromText('LINESTRING(-1 0.5,2 0.5)',3857),ST_MakeEnvelope(0,0,1,1))) = 3857",
        "ST_X(_ST_AsMVTGeom(POINT(0.5,1.5),ST_MakeEnvelope(0,0,10,10),10,0)) = 0",
        "ST_Y(_ST_AsMVTGeom(POINT(0.5,1.5),ST_MakeEnvelope(0,0,10,10),10,0)) = 8",
        "ST_X(_ST_AsMVTGeom(POINT(5,5),ST_MakeEnvelope(0,0,10,10),NULL,NULL,NULL)) = 2048",
        "ST_X(_ST_AsMVTGeom(POINT(5,5),ST_MakeEnvelope(0,0,10,10))) = 2048",
        "ST_SRID(_ST_AsMVTGeom(ST_GeomFromText('POINT(5 5)',3857),ST_MakeEnvelope(0,0,10,10),10,0)) = 3857",
        "_ST_AsMVTGeom(POINT(20,20),ST_MakeEnvelope(0,0,10,10),10,0) IS NULL",
        "ST_X(_ST_AsMVTGeom(POINT(20,20),ST_MakeEnvelope(0,0,10,10),10,0,0)) = 20",
        "ST_Y(_ST_AsMVTGeom(POINT(20,20),ST_MakeEnvelope(0,0,10,10),10,0,0)) = -10",
        "ST_X(_ST_AsMVTGeom(POINT(20,20),ST_MakeEnvelope(0,0,10,10),10,10)) = 20",
        "ABS(ST_Length(_ST_AsMVTGeom(ST_GeomFromText('LINESTRING(-5 5,15 0)'),ST_MakeEnvelope(0,0,10,10),10,0))-SQRT(109)) < 0.000000001",
        "_ST_AsMVTGeom(ST_GeomFromText('LINESTRING(-1 0.5,0.5 2)'),ST_MakeEnvelope(0,0,1,1),10,0) IS NULL",
        "_ST_AsMVTGeom(ST_GeomFromText('LINESTRING(0.49 1,0.51 1)'),ST_MakeEnvelope(0,0,10,10),10,0,0) IS NULL",
        "_ST_AsMVTGeom(ST_MakeEnvelope(1.1,1.1,1.2,1.2),ST_MakeEnvelope(0,0,10,10),10,0) IS NULL",
        "_ST_AsMVTGeom(ST_GeomFromWKB(UNHEX('010700000000000000')),ST_MakeEnvelope(0,0,10,10)) IS NULL",
        "ST_Area(_ST_AsMVTGeom(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(1 1,3 1,3 3,1 3,1 1))'),ST_MakeEnvelope(0,0,10,10),10,0)) = 12",
        "ST_Area(_ST_AsMVTGeom(ST_GeomFromText('POLYGON((0 0,4 4,0 4,4 0,0 0))'),ST_MakeEnvelope(0,0,10,10),10,0,0)) = 8",
        "ST_Area(_ST_AsMVTGeom(GEOMETRYCOLLECTION(POINT(100,100),ST_MakeEnvelope(0,0,4,4)),ST_MakeEnvelope(0,0,10,10),10,0)) = 16",
        "_ST_GeometryType(_ST_AsMVTGeom(ST_GeomFromText('MULTIPOINT((1 1),(1.1 1.1))'),ST_MakeEnvelope(0,0,10,10),10,0)) = 'POINT'",
        "LENGTH(_ST_GeoHash(POINT(0,0))) = 20",
        "_ST_GeoHash(POINT(0,0),5) = 's0000'",
        "_ST_GeoHash(POINT(-180,-90),6) = '000000'",
        "_ST_GeoHash(POINT(180,90),6) = 'zzzzzz'",
        "LENGTH(_ST_GeoHash(POINT(0,0),64)) = 64",
        "_ST_GeoHash(POINT(0,0),0) = _ST_GeoHash(POINT(0,0))",
        "_ST_GeoHash(POINT(0,0),-1) = _ST_GeoHash(POINT(0,0))",
        "_ST_GeoHash(POINT(0,0),-2147483648) = _ST_GeoHash(POINT(0,0))",
        "LENGTH(_ST_GeoHash(POINT(0,0),NULL)) = 20",
        "_ST_GeoHash(ST_MakeEnvelope(-1,-1,1,1)) = ''",
        "_ST_GeoHash(ST_GeomFromText('POLYGON((0 0,4 0,0 4,0 0))'),8) = _ST_GeoHash(POINT(2,2),8)",
        "_ST_GeoHash(ST_GeomFromText('LINESTRING(0 0,0 0,0 0,4 4)'),8) = _ST_GeoHash(POINT(2,2),8)",
        "_ST_GeoHash(ST_GeomFromText('POLYGON((0 0,4 0,4 4,0 4,0 0),(500 500,600 500,600 600,500 600,500 500))'),8) = _ST_GeoHash(POINT(2,2),8)",
        "_ST_GeoHash(GEOMETRYCOLLECTION(POINT(0,0),POINT(4,4)),8) = _ST_GeoHash(POINT(2,2),8)",
        "_ST_GeoHash(ST_MakePoint(2,2,100),8) = _ST_GeoHash(POINT(2,2),8)",
        "_ST_GeoHash(ST_GeomFromText('POINT(120 30)',4326,'axis-order=long-lat'),8) = _ST_GeoHash(POINT(120,30),8)",
        "_ST_GeoHash(ST_GeomFromText('POINT(10 20)',3857),8) = _ST_GeoHash(POINT(10,20),8)",
        "_ST_GeoHash(ST_GeomFromWKB(UNHEX('010700000000000000'))) IS NULL",
        "_ST_GeoHash(ST_GeomFromWKB(UNHEX('010300000000000000'))) IS NULL",
        "_ST_GeoHash(ST_GeomFromWKB(UNHEX('010700000000000000')),2147483648) IS NULL",
        "_ST_BestSRID(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat')) = 999031",
        "_ST_BestSRID(ST_GeomFromText('POINT(18 -33)',4326,'axis-order=long-lat')) = 999134",
        "_ST_BestSRID(ST_GeomFromText('POINT(0 80)',4326,'axis-order=long-lat')) = 999061",
        "_ST_BestSRID(ST_GeomFromText('POINT(0 -80)',4326,'axis-order=long-lat')) = 999161",
        "_ST_BestSRID(ST_GeomFromText('POINT(180 0)',4326,'axis-order=long-lat')) = 999060",
        "_ST_BestSRID(ST_GeomFromText('LINESTRING(1 49,3 49)',4326,'axis-order=long-lat')) = 999031",
        "_ST_BestSRID(ST_GeomFromText('POLYGON((1 48,3 48,3 50,1 50,1 48))',4326,'axis-order=long-lat')) = 999031",
        "_ST_BestSRID(ST_GeomFromText('POLYGON((-135 80,-45 80,45 80,135 80,-135 80))',4326,'axis-order=long-lat')) = 999061",
        "_ST_BestSRID(ST_GeomFromText('MULTIPOINT((-10 5),(10 5))',4326,'axis-order=long-lat')) = 999229",
        "_ST_BestSRID(ST_GeomFromText('POINT(-10 5)',4326,'axis-order=long-lat'),ST_GeomFromText('POINT(10 5)',4326,'axis-order=long-lat')) = 999229",
        "_ST_BestSRID(ST_GeomFromText('MULTIPOINT((-100 -40),(100 40))',4326,'axis-order=long-lat')) = 999000",
        "_ST_BestSRID(_ST_SetSRID(ST_MakePoint(2,49,100),4326)) = 999031",
        "_ST_BestSRID(ST_GeomFromWKB(UNHEX('010700000000000000'))) = 999000",
        "_ST_BestSRID(ST_GeomFromWKB(UNHEX('010300000000000000'))) = 999000",
        "_ST_BestSRID(ST_GeomFromWKB(UNHEX('010700000000000000')),ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat')) = 999031",
        "_ST_BestSRID(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat'),ST_GeomFromWKB(UNHEX('010700000000000000'))) = 999031",
        "_ST_GeometryType(POINT(3,4)) = 'POINT'",
        "ST_Contains(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'),POINT(2,1)) = 1",
        "ST_X(ST_Centroid(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4)))) = 2",
        "ST_AsText(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4))) = 'GEOMETRYCOLLECTION(POINT(1 2),POINT(3 4))'",
        "ST_AsText(ST_GeomFromText(ST_AsText(GEOMETRYCOLLECTION(POINT(1,2),POINT(3,4))))) = 'GEOMETRYCOLLECTION(POINT(1 2),POINT(3 4))'",
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
    // Unqualified names remain internal built-ins for generated index columns.
    // They must not even evaluate their plugin-backed POINT operand.
    expression(provider, catalog, "spatial_cellid(POINT(3,4)) IS NULL", false);
    expression(provider, catalog, "spatial_mbr(POINT(3,4)) IS NULL", false);
    const int cell_calls = provider.function_calls_["org.seekdb.gis.function.spatial_cellid"];
    const int mbr_calls = provider.function_calls_["org.seekdb.gis.function.spatial_mbr"];
    // Qualify the SQL-package compatibility routines so the test cannot pass
    // merely by executing the built-in placeholders instead of the actual DSO.
    for (const char *sql : {
        "gis_db.spatial_cellid(POINT(3,4)) IS NULL",
        "gis_db.spatial_mbr(POINT(3,4)) IS NULL",
        "gis_db.spatial_cellid(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) IS NULL",
        "gis_db.spatial_mbr(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))')) IS NULL",
        "gis_db.spatial_cellid(ST_GeomFromWKB(UNHEX('010700000000000000'))) IS NULL",
        "gis_db.spatial_mbr(ST_GeomFromWKB(UNHEX('010700000000000000'))) IS NULL",
    }) expression(provider, catalog, sql);
    CHECK(provider.function_calls_["org.seekdb.gis.function.spatial_cellid"] == cell_calls + 3);
    CHECK(provider.function_calls_["org.seekdb.gis.function.spatial_mbr"] == mbr_calls + 3);
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
    expression(provider, catalog, "_ST_AsEWKT(NULL,2) IS NULL", false);
    const int ewkt_calls = provider.function_calls_["org.seekdb.gis.function.st_asewkt"];
    expression(provider, catalog, "_ST_AsEWKT(POINT(1,2),NULL) IS NULL");
    CHECK(provider.function_calls_["org.seekdb.gis.function.st_asewkt"] == ewkt_calls);
    expression(provider, catalog, "_ST_BestSRID(NULL) IS NULL", false);
    expression(provider, catalog, "gis_db.spatial_cellid(NULL) IS NULL", false);
    expression(provider, catalog, "gis_db.spatial_mbr(NULL) IS NULL", false);
    expression(provider, catalog, "_ST_GeoHash(NULL) IS NULL");
    expression(provider, catalog, "_ST_GeoHash(NULL,2147483648) IS NULL");
    expression(provider, catalog, "_ST_AsMVTGeom(NULL,ST_MakeEnvelope(0,0,10,10)) IS NULL");
    expression(provider, catalog, "ST_IsValid(NULL) IS NULL", false);
    expression(provider, catalog, "_ST_MakeValid(NULL) IS NULL", false);
    expression(provider, catalog, "POINT(NULL,2) IS NULL", false);
    expression(provider, catalog, "ST_GeomFromText(NULL) IS NULL", false);
    expression(provider, catalog, "ST_Transform(NULL,3857) IS NULL", false);
    expression(provider, catalog, "_ST_GeomFromEWKB(NULL,NULL) IS NULL");
    expression(provider, catalog, "_ST_GeogFromText(NULL) IS NULL", false);
    expression(provider, catalog, "ST_GeomFromText('POINT(3 4)',NULL) IS NULL", false);
    expression(provider, catalog, "ST_GeomFromText('POINT(49 2)',4326,NULL) IS NULL", false);
    const int astext_calls = provider.function_calls_["org.seekdb.gis.function.st_astext"];
    expression(provider, catalog, "ST_AsText(POINT(1,2),NULL) IS NULL");
    CHECK(provider.function_calls_["org.seekdb.gis.function.st_astext"] == astext_calls);
    for (const char *sql : {"ST_Area()", "ST_Area(POINT(1,2),POINT(3,4))",
         "ST_GeomFromText('LINESTRING(0 0)')",
         "ST_GeomFromText('MULTILINESTRING((0 0))')",
         "ST_GeomFromText('POLYGON((0 0,1 0,1 1,0 1))')",
         "ST_GeomFromText('MULTIPOLYGON(((0 0,1 0,0 0)))')",
         "ST_GeomFromText('MULTIPOINT(1 2,(3 4))')",
         "ST_GeomFromText('GEOMETRYCOLLECTION(POINT(1 2),POINT(3 4 5))')",
         "ST_GeomFromText('POINT Z(1 2)')",
         "ST_GeomFromText('POINT M(1 2 3)')",
         "ST_GeomFromText('POINT ZM(1 2 3 4)')",
         "ST_GeomFromText('POINT(0x1p2 3)')",
         "ST_GeomFromText('POINT EMPTY')",
         "ST_GeomFromText('POINT(91 2)',4326)",
         "_ST_GeomFromEWKT('SRID=99999;POINT(2 49)')",
         "_ST_GeomFromEWKT('SRID=4326;POINT(2 91)')",
         "_ST_GeomFromEWKT('SRID=4294967296;POINT(2 49)')",
         "_ST_GeomFromEWKT('SRID=-1;POINT(2 49)')",
         "_ST_GeomFromEWKT('SRID= 4326;POINT(2 49)')",
         "_ST_GeogFromText('SRID=3857;POINT(2 49)')",
         "_ST_GeomFromEWKT('POINT(2 49)',4326)",
         "_ST_GeogFromText('POINT(2 49)',4326)",
         "_ST_GeomFromEWKB(UNHEX('0020000001000010E640000000000000004048800000000000'))",
         "_ST_GeomFromEWKB(UNHEX('01E9030000000000000000F03F00000000000000400000000000000840'))",
         // Only the root may contain EWKB Z/SRID flags; children use WKB.
         "_ST_GeomFromEWKB(UNHEX('0107000080010000000101000080000000000000F03F00000000000000400000000000000840'))",
         "_ST_GeomFromEWKB(UNHEX('010700000001000000010100002000000000000000000000F03F0000000000000040'))",
         "ST_GeomFromText('POINT(0 -181)',4326)",
         "ST_GeomFromText('POINT(0 0)',99999)",
         "ST_GeomFromText('GEOMETRYCOLLECTION EMPTY',99999)",
         "ST_GeomFromText('POINT(0 0)',0,'axis-order=invalid')",
         "ST_AsText(POINT(1,2),'axis-order=invalid')",
         "ST_AsWKB(POINT(1,2),'axis-order=invalid')",
         "_ST_BestSRID(POINT(1,1))",
         "_ST_BestSRID(ST_GeomFromText('POINT(1 1)',3857))",
         "_ST_BestSRID(ST_GeomFromText('POINT(1 1)',99999))",
         "_ST_BestSRID(ST_GeomFromText('POINT(0 91)',4326,'axis-order=long-lat'))",
         "_ST_BestSRID(ST_GeomFromText('LINESTRING(0 0,180 0)',4326,'axis-order=long-lat'))",
         "_ST_GeoHash(POINT(181,0),5)", "_ST_GeoHash(POINT(0,-91),5)",
         "_ST_GeoHash(POINT(0,0),2147483648)", "_ST_GeoHash(POINT(0,0),-2147483649)",
         "_ST_GeoHash(ST_GeomFromText('POINT(1 1)',99999),5)",
         "_ST_AsMVTGeom(POINT(1,1),NULL)",
         "_ST_AsMVTGeom(NULL,ST_MakeEnvelope(0,0,10,10),0)",
         "_ST_AsMVTGeom(POINT(1,1),POINT(1,1))",
         "_ST_AsMVTGeom(POINT(1,1),ST_MakeEnvelope(0,0,10,10),0)",
         "_ST_AsMVTGeom(POINT(1,1),ST_MakeEnvelope(0,0,10,10),10.5)",
         "_ST_AsMVTGeom(POINT(1,1),ST_MakeEnvelope(0,0,10,10),2147483648)",
         "_ST_AsMVTGeom(POINT(1,1),ST_MakeEnvelope(0,0,10,10),10,-1)",
         "_ST_AsMVTGeom(POINT(1,1),ST_MakeEnvelope(0,0,10,10),10,0,128)",
         "_ST_AsMVTGeom(ST_GeomFromText('POINT(1 1)',4326,'axis-order=long-lat'),ST_MakeEnvelope(0,0,10,10))",
         "ST_Buffer(POINT(1,1),-1)", "ST_Buffer_Strategy('point_square',4)",
         "_ST_MakeValid(ST_GeomFromText('LINESTRING(1 1,1 1)'))",
         "_ST_MakeValid(ST_GeomFromText('GEOMETRYCOLLECTION(POLYGON((0 0,4 4,0 4,4 0,0 0)))'))",
         "ST_Buffer_Strategy('end_round')", "ST_Buffer_Strategy('point_circle',0)",
         "ST_Buffer_Strategy('point_circle',65537)", "ST_Buffer_Strategy('join_round_extra',32)",
         "ST_Buffer(POINT(1,1),1,ST_Buffer_Strategy('end_flat'))",
         "ST_Buffer(POINT(1,1),1,ST_Buffer_Strategy('point_square'),ST_Buffer_Strategy('point_circle',8))",
         "ST_Area('not geometry')", "POINT('not a number',2)",
         "ST_GeomFromText('POINT(3 4)',-1)", "ST_GeomFromText('POINT(3 4)',4294967296)",
         "ST_Transform(ST_GeomFromText('POINT(2 90)',4326,'axis-order=long-lat'),3857)",
         "ST_Transform(ST_GeomFromText('LINESTRING(2 49,2 -90)',4326,'axis-order=long-lat'),3857)",
         "ST_Transform(ST_GeomFromText('POLYGON((0 0,2 0,2 90,0 90,0 0))',4326,'axis-order=long-lat'),3857)",
         "ST_Transform(ST_GeomFromText('GEOMETRYCOLLECTION(POINT(2 49),POINT(2 90))',4326,'axis-order=long-lat'),3857)",
         "ST_Transform(POINT(2,49),3857)",
         "ST_Transform(ST_GeomFromText('POINT(2 49)',4326,'axis-order=long-lat'),99999)"}) {
      expression(provider, catalog, sql, false, true);
    }
    batch_area(provider, catalog);
  }
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.gis", status) == OB_SUCCESS && status.lease_count_ == 0);
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  CHECK(retained_srs->is_wgs84() && retained_srs->get_srid() == 4326);
  ObString retained_proj4;
  CHECK(retained_srs->get_proj4_param(&srs_arena, retained_proj4) == OB_SUCCESS && !retained_proj4.empty());
  const ObSrsItem *cached = nullptr;
  CHECK(snapshot.get_srs_item(999000, cached) == OB_SUCCESS && cached->get_srid() == 999000);
  retained_proj4.reset();
  CHECK(cached->get_proj4_param(&srs_arena, retained_proj4) == OB_SUCCESS && !retained_proj4.empty());
  std::cout << "PASS: actual SRS snapshot and host proxy remain readable after GIS DSO shutdown" << std::endl;
  std::cout << "PASS: GIS SQL/LOB expressions through real plugin; no live-server/storage claims" << std::endl;
  OTTZ_MGR.destroy();
}
