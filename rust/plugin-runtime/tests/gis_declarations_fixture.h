/*
 * Copyright (c) 2026 OceanBase.
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
#pragma once
#include "sql/resolver/ddl/extension_script.h"
#include "sql/resolver/ddl/ob_create_routine_resolver.h"
#include "sql/resolver/ddl/ob_create_routine_stmt.h"
#include "share/schema/native_routine_signature.h"
#include "share/schema/native_routine_create_slot.h"
#include "share/schema/routine_schema_overlay.h"
#include "routine_overlay_guard_fixture.h"
#include <map>
#include <set>

// Populate a controlled database view from the actual package, never from
// descriptor SQL names. Parsing, signature placement and DSO admission are
// real; identity allocation, catalog storage and grants remain test fixtures.
inline void stage_gis_catalog(const std::string &package_root,
    oceanbase::sql::ObResolverParams &params,
    oceanbase::share::schema::ObSchemaGetterGuard &guard,
    oceanbase::share::schema::ObSchemaMgr &manager,
    oceanbase::share::schema::RoutineSchemaOverlay &overlay,
    uint64_t grantee)
{
  using namespace oceanbase::common;
  using namespace oceanbase::sql;
  using namespace oceanbase::share;
  using namespace oceanbase::share::schema;
  ExtensionScript script;
  std::string error;
  CHECK(script.load(package_root, "gis", "1.0", params.session_info_->get_sql_mode(), error) == OB_SUCCESS);
  CHECK(script.statements().count() == 106);
  ObPackedObjPriv execute = 0;
  CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_EXECUTE, execute) == OB_SUCCESS);
  for (int64_t i = 0; i < script.statements().count(); ++i) {
    ObCreateFunctionResolver resolver(params);
    CHECK(resolver.resolve(*script.statements().at(i).node_) == OB_SUCCESS);
    const auto *statement = dynamic_cast<ObCreateRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
    ObRoutineInfo routine;
    CHECK(routine.assign(statement->get_routine_arg().routine_info_) == OB_SUCCESS);
    CHECK(routine.is_native_binding_valid());
    seekdb_plugin_sql_binding_v1_t binding{};
    std::vector<std::string> arguments;
    CHECK(PluginFunctionExpr::resolve_native_binding(routine, binding, arguments) == OB_SUCCESS);
    CHECK((binding.flags & SEEKDB_PLUGIN_EXTENSION_FLAG_IMPLEMENTATION_ONLY) != 0);
    routine.set_routine_id(340000 + i); routine.set_schema_version(42);
    CHECK(NativeRoutineCreateSlot::assign(guard, routine) == OB_SUCCESS);
    for (int64_t j = 0; j < routine.get_routine_params().count(); ++j) {
      auto *parameter = routine.get_routine_params().at(j);
      parameter->set_routine_id(routine.get_routine_id()); parameter->set_schema_version(42);
    }
    CHECK(overlay.stage(routine) == OB_SUCCESS);
    CHECK(MockSchemaService::grant_object(manager, routine.get_routine_id(), routine.get_owner_id(),
        grantee, execute) == OB_SUCCESS);
  }
}

// Admit source declarations against the actual GIS DSO and place each using
// Root's signature-aware slot allocator. IDs/storage remain controlled; this
// does not claim SQL catalog installation or durable commit.
inline void verify_gis_declarations(const std::string &package_root,
    oceanbase::sql::ObResolverParams &params, oceanbase::sql::ObSQLSessionInfo &session)
{
  using namespace oceanbase::common;
  using namespace oceanbase::sql;
  using namespace oceanbase::share::schema;
  ExtensionScript script;
  std::string error;
  const int loaded = script.load(package_root, "gis", "1.0", session.get_sql_mode(), error);
  std::cout << "GIS declaration source: status=" << loaded << " error=" << error << std::endl;
  CHECK(loaded == OB_SUCCESS);
  CHECK(script.source().native_module_ == "org.seekdb.gis");
  CHECK(script.statements().count() == 106);
  std::map<std::string, int> names;
  std::set<std::string> signatures;
  RoutineSchemaOverlay declarations;
  auto manager = std::make_unique<ObSchemaMgr>();
  auto service = std::make_unique<MockSchemaService>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSchemaGetterGuard catalog_guard;
  CHECK(MockSchemaService::bind(catalog_guard, *service, *manager) == OB_SUCCESS);
  CHECK(session.set_default_database(ObString::make_string("native_db")) == OB_SUCCESS);
  session.set_database_id(100);
  for (int64_t i = 0; i < script.statements().count(); ++i) {
    const auto &entry = script.statements().at(i);
    CHECK(entry.node_ && entry.node_->type_ == T_SF_CREATE);
    ObCreateFunctionResolver resolver(params);
    const int status = resolver.resolve(*entry.node_);
    if (status != OB_SUCCESS)
      std::cerr << "GIS declaration " << i << " failed: " << status << " SQL="
                << std::string(entry.sql_.ptr(), entry.sql_.length()) << std::endl;
    CHECK(status == OB_SUCCESS);
    const auto *statement = dynamic_cast<ObCreateRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
    const auto &routine = statement->get_routine_arg().routine_info_;
    CHECK(routine.is_native() && routine.is_native_binding_valid() && routine.get_route_sql().empty());
    CHECK(routine.get_database_id() == 100 && routine.get_native_module_id() == ObString::make_string("org.seekdb.gis"));
    const std::string implementation(routine.get_native_implementation_id().ptr(), routine.get_native_implementation_id().length());
    CHECK(implementation.find(".alias.") == std::string::npos); // SQL aliases share canonical code, not wrapper code.
    const std::string name(routine.get_routine_name().ptr(), routine.get_routine_name().length());
    ++names[name];
    seekdb_plugin_sql_binding_v1_t binding{};
    std::vector<std::string> arguments;
    CHECK(PluginFunctionExpr::resolve_native_binding(routine, binding, arguments) == OB_SUCCESS);
    CHECK(arguments.size() == routine.get_param_count());
    std::string signature = name;
    for (const auto &type : arguments) signature += ':' + type;
    if (NativeRoutineSignature::variadic(routine)) signature += "[]";
    CHECK(signatures.insert(signature).second);
    // Ensure the body-less native binding survives the real routine wire.
    std::vector<char> wire(routine.get_serialize_size()); int64_t position = 0;
    CHECK(routine.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
    ObRoutineInfo decoded; position = 0;
    CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS);
    CHECK(decoded.is_native_binding_valid() && decoded.get_native_implementation_id() == routine.get_native_implementation_id());
    // Real slot allocation over the complete preceding candidate family;
    // object IDs/schema snapshots remain controlled, not durable allocation.
    decoded.set_routine_id(330000 + i); decoded.set_schema_version(42);
    CHECK(NativeRoutineCreateSlot::assign(catalog_guard, decoded) == OB_SUCCESS);
    CHECK(decoded.get_overload() == names[name] - 1);
    for (int64_t j = 0; j < decoded.get_routine_params().count(); ++j) {
      decoded.get_routine_params().at(j)->set_routine_id(330000 + i);
      decoded.get_routine_params().at(j)->set_schema_version(42);
    }
    CHECK(declarations.stage(decoded) == OB_SUCCESS);
    bool handled = false;
    const ObRoutineInfo *owned = nullptr;
    CHECK(declarations.lookup(decoded.get_routine_id(), handled, owned) == OB_SUCCESS && handled && owned);
    CHECK(MockSchemaService::add(*manager, 100, name.c_str(), owned->get_routine_id(),
        ROUTINE_FUNCTION_TYPE, 42, owned->get_overload()) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_routine(catalog_guard, *owned) == OB_SUCCESS);
  }
  CHECK(names.size() == 82);
  CHECK(names.at("st_makepoint") == 2 && names.at("st_asgeojson") == 3);
  CHECK(names.at("st_buffer") == 2 && names.at("_st_asmvtgeom") == 4);
  CHECK(names.at("point") == 1 && names.at("geometrycollection") == 1 && names.at("geomcollection") == 1);
  ObSEArray<const ObRoutineInfo *, 1> base;
  ObSEArray<const ObRoutineInfo *, 4> family;
  ObSEArray<const ObRoutineInfo *, 4> indexed;
  for (const auto &entry : names) {
    CHECK(declarations.merge_function_candidates(100, ObString(entry.first.size(), entry.first.data()), base, family) == OB_SUCCESS);
    CHECK(family.count() == entry.second);
    CHECK(catalog_guard.get_standalone_function_infos(100,
        ObString(entry.first.size(), entry.first.data()), indexed) == OB_SUCCESS);
    CHECK(indexed.count() == family.count());
    for (int64_t i = 0; i < family.count(); ++i) {
      CHECK(family.at(i)->get_overload() == i && indexed.at(i) == family.at(i));
    }
  }
  CHECK(session.set_default_database(ObString::make_string("caller_db")) == OB_SUCCESS);
  session.set_database_id(101);
  std::cout << "PASS: 106 GIS native declarations for 82 SQL names, exact DSO admission, Root slot allocation, wire, owned overload families and schema-manager/guard candidate index; no persistent installation claims" << std::endl;
}
