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
#include "rootserver/ob_runtime_ddl_service.h"
#include "rootserver/ob_snapshot_info_manager.h"
#include "share/plugin/extension_routine_update.h"
#include "share/plugin/ob_plugin_catalog.h"

// Real Root installer, routine/ACL SQL writers, catalog binder and Rust install
// coordinator. Only resolved inputs, schema allocation, module capability and
// SQL transport are supplied. Does not simulate durable rollback or isolation.
namespace routine_extension_install_test {
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::share::plugin;
using namespace oceanbase::rootserver;
using namespace oceanbase::obcall;

class Transaction final : public ObDDLSQLTransaction {
public:
  Transaction(ObMultiVersionSchemaService &service, ExtensionVersionRows &rows)
      : ObDDLSQLTransaction(&service), rows(rows) {}
  ExtensionVersionRows &rows;
  int end_status = OB_SUCCESS;
  int invalidations = 0;
  int record_routine_invalidation(uint64_t routine, uint64_t database) override {
    CHECK(rows.active && routine == 400001 && database == 100);
    ++invalidations; return OB_SUCCESS; // Queue/outcome semantics exercised by routine_ddl_invalidation_fixture.
  }
  bool is_started() const override { return rows.active; }
  int start(ObISQLClient *client, const int64_t &version, bool snapshot = false) override {
    CHECK(version == 42 && !snapshot && !rows.active);
    return rows.start(client, version, snapshot);
  }
  int start(ObISQLClient *, bool, int32_t) override { CHECK(false); return OB_ERR_UNEXPECTED; }
  int end(bool commit) override {
    CHECK(rows.active);
    const int ret = rows.end(commit);
    return end_status == OB_SUCCESS ? ret : end_status;
  }
  int write(const char *sql, int32_t group, int64_t &affected) override {
    CHECK(rows.active); return rows.write(sql, group, affected);
  }
  int read(ReadResult &result, const char *sql, int32_t group) override {
    CHECK(rows.active); return rows.read(result, sql, group);
  }
};

class Script final : public IExtensionRoutineScript {
public:
  ObCreateRoutineArg first, last, alter;
  ObDropRoutineArg drop;
  ObGrantArg grant;
  ObRevokeRoutineArg revoke;
  ExtensionRoutineUpdateOperation operation;
  ExtensionRoutineUpdateOperation operations[5];
  bool update = false, published = false;
  int mixed = 0; // 1: insert ALTER before REVOKE; 2: replace REVOKE with DROP and recreate the name.
  int phase = -1, resolves = 0, fail_resolve = -1;
  ExtensionVersionRows &rows;
  explicit Script(ExtensionVersionRows &rows) : rows(rows) {
    for (auto *arg : {&first, &last}) {
      arg->db_name_ = ObString::make_string("install_db");
      auto &routine = arg->routine_info_;
      routine.set_database_id(100); routine.set_owner_id(123);
      routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
      routine.set_package_id(OB_INVALID_ID); routine.set_overload(0); routine.set_subprogram_id(0);
      CHECK(routine.set_routine_name(ObString::make_string(arg == &first ? "install_value" : "install_tail")) == OB_SUCCESS);
      CHECK(routine.set_native_binding(ObString::make_string("org.seekdb.gis"),
          ObString::make_string("org.seekdb.gis.function.st_area"), 1) == OB_SUCCESS);
      CHECK(routine.set_routine_body(ObString::make_string(
          "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_area' LANGUAGE C")) == OB_SUCCESS);
      ObRoutineParam result;
      result.set_sequence(0); result.set_subprogram_id(0); result.set_param_position(0); result.set_param_level(0);
      result.set_param_type(ObDoubleType);
      CHECK(routine.add_routine_param(result) == OB_SUCCESS);
      CHECK(arg->is_valid());
    }
  }
  int64_t statement_count() const override { return (published ? 3 : 4) + (mixed == 1); }
  int preflight(const ExtensionUpdateRequest &, std::string &) override { return update ? OB_SUCCESS : OB_NOT_SUPPORTED; }
  int preflight_install(const ExtensionInstallSpec &, std::string &) override { return OB_SUCCESS; }
  int validate_view(ObSchemaGetterGuard &, std::string &) override { return OB_SUCCESS; }
  int resolve(int64_t index, ObSchemaGetterGuard &view,
      const ExtensionRoutineUpdateOperation *&output, std::string &) override {
    CHECK(rows.active && rows.starts == 1 && rows.ends == 0 && index == resolves);
    output = nullptr; ++resolves;
    if (update) CHECK(rows.writes == 1); // Only the coordinator's dependency fence; no routine/ACL SQL in admission.
    if (index == fail_resolve) return OB_USER_NOT_EXIST;
    const int64_t slot = index;
    if (published) ++index;
    const bool alter_step = mixed == 1 && index == 2;
    if (mixed == 1 && index > 2) --index;
    phase = index;
    if (index == 0) operation = {ExtensionRoutineUpdateOperation::Kind::CREATE, &first};
    else if (mixed == 2 && index == 3) {
      const ObRoutineInfo *removed = nullptr;
      CHECK(view.get_routine_info(400001, removed) == OB_SUCCESS && !removed);
      operation = {ExtensionRoutineUpdateOperation::Kind::CREATE, &last};
    }
    else {
      const ObRoutineInfo *routine = nullptr;
      CHECK(view.get_routine_info(400001, routine) == OB_SUCCESS && routine);
      CHECK(routine->get_schema_version() == (mixed == 1 && index >= 2 && !alter_step
          ? (published ? 1003 : 1004) : (published ? 42 : 1001)) && rows.writes > 0);
      if (index == 1) {
        CHECK(grant.native_target_.assign(*routine, true) == OB_SUCCESS);
        ObSEArray<uint64_t, 1> roles;
        CHECK(grant.native_target_.bind_actor(123, roles) == OB_SUCCESS);
        grant.grantor_id_ = 123; grant.grantor_ = ObString::make_string("owner");
        grant.grantor_host_ = ObString::make_string("localhost");
        grant.db_ = first.db_name_; grant.table_ = routine->get_routine_name();
        grant.object_id_ = routine->get_routine_id(); grant.object_type_ = ObObjectType::FUNCTION;
        grant.priv_level_ = OB_PRIV_ROUTINE_LEVEL; grant.priv_set_ = OB_PRIV_EXECUTE;
        CHECK(grant.users_passwd_.push_back(ObString::make_string("recipient")) == OB_SUCCESS);
        CHECK(grant.users_passwd_.push_back(ObString()) == OB_SUCCESS);
        CHECK(grant.hosts_.push_back(ObString::make_string("localhost")) == OB_SUCCESS);
        CHECK(grant.is_valid());
        operation = {ExtensionRoutineUpdateOperation::Kind::GRANT, nullptr, nullptr, &grant};
      } else {
        ObSessionPrivInfo recipient;
        recipient.user_id_ = 124; recipient.user_name_ = ObString::make_string("recipient");
        recipient.host_name_ = ObString::make_string("localhost");
        ObSEArray<uint64_t, 1> roles;
        const int permission = view.check_native_routine_priv(recipient, roles, *routine, OB_PRIV_EXECUTE);
        std::cout << "Root install private ACL: index=" << index << " permission=" << permission << std::endl;
        CHECK(permission == (index == 2 ? OB_SUCCESS : OB_ERR_NO_ROUTINE_PRIVILEGE));
        if (alter_step) {
          alter.db_name_ = first.db_name_; alter.is_need_alter_ = true;
          CHECK(alter.routine_info_.assign(*routine) == OB_SUCCESS);
          CHECK(alter.routine_info_.set_comment(ObString::make_string("private alteration")) == OB_SUCCESS);
          CHECK(alter.is_valid());
          operation = {ExtensionRoutineUpdateOperation::Kind::ALTER, &alter};
        } else if (mixed == 2 && index == 2) {
          drop.db_name_ = first.db_name_; drop.routine_name_ = routine->get_routine_name();
          drop.routine_type_ = ROUTINE_FUNCTION_TYPE; drop.native_target_resolved_ = true;
          CHECK(drop.native_target_.assign(*routine) == OB_SUCCESS && drop.is_valid());
          operation = {ExtensionRoutineUpdateOperation::Kind::DROP, nullptr, &drop};
        } else if (index == 2) {
          CHECK(revoke.native_target_.assign(*routine, true) == OB_SUCCESS);
          CHECK(revoke.native_target_.bind_actor(123, roles) == OB_SUCCESS);
          revoke.grantor_id_ = 123; revoke.grantor_ = grant.grantor_; revoke.grantor_host_ = grant.grantor_host_;
          revoke.db_ = first.db_name_; revoke.routine_ = routine->get_routine_name();
          revoke.obj_id_ = routine->get_routine_id(); revoke.obj_type_ = uint64_t(ObObjectType::FUNCTION);
          revoke.priv_set_ = OB_PRIV_EXECUTE;
          ObSEArray<uint64_t, 1> users; CHECK(users.push_back(124) == OB_SUCCESS);
          CHECK(revoke.set_native_grantees(users) == OB_SUCCESS && revoke.is_valid());
          operation = {ExtensionRoutineUpdateOperation::Kind::REVOKE, nullptr, nullptr, nullptr, &revoke};
        } else operation = {ExtensionRoutineUpdateOperation::Kind::CREATE, &last};
      }
    }
    operations[slot] = operation;
    output = &operations[slot]; return OB_SUCCESS;
  }
};

inline int run_case(int fail_write, int fail_resolve = -1, int end_status = OB_SUCCESS, bool update = false,
    bool published = false, bool drift = false, int mixed = 0)
{
  auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSimpleServerRuntimeSchema runtime;
  runtime.set_schema_version(42); runtime.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  runtime.set_status(SERVER_RUNTIME_STATUS_NORMAL);
  CHECK(runtime.set_runtime_name(ObString::make_string("install_fixture")) == OB_SUCCESS);
  CHECK(manager->add_runtime_schema(runtime) == OB_SUCCESS);
  ObDatabaseSchema database; database.set_database_id(100); database.set_schema_version(42);
  CHECK(database.set_database_name("install_db") == OB_SUCCESS);
  ObSimpleDatabaseSchema simple_database;
  simple_database.set_database_id(100); simple_database.set_schema_version(42);
  CHECK(simple_database.set_database_name(database.get_database_name_str()) == OB_SUCCESS);
  CHECK(manager->add_database(simple_database) == OB_SUCCESS);
  ObSysVariableSchema variables; variables.set_schema_version(42);
  CHECK(variables.load_default_system_variable() == OB_SUCCESS);
  int64_t var_index = OB_INVALID_INDEX;
  CHECK(ObSysVarMeta::calc_sys_var_store_idx(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, var_index) == OB_SUCCESS);
  CHECK(variables.get_sysvar_schema(var_index)->set_value(ObString::make_string("0")) == OB_SUCCESS);
  ObUserInfo owner, recipient;
  for (auto *user : {&owner, &recipient}) {
    user->set_user_id(user == &owner ? 123 : 124); user->set_schema_version(42);
    CHECK(user->set_user_name(user == &owner ? "owner" : "recipient") == OB_SUCCESS);
    CHECK(user->set_host("localhost") == OB_SUCCESS);
    ObSimpleUserSchema simple; simple.set_user_id(user->get_user_id()); simple.set_schema_version(42);
    CHECK(simple.set_user_name(user->get_user_name_str()) == OB_SUCCESS);
    CHECK(simple.set_host(user->get_host_name_str()) == OB_SUCCESS);
    CHECK(manager->add_user(simple) == OB_SUCCESS);
  }
  owner.set_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
  struct Service final : MockSchemaService {
    ObSchemaMgr &manager; ObDatabaseSchema &database; ObSysVariableSchema &variables;
    ObUserInfo &owner, &recipient;
    Service(ObSchemaMgr &m, ObDatabaseSchema &d, ObSysVariableSchema &v, ObUserInfo &o, ObUserInfo &r)
        : manager(m), database(d), variables(v), owner(o), recipient(r) {}
    ~Service() override { schema_service_ = nullptr; }
    void bind_sql(ObSchemaService &sql) { schema_service_ = &sql; }
    int get_runtime_refreshed_schema_version(int64_t &version, bool = false) const override
    { version = 42; return OB_SUCCESS; }
    int get_runtime_schema_guard(ObSchemaGetterGuard &guard, int64_t = OB_INVALID_VERSION,
        RefreshSchemaMode = NORMAL) override {
      int ret = MockSchemaService::bind(guard, *this, manager);
      if (ret == OB_SUCCESS) ret = cache_database(guard, database);
      if (ret == OB_SUCCESS) ret = cache_variables(guard, variables);
      if (ret == OB_SUCCESS) ret = cache_user(guard, owner);
      if (ret == OB_SUCCESS) ret = cache_user(guard, recipient);
      return ret;
    }
  };
  auto service = std::make_unique<Service>(*manager, database, variables, owner, recipient);
  ObMySQLProxy proxy; routine_version_test::Allocator versions(proxy, *service); service->bind_sql(versions);
  ObSchemaGetterGuard view; CHECK(service->get_runtime_schema_guard(view) == OB_SUCCESS);
  ObSnapshotInfoManager snapshots; ObRuntimeDDLService runtime_ddl; ObDDLService ddl;
  CHECK(ddl.init(proxy, *service, snapshots, runtime_ddl) == OB_SUCCESS);
  ExtensionVersionRows rows; rows.rows.clear(); rows.write_status = rows.transaction_status = OB_SUCCESS;
  rows.fail_write_at = fail_write;
  Transaction transaction(*service, rows); transaction.end_status = end_status;
  Script script(rows); script.fail_resolve = fail_resolve; script.update = update; script.published = published;
  script.mixed = mixed;
  if (mixed == 2) CHECK(script.last.routine_info_.set_routine_name(ObString::make_string("install_value")) == OB_SUCCESS);
  if (published) {
    auto &routine = script.first.routine_info_;
    routine.set_routine_id(400001); routine.set_schema_version(42);
    CHECK(MockSchemaService::add(*manager, 100, "install_value", 400001, ROUTINE_FUNCTION_TYPE, 42) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_routine(view, routine) == OB_SUCCESS);
    versions.ids_ = 1;
  }
  bool granted = false, detached = false, altered = false;
  if (update) rows.affected_rows = [&](const std::string &sql) {
    if (sql.find("DELETE FROM __all_extension_member") == 0) {
      CHECK(script.resolves == script.statement_count() && !detached); detached = true;
      const ObRoutineInfo *hidden = nullptr;
      CHECK(view.get_routine_info(400001, hidden) == OB_SUCCESS && bool(hidden) == published);
      if (drift) { CHECK(published); granted = true; } // Actual SQL state differs from the admitted before-image.
      return int64_t{published ? 1 : 0};
    }
    if (sql.find("REPLACE INTO oceanbase.__all_objauth ") == 0) {
      CHECK(detached && !granted); granted = true;
    }
    if (sql.find("DELETE FROM oceanbase.__all_objauth ") == 0) {
      CHECK(detached && granted); granted = false;
    }
    if (sql.find("UPDATE oceanbase.__all_routine ") == 0) { CHECK(mixed == 1); altered = true; }
    return int64_t{1};
  };
  rows.on_read = [&](ExtensionVersionRows &transport) {
    CHECK(transport.active); transport.rows.clear();
    using Row = ExtensionVersionRows::Row;
    if (update && transport.sql.find("SELECT extension_id,owner_id,extension_version,native_module_id") == 0) {
      Row row; row.integers = {{0,91},{1,123}}; row.strings = {{2,"1.0"},{3,""}};
      transport.rows.push_back(row);
    } else if (update && transport.sql.find("SELECT object_class,object_id") == 0) {
      if (published) { Row row; row.integers = {{0,ROUTINE_SCHEMA},{1,400001}}; transport.rows.push_back(row); }
    } else if (update && transport.sql.find("SELECT extension_id FROM oceanbase.__all_extension_member ") == 0) {
      CHECK(detached && mixed == 2); // Coordinator detached this member in the same transaction.
    } else if (update && (transport.sql.find("SELECT i.extension_name") == 0 ||
        transport.sql.find("SELECT required_extension_id,extension_id") == 0 ||
        transport.sql.find("SELECT * FROM oceanbase.__all_dependency ") == 0 ||
        transport.sql.find("SELECT dep_obj_id,dep_obj_type FROM __all_dependency ") == 0 ||
        transport.sql.find("SELECT dep_obj_id, dep_obj_type") == 0)) {
      // Existing empty extension; the update adds precisely its two CREATE members.
    } else if (transport.sql.find("SELECT database_id,owner_id") == 0) {
      const ObRoutineInfo *routine = nullptr;
      ObRoutineInfo created;
      if (update) {
        CHECK(detached || published); // A private CREATE must not be locked before it is written.
        const bool last = transport.sql.find("400002") != std::string::npos;
        CHECK(created.assign(last ? script.last.routine_info_ : script.first.routine_info_) == OB_SUCCESS);
        created.set_routine_id(last ? 400002 : 400001);
        created.set_schema_version(last ? (published ? 1003 : 1004) + (mixed == 1 ? 2 : 0)
            : (altered ? (published ? 1003 : 1004) : (published ? 42 : 1001)));
        routine = &created;
      } else CHECK(view.get_routine_info(script.phase == 3 ? 400002 : 400001, routine) == OB_SUCCESS && routine);
      Row row;
      row.integers = {{0,100},{1,123},{2,routine->get_overload()},{3,routine->get_schema_version()},
          {4,ROUTINE_FUNCTION_TYPE},{5,1}};
      row.strings = {{6,std::string(routine->get_routine_name().ptr(), routine->get_routine_name().length())},
          {7,"org.seekdb.gis"},{8,"org.seekdb.gis.function.st_area"}};
      transport.rows.push_back(row);
    } else if (transport.sql.find("SELECT grantee_id,grantor_id,col_id") == 0) {
      if (update ? granted && transport.sql.find("400002") == std::string::npos : script.phase == 2) {
        Row row; row.integers = {{0,124},{1,123},{2,OBJ_LEVEL_FOR_TAB_PRIV},{3,OBJ_PRIV_ID_EXECUTE},{4,0}};
        transport.rows.push_back(row);
      }
    } else if (transport.sql.find("SELECT priv_id,priv_option") == 0) {
      if (update ? granted : script.phase == 2) {
        Row row; row.integers = {{0,OBJ_PRIV_ID_EXECUTE},{1,0}}; transport.rows.push_back(row);
      }
    } else if (transport.sql.find("SELECT next_value") == 0) {
      CHECK(script.resolves == script.statement_count()); Row row; row.id = 91; transport.rows.push_back(row);
    } else if (transport.sql.find("SELECT native_module_id") != 0 &&
               transport.sql.find("SELECT obj_id, obj_seq FROM oceanbase.__all_error") != 0 &&
               transport.sql.find("SELECT all_priv FROM oceanbase.__all_routine_privilege") != 0) {
      std::cerr << "unexpected install query: " << transport.sql << std::endl; CHECK(false);
    }
  };
  plugin_expression_test::Provider provider;
  provider.native_resolution_ = [](const char *module, const char *implementation,
      const char *const *arguments, uint32_t count, seekdb_plugin_sql_binding_v1_t *binding) {
    CHECK(!arguments && count == 0);
    *binding = {}; binding->struct_size = sizeof(*binding); binding->kind = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
    binding->owner_generation = 7; binding->catalog_epoch = 11;
    std::strcpy(binding->owner_plugin_id, module); std::strcpy(binding->object_id, implementation);
    std::strcpy(binding->result_type_id, "core.type.float64"); return OB_SUCCESS;
  };
  provider.native_dependency_ = [&](ObISQLClient &client, const ObString &, const ObString &,
      uint64_t id, bool add, uint64_t generation) {
    CHECK(&client == &transaction && transaction.is_started() && generation == (add ? 7U : 0U));
    CHECK(add || (mixed == 2 && id == 400001 && transaction.invalidations == 1));
    CHECK(id == 400001 || id == 400002); return OB_SUCCESS;
  };
  ObSessionPrivInfo priv;
  priv.user_id_ = 123; priv.user_name_ = owner.get_user_name_str(); priv.host_name_ = owner.get_host_name_str();
  priv.user_priv_set_ = owner.get_priv_set();
  ObSEArray<uint64_t, 1> roles; ObSEArray<const ObCreateRoutineArg *, 1> args;
  ObPluginCatalog catalog; CHECK(catalog.init(&proxy) == OB_SUCCESS);
  ExtensionInstallSpec spec{1,100,123,"install_fixture","1.0","",{}, {}};
  uint64_t identity = 999; std::string error;
  int status = OB_SUCCESS;
  if (update) {
    ObSEArray<ExtensionRoutineUpdateOperation, 1> operations;
    auto updater = ObPLDDLService::make_routine_extension_updater(operations, priv, roles, view, ddl, transaction, &script);
    CHECK(updater);
    ExtensionUpdateRequest request{1,100,"install_fixture",91,"1.0","2.0",{}};
    if (fail_write == 0 && fail_resolve < 0 && end_status == OB_SUCCESS && !drift) {
      auto ordinary = priv;
      ordinary.user_priv_set_ = OB_PRIV_CREATE_ROUTINE;
      auto policy = ObPLDDLService::make_routine_extension_updater(
          operations, ordinary, roles, view, ddl, transaction, &script);
      CHECK(policy && policy->preflight(request, error) == OB_ERR_NO_PRIVILEGE);
      auto invoker = request;
      invoker.requires_superuser_ = false;
      CHECK(policy->preflight(invoker, error) == OB_SUCCESS);
      CHECK(rows.starts == 0 && rows.written.empty());
    }
    bool changed = false;
    status = catalog.update_extension(request, *updater, identity, changed, error, &transaction, 42);
    CHECK(changed == (status == OB_SUCCESS));
  } else {
    auto installer = ObPLDDLService::make_routine_extension_installer(args, priv, roles, view, ddl, transaction, &script);
    CHECK(installer);
    if (fail_write == 0 && fail_resolve < 0 && end_status == OB_SUCCESS) {
      auto ordinary = priv;
      ordinary.user_priv_set_ = OB_PRIV_CREATE_ROUTINE;
      auto policy = ObPLDDLService::make_routine_extension_installer(
          args, ordinary, roles, view, ddl, transaction, &script);
      CHECK(policy && policy->preflight(spec, error) == OB_ERR_NO_PRIVILEGE);
      auto invoker = spec;
      invoker.requires_superuser_ = false;
      CHECK(policy->preflight(invoker, error) == OB_SUCCESS);
      CHECK(rows.starts == 0 && rows.written.empty());
    }
    status = catalog.install_extension(spec, *installer, identity, error, &transaction, 42);
  }
  const bool failure = fail_write > 0 || fail_resolve >= 0 || drift;
  std::cout << "Root " << (update ? "update" : "install") << " fixture: fail_write=" << fail_write << " fail_resolve=" << fail_resolve
            << " status=" << status << " phase=" << script.phase << " error=" << error << std::endl;
  CHECK(status == (end_status != OB_SUCCESS ? OB_TRANS_UNKNOWN : failure ?
      (drift ? OB_STATE_NOT_MATCH : fail_resolve >= 0 ? OB_USER_NOT_EXIST : OB_TIMEOUT) : OB_SUCCESS));
  CHECK(!rows.active && rows.starts == 1 && rows.ends == 1 && rows.commits == std::vector<bool>{!failure});
  CHECK(identity == (failure || end_status != OB_SUCCESS ? 0 : 91));
  int members = 0;
  for (const auto &sql : rows.written) {
    if (sql.find("INSERT INTO __all_extension_member") == 0) {
      ++members; CHECK(script.resolves == script.statement_count());
    }
    if (sql.find("__all_extension_") != std::string::npos) CHECK(script.resolves == script.statement_count());
  }
  if (!failure) {
    CHECK(members == (mixed == 2 ? 1 : 2) && script.resolves == script.statement_count());
    CHECK(transaction.invalidations == (mixed == 2 ? 1 : 0));
    if (mixed == 2) {
      CHECK(!granted);
      const ObRoutineInfo *old = nullptr, *replacement = nullptr;
      CHECK(view.get_routine_info(400001, old) == OB_SUCCESS && !old);
      CHECK(view.get_routine_info(400002, replacement) == OB_SUCCESS && replacement);
      CHECK(replacement->get_routine_name() == ObString::make_string("install_value"));
    }
  }
  if (fail_resolve >= 0) CHECK(script.resolves == fail_resolve + 1 && members == 0);
  if (fail_write > 0) CHECK(rows.writes == fail_write); // No later write or replay.
  if (drift) CHECK(rows.writes == 2 && granted && members == 0); // Fence/detach only: do not rebase or overwrite ACL.
  return rows.writes;
}

inline void run()
{
  const int writes = run_case(-1);
  for (int fail = 1; fail <= writes; ++fail) run_case(fail);
  for (int fail = 0; fail < 4; ++fail) run_case(-1, fail);
  run_case(-1, -1, OB_TIMEOUT); // Unknown COMMIT: no rollback or replay.
  run_case(-1, 2, OB_TIMEOUT); // Unknown ROLLBACK: not reported as successful undo.
  std::cout << "PASS: real Root install adapter CREATE/GRANT/REVOKE/CREATE, private ACL visibility, two members, "
            << writes << " SQL failure positions, resolution failure and unknown end; controlled transport, not live rollback" << std::endl;
  const int update_writes = run_case(-1, -1, OB_SUCCESS, true);
  for (int fail = 1; fail <= update_writes; ++fail) run_case(fail, -1, OB_SUCCESS, true);
  for (int fail = 0; fail < 4; ++fail) run_case(-1, fail, OB_SUCCESS, true);
  run_case(-1, -1, OB_TIMEOUT, true);
  run_case(-1, 2, OB_TIMEOUT, true);
  std::cout << "PASS: real Root UPDATE adapter/coordinator CREATE/GRANT/REVOKE/CREATE, private admission without routine/ACL SQL, initial-view rewind, sequential execution, two CREATE-only members, "
            << update_writes << " SQL failure positions and unknown end; controlled transport, not live rollback" << std::endl;
  const int published_writes = run_case(-1, -1, OB_SUCCESS, true, true);
  for (int fail = 1; fail <= published_writes; ++fail) run_case(fail, -1, OB_SUCCESS, true, true);
  for (int fail = 0; fail < 3; ++fail) run_case(-1, fail, OB_SUCCESS, true, true);
  run_case(-1, -1, OB_SUCCESS, true, true, true);
  std::cout << "PASS: real Root UPDATE on a published native member, locked base ACL, GRANT/REVOKE followed by CREATE, unchanged member retained and "
            << published_writes << " SQL failure positions; controlled transport, not live rollback" << std::endl;
  for (const int mixed : {1, 2}) for (const bool published : {false, true}) {
    const int count = run_case(-1, -1, OB_SUCCESS, true, published, false, mixed);
    for (int fail = 1; fail <= count; ++fail) run_case(fail, -1, OB_SUCCESS, true, published, false, mixed);
    const int statements = (published ? 3 : 4) + (mixed == 1);
    for (int fail = 0; fail < statements; ++fail) run_case(-1, fail, OB_SUCCESS, true, published, false, mixed);
    run_case(-1, -1, OB_TIMEOUT, true, published, false, mixed);
    std::cout << "PASS: Root mixed UPDATE mode=" << mixed << " published=" << published << " writes=" << count
              << ": ALTER preserves private ACL/version; DROP clears ACL, defers invalidation and recreates a new member identity; controlled transport" << std::endl;
  }
}
} // namespace routine_extension_install_test
