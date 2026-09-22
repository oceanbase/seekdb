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
  int execute_plugin_function(const char *name, uint32_t major, uint32_t minor,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  { ++calls_; return loader_.execute_function(name, major, minor, ctx, args, count); }
  int execute_plugin_extension(seekdb_plugin_extension_kind_t kind, const char *name,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  { ++calls_; return loader_.execute_extension(kind, name, ctx, args, count); }
  int resolve_plugin_sql_object(seekdb_plugin_extension_kind_t kind, const char *name,
      const char *const *types, uint32_t count, seekdb_plugin_sql_binding_v1_t *binding) override
  { return loader_.resolve_sql_extension(kind, name, types, count, *binding); }
  int execute_bound_plugin_function(const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *ctx,
      const seekdb_plugin_execution_value_v1 *args, uint32_t count) override
  { ++calls_; return loader_.execute_bound_function(*binding, ctx, args, count); }
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

static void expression(GisProvider &provider, const char *sql, bool invokes_plugin = true)
{
  std::cout << "CHECK: " << sql << std::endl;
  ObArenaAllocator arena;
  auto session = std::make_unique<ObSQLSessionInfo>();
  CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
  ObRawExprFactory factory(arena);
  ObExecContext execution(arena);
  LobService lob_service;
  execution.set_lob_read_service(&lob_service);
  execution.set_my_session(session.get());
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister register_execution(*session, &execution);
  const ParseNode *node = nullptr;
  CHECK(ObRawExprUtils::parse_expr_node_from_str(ObString::make_string(sql),
      session->get_charsets4parser(), arena, node, session->get_sql_mode()) == OB_SUCCESS);
  ObSEArray<ObQualifiedName, 1> columns;
  ObSEArray<ObVarInfo, 1> variables;
  ObSEArray<ObAggFunRawExpr *, 1> aggregates;
  ObSEArray<ObWinFunRawExpr *, 1> windows;
  ObSEArray<ObSubQueryInfo, 1> subqueries;
  ObSEArray<ObUDFInfo, 1> udfs;
  ObSEArray<ObOpRawExpr *, 1> operators;
  ObRawExpr *raw = nullptr;
  CHECK(ObRawExprUtils::build_raw_expr(factory, *session, *node, raw, columns,
      variables, aggregates, windows, subqueries, udfs, operators) == OB_SUCCESS);
  CHECK(raw && raw->formalize(session.get()) == OB_SUCCESS);
  ObRawExprUniqueSet roots(false);
  CHECK(roots.append(raw) == OB_SUCCESS);
  ObStaticEngineExprCG generator(arena, session.get(), nullptr, 0, 0);
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
  const int status = root->eval(eval, value);
  if (status != OB_SUCCESS) std::cerr << "GIS expression error " << status << ": " << sql << std::endl;
  CHECK(status == OB_SUCCESS && value && !value->is_null() && value->get_int() == 1);
  if (invokes_plugin) CHECK(provider.calls_ > before);
  std::cout << "PASS: " << sql << std::endl;
  ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(*session, nullptr);
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
  CHECK(argc == 2);
  OB_LOGGER.set_file_name("gis_sql.log", true);
  OB_LOGGER.set_enable_async_log(false);
  OB_LOGGER.set_log_level("WARN");
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  CHECK(init_sql_factories() == OB_SUCCESS);
  CHECK(ObSysVariables::init_default_values() == OB_SUCCESS);
  CHECK(ObBasicSessionInfo::init_sys_vars_cache_base_values() == OB_SUCCESS);
  native_activation_test::Observation observation;
  observation.gis = true;
  auto guard = std::make_shared<native_activation_test::TestGuard>(observation);
  ObPluginLoader loader;
  const std::string path(argv[1]);
  const auto slash = path.rfind('/'); CHECK(slash != std::string::npos);
  CHECK(loader.init(path.substr(0, slash),
      std::make_shared<native_activation_test::TestVerifier>(false, true, false),
      guard, guard, observation.registry) == OB_SUCCESS);
  CHECK(loader.load(path.substr(slash + 1)) == OB_SUCCESS);
  {
    GisProvider provider(loader);
    payload_materialization();
    for (const char *sql : {
        "ST_X(POINT(1,2)) = 1 AND ST_Y(POINT(1,2)) = 2",
        "ST_Distance(POINT(0,0),POINT(3,4)) = 5",
        "ST_Distance_Sphere(POINT(0,0),POINT(0,0)) = 0",
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
    }) expression(provider, sql);
    expression(provider, "ST_Distance(NULL,POINT(0,0)) IS NULL", false);
    expression(provider, "ST_Area(NULL) IS NULL", false);
  }
  ObPluginStatusSnapshot status;
  CHECK(loader.get_status("org.seekdb.gis", status) == OB_SUCCESS && status.lease_count_ == 0);
  CHECK(loader.shutdown_for_process_exit(1000000) == OB_SUCCESS);
  std::cout << "PASS: GIS SQL/LOB expressions through real plugin; no live-server/storage claims" << std::endl;
}
