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

#define USING_LOG_PREFIX RS

#include "ob_pl_ddl_service.h"
#include "rootserver/pl_ddl/routine_catalog_writer.h"
#include "sql/pl/pl_cache/ob_pl_cache_mgr.h"
#include "share/schema/routine_schema_overlay.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "rootserver/ob_dependency_ddl_helper.h"
#include "lib/utility/ob_smart_call.h"
#include "rootserver/ob_ddl_service.h"
#include "share/schema/ob_error_info.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_routine_info.h"
#include "share/schema/ob_package_info.h"
#include "share/schema/ob_trigger_info.h"
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
#include "share/plugin/extension_install.h"
#include "share/plugin/extension_routine_update.h"
#include "share/plugin/ob_plugin_sql_catalog.h"
#include <set>
#include <map>
#include <new>
#include <memory>
#endif

namespace oceanbase
{
using rootserver::ObDDLSQLTransaction;
using rootserver::ObDDLOperator;

namespace rootserver
{

// Reserved Extension identities must not inherit a historical name-keyed ACL,
// even if a prior ordinary DROP left one behind with automatic grants disabled.
// Reserved identities use this policy; ordinary PL DDL is unchanged. The host
// admission layer must hold the serial DDL lock before calling this helper.
static int clear_reserved_routine_privileges(const ObRoutineInfo &routine,
                                           ObSchemaGetterGuard &guard,
                                           ObPLDDLOperator &operation,
                                           ObMySQLTransaction &transaction)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema *database = nullptr;
  ObSEArray<const ObUserInfo *, 10> users;
  if (!transaction.is_started()) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(guard.get_database_schema(routine.get_database_id(), database))) {
  } else if (database == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(guard.get_user_infos_by_id(users))) {
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < users.count(); ++i) {
    if (users.at(i) == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObRoutinePrivSortKey key(users.at(i)->get_user_id(), database->get_database_name_str(),
                                    routine.get_routine_name(), routine.get_routine_type());
      ret = operation.revoke_routine(key, OB_PRIV_ROUTINE_ACC | OB_PRIV_GRANT, transaction,
                                     false, false, "", "", true /* transaction ACL, not final overlay */);
    }
  }
  return ret;
}

int RoutineCatalogWriter::begin()
{
  if (attempted_) return OB_INIT_TWICE;
  attempted_ = true;
  if (!transaction_.is_started()) return OB_STATE_NOT_MATCH;
  if (!guard_.is_inited()) return OB_NOT_INIT;
  if (service_.get_schema_service() == nullptr) return OB_ERR_UNEXPECTED;
  return OB_SUCCESS;
}

int RoutineCatalogWriter::create(ObRoutineInfo &routine_info, const ObRoutineInfo *old_routine_info,
    ObErrorInfo &error_info, ObIArray<ObDependencyInfo> &dep_infos, const ObString *ddl_stmt_str,
    RoutineIdReservation *reservation, RoutineVersionReservation *version_reservation)
{
  int ret = begin();
  if (ret != OB_SUCCESS) return ret;
  const bool replace = old_routine_info != nullptr;
  if ((reservation != nullptr && replace) ||
      ((reservation != nullptr || version_reservation != nullptr) && !transaction_privileges_))
    return OB_INVALID_ARGUMENT;
  auto &schema_guard = guard_;
  auto &trans = transaction_;
  ObPLDDLOperator pl_operator(service_, proxy_);
  if (OB_SUCC(ret)) {
    if (replace) {
      if (OB_FAIL(pl_operator.replace_routine(routine_info,
                                               old_routine_info,
                                               trans,
                                               error_info,
                                               dep_infos,
                                               ddl_stmt_str,
                                               version_reservation))) {
      }
    } else {
      if (OB_FAIL(pl_operator.create_routine(routine_info,
                                             trans,
                                             error_info,
                                             dep_infos,
                                             ddl_stmt_str,
                                             reservation,
                                             version_reservation))) {
      }
    }
  }
  if (OB_SUCC(ret) && !replace && version_reservation != nullptr) {
    ret = clear_reserved_routine_privileges(routine_info, schema_guard, pl_operator, trans);
  }
  if (OB_FAIL(ret)) {
  } else if (replace) {
  } else {
    const ObSysVarSchema *sys_var = NULL;
    ObMalloc alloc(ObModIds::OB_TEMP_VARIABLES);
    ObObj val;
    if (OB_FAIL(schema_guard.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, sys_var))) {
    } else if (OB_ISNULL(sys_var)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys variable schema is null", KR(ret));
    } else if (OB_FAIL(sys_var->get_value(&alloc, NULL, val))) {
    } else {
      bool grant_priv = val.get_bool();
      if (grant_priv) {
        int64_t db_id = routine_info.get_database_id();
        const ObDatabaseSchema* database_schema = NULL;
        if (OB_FAIL(schema_guard.get_database_schema( db_id, database_schema))) {
        } else if (OB_ISNULL(database_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("database schema should not be null", K(ret));
        } else {
          ObRoutinePrivSortKey routine_key(routine_info.get_owner_id(),
                                            database_schema->get_database_name_str(),
                                            routine_info.get_routine_name(), routine_info.is_procedure() ?
                                            ObRoutineType::ROUTINE_PROCEDURE_TYPE : ObRoutineType::ROUTINE_FUNCTION_TYPE);
          ObPrivSet priv_set = (OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE);
          int64_t option = 0;
          const bool gen_ddl_stmt = false;
          const ObUserInfo *user_info = NULL;
          if (OB_FAIL(schema_guard.get_user_info(routine_info.get_owner_id(),
                                                 user_info))) {
          } else if (OB_ISNULL(user_info)) {
            ret = OB_ERR_PARALLEL_DDL_CONFLICT;
            LOG_WARN("user info is null, may be parallel ddl conflict", K(ret));
          } else if (OB_FAIL(pl_operator.grant_routine(routine_key,
                                                        priv_set,
                                                        trans,
                                                        option,
                                                        gen_ddl_stmt,
                                                        user_info->get_user_name_str(),
                                                        user_info->get_host_name_str(),
                                                        transaction_privileges_))) {
          }
        }
      }
    }
  }
  return ret;
}

int RoutineCatalogWriter::alter(const ObRoutineInfo &routine_info, ObErrorInfo &error_info,
    const ObString *ddl_stmt_str)
{
  int ret = begin();
  if (ret != OB_SUCCESS) return ret;
  auto &trans = transaction_;
  ObPLDDLOperator pl_operator(service_, proxy_);
  if (OB_FAIL(ObDependencyDDLHelper::modify_dep_obj_status(trans,
                                                              routine_info.get_routine_id(),
                                                              pl_operator,
                                                              service_))) {
  } else if (OB_FAIL(pl_operator.alter_routine(routine_info, trans, error_info, ddl_stmt_str))) {
  }
  return ret;
}

int RoutineCatalogWriter::drop(const ObRoutineInfo &routine_info, ObErrorInfo &error_info,
    const ObString *ddl_stmt_str, IRoutineCacheInvalidation &invalidation,
    RoutineVersionReservation *version_reservation)
{
  int ret = begin();
  if (ret != OB_SUCCESS) return ret;
  if (version_reservation != nullptr && !transaction_privileges_) return OB_INVALID_ARGUMENT;
  auto &schema_guard = guard_;
  auto &trans = transaction_;
  ObPLDDLOperator pl_operator(service_, proxy_);
  if (OB_FAIL(ObDependencyDDLHelper::modify_dep_obj_status(trans,
                                                             routine_info.get_routine_id(),
                                                             pl_operator,
                                                             service_))) {
  } else if (OB_FAIL(pl_operator.drop_routine(routine_info, trans, error_info, ddl_stmt_str,
                                            version_reservation, &invalidation))) {
  } else {
    const ObSysVarSchema *sys_var = NULL;
    ObMalloc alloc(ObModIds::OB_TEMP_VARIABLES);
    ObObj val;
    if (OB_FAIL(schema_guard.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, sys_var))) {
    } else if (OB_ISNULL(sys_var)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sys variable schema is null", KR(ret));
    } else if (OB_FAIL(sys_var->get_value(&alloc, NULL, val))) {
    } else if (version_reservation != nullptr) {
      // Extension updates replace object identities, not names. Always clear
      // old named grants on their reserved DROP, including when automatic
      // grants were disabled since the old object was created. Ordinary DROP
      // (without a host reservation) retains its existing policy.
      ret = clear_reserved_routine_privileges(routine_info, schema_guard, pl_operator, trans);
    } else if (val.get_bool()) {
      const int64_t db_id = routine_info.get_database_id();
      const ObDatabaseSchema *database_schema = NULL;
      ObSEArray<const ObUserInfo *, 10> user_infos;
      if (OB_FAIL(schema_guard.get_database_schema(db_id, database_schema))) {
      } else if (OB_ISNULL(database_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("database schema is null", K(ret));
      } else if (OB_FAIL(schema_guard.get_user_infos_by_id(user_infos))) {
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < user_infos.count(); ++i) {
        const ObUserInfo *user_info = user_infos.at(i);
        if (OB_ISNULL(user_info)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null user info", K(ret));
        } else {
          ObRoutinePrivSortKey routine_key(
              user_info->get_user_id(),
              database_schema->get_database_name_str(),
              routine_info.get_routine_name(),
              routine_info.is_procedure()
                  ? ObRoutineType::ROUTINE_PROCEDURE_TYPE
                  : ObRoutineType::ROUTINE_FUNCTION_TYPE);
          const ObPrivSet priv_set = OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE;
          bool gen_ddl_stmt = false;
          if (OB_FAIL(pl_operator.revoke_routine(
                  routine_key, priv_set, trans, false, gen_ddl_stmt, "", "",
                  transaction_privileges_))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPLDDLService::install_routine_extension(
    const share::plugin::ExtensionInstallSpec &spec,
    const obcall::ObCreateRoutineArg &arg,
    const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles,
    share::plugin::IExtensionCatalogInstaller &catalog,
    ObDDLService &ddl_service,
    uint64_t &extension_id, int &publication_status, std::string &error)
{
  extension_id = 0;
  publication_status = OB_NOT_INIT;
  error.clear();
  ObSEArray<const ObCreateRoutineArg *, 1> args;
  int ret = args.push_back(&arg);
  if (OB_SUCC(ret)) {
    ret = install_routines_extension(spec, args, session_priv, enabled_roles,
                                    catalog, ddl_service, extension_id, publication_status, error);
  }
  return ret;
}

int ObPLDDLService::install_routines_extension(
    const share::plugin::ExtensionInstallSpec &spec,
    const ObIArray<const ObCreateRoutineArg *> &args,
    const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles,
    share::plugin::IExtensionCatalogInstaller &catalog,
    ObDDLService &ddl_service,
    uint64_t &extension_id, int &publication_status, std::string &error,
    share::plugin::IExtensionRoutineScript *script)
{
  extension_id = 0;
  publication_status = OB_NOT_INIT; // not attempted unless installation commits
  error.clear();
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(spec, args, session_priv, enabled_roles, catalog, ddl_service, script);
  return OB_NOT_SUPPORTED;
#else
  using namespace share::plugin;
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  int64_t schema_version = 0;
  const int64_t count = script ? script->statement_count() : args.count();
  if (count < 0 || (count == 0 && (script == nullptr || !script->has_builder())) ||
      count > 4096 || (script != nullptr && !args.empty())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ddl_service.check_inner_stat())) {
  } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
  } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
  } else {
    // Non-parallel DDL owns the schema epoch/ordering locks and end marker.
    // An ordinary ObMySQLTransaction would omit the schema watermark at commit.
    ObDDLSQLTransaction transaction(&ddl_service.get_schema_service());
    class RoutineInstaller final : public IExtensionSchemaInstaller
    {
    public:
      RoutineInstaller(const ObIArray<const ObCreateRoutineArg *> &args, const ObSessionPrivInfo &priv,
                       const ObIArray<uint64_t> &roles, ObSchemaGetterGuard &guard,
                       ObDDLService &ddl, ObDDLSQLTransaction &transaction, IExtensionRoutineScript *script,
                       int64_t count)
          : args_(args), priv_(priv), roles_(roles), guard_(guard), ddl_(ddl), transaction_(transaction),
            script_(script), count_(count) {}

      int preflight(const ExtensionInstallSpec &spec, std::string &error) override
      {
        int ret = OB_SUCCESS;
        if (consumed_) return OB_STATE_NOT_MATCH;
        if (spec.tenant_id_ != 1) {
          ret = OB_NOT_SUPPORTED;
          error = "routine extension adapter requires the local tenant";
        } else if (!priv_.is_valid() || spec.owner_id_ != priv_.user_id_) {
          ret = OB_ERR_NO_PRIVILEGE;
        } else if (script_ != nullptr) {
          if (!args_.empty() || count_ != script_->statement_count()) ret = OB_STATE_NOT_MATCH;
          else ret = script_->preflight_install(spec, error);
        }
        // Use the same comparison as ObRoutineNameHashWrapper; bytewise or
        // ASCII-only folding would miss conflicting names in this transaction.
        // FUNCTION and PROCEDURE have distinct kernel namespaces.
        struct NameLess {
          bool operator()(const ObCreateRoutineArg *lhs, const ObCreateRoutineArg *rhs) const
          {
            const auto &left = lhs->routine_info_;
            const auto &right = rhs->routine_info_;
            ObSchemaNameComparator comparator;
            return left.get_routine_type() != right.get_routine_type()
                ? left.get_routine_type() < right.get_routine_type()
                : comparator.compare(left.get_routine_name(), right.get_routine_name()) < 0;
          }
        };
        std::set<const ObCreateRoutineArg *, NameLess> names;
        for (int64_t i = 0; OB_SUCC(ret) && i < args_.count(); ++i) {
          if (nullptr == args_.at(i) || !args_.at(i)->is_valid()) {
            ret = OB_INVALID_ARGUMENT;
          } else if (OB_FAIL(check_routine(spec, *args_.at(i), error))) {
          } else if (!names.insert(args_.at(i)).second) {
            ret = OB_ERR_SP_ALREADY_EXISTS;
            error = "duplicate routine name inside extension installation";
          }
        }
        return ret;
      }

      int check_routine(const ExtensionInstallSpec &spec, const ObCreateRoutineArg &arg,
                        std::string &error)
      {
        int ret = OB_SUCCESS;
        const ObDatabaseSchema *database = nullptr;
        const auto type = arg.routine_info_.get_routine_type();
        if (arg.is_or_replace_ || arg.is_need_alter_ || arg.with_if_not_exist_ ||
            (type != ROUTINE_FUNCTION_TYPE && type != ROUTINE_PROCEDURE_TYPE)) {
          ret = OB_NOT_SUPPORTED;
          error = "extension routine installation requires new functions or procedures";
        } else if (arg.error_info_.get_error_status() == ERROR_STATUS_HAS_ERROR) {
          ret = OB_ERR_RESOLVE_SQL;
          error = "extension routine body contains unresolved compilation errors";
        } else if (arg.routine_info_.get_owner_id() != priv_.user_id_) {
          ret = OB_ERR_NO_PRIVILEGE;
        } else if (OB_FAIL(guard_.get_database_schema(arg.db_name_, database))) {
        } else if (nullptr == database || database->is_in_recyclebin() ||
                   database->get_database_id() != spec.database_id_) {
          ret = OB_ERR_BAD_DATABASE;
        } else if (OB_FAIL(ddl_.check_parallel_ddl_conflict(guard_, arg))) {
        } else {
          ObArenaAllocator allocator;
          ObStmtNeedPrivs privileges(allocator);
          ObNeedPriv need;
          need.db_ = arg.db_name_;
          need.table_ = arg.routine_info_.get_routine_name();
          need.obj_type_ = type == ROUTINE_PROCEDURE_TYPE ? ObObjectType::PROCEDURE : ObObjectType::FUNCTION;
          need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
          need.priv_set_ = OB_PRIV_CREATE_ROUTINE;
          if (OB_FAIL(privileges.need_privs_.reserve(1))) {
          } else if (OB_FAIL(privileges.need_privs_.push_back(need))) {
          } else if (OB_FAIL(guard_.check_priv(priv_, roles_, privileges))) {
          } else if (!(priv_.user_priv_set_ & OB_PRIV_SUPER) && OB_FAIL(guard_.verify_read_only(privileges))) {
          }
          bool exists = false;
          if (OB_SUCC(ret)) {
            ret = type == ROUTINE_PROCEDURE_TYPE
                ? guard_.check_standalone_procedure_exist(spec.database_id_, need.table_, exists)
                : guard_.check_standalone_function_exist(spec.database_id_, need.table_, exists);
            if (OB_SUCC(ret) && exists) {
              ret = OB_ERR_SP_ALREADY_EXISTS;
              error = "extension routine already exists in target database";
            }
          }
        }
        return ret;
      }

      int apply(ObPluginSqlConnection &connection, const ExtensionInstallSpec &spec,
                std::vector<ExtensionMemberIdentity> &members, std::string &error) override
      {
        int ret = OB_SUCCESS;
        members.clear();
        if (!transaction_.is_started() || !connection.is_in_transaction()) {
          ret = OB_STATE_NOT_MATCH;
        } else if (OB_FAIL(preflight(spec, error))) {
        } else {
          consumed_ = true;
          // Reserve/stage all objects before writing any schema. The Rust
          // coordinator still owns this one DDL transaction and its rollback.
          const ObSysVarSchema *variable = nullptr;
          ObMalloc allocator(ObModIds::OB_TEMP_VARIABLES);
          ObObj value;
          if (OB_FAIL(guard_.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable))) {}
          else if (variable == nullptr) ret = OB_ERR_UNEXPECTED;
          else if (OB_FAIL(variable->get_value(&allocator, nullptr, value))) {}
          else automatic_privileges_ = value.get_bool();
          if (OB_SUCC(ret)) {
            privileges_ = std::make_shared<RoutinePrivilegeOverlay>(spec.database_id_, priv_.user_id_);
            overlay_ = std::make_shared<RoutineSchemaOverlay>(privileges_);
            ret = guard_.attach_routine_overlay(overlay_);
          }
          const auto admit_and_stage = [&](const ExtensionRoutineUpdateOperation &op) {
            if (!op.has_valid_shape() || op.kind_ != ExtensionRoutineUpdateOperation::Kind::CREATE)
              return OB_NOT_SUPPORTED;
            return stage(spec, *op.create_arg_, error);
          };
          if (OB_SUCC(ret) && script_ != nullptr) {
            ret = resolve_extension_routine_sequence(*script_, count_, guard_, admit_and_stage, error);
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < args_.count(); ++i) ret = stage(spec, *args_.at(i), error);
          }
          if (OB_SUCC(ret) && script_ != nullptr && script_->has_builder()) {
            ret = script_->build(guard_, [&](const ExtensionRoutineUpdateOperation &op, uint64_t &object_id) {
              object_id = 0;
              if (!transaction_.is_started() || !connection.is_in_transaction()) return OB_STATE_NOT_MATCH;
              if (nodes_.size() >= 4096) return OB_SIZE_OVERFLOW;
              const int code = admit_and_stage(op);
              if (code == OB_SUCCESS) object_id = nodes_.back()->routine_.get_routine_id();
              return code;
            }, error);
          }
          if (OB_SUCC(ret) && nodes_.empty()) ret = OB_INVALID_ARGUMENT;
          if (OB_SUCC(ret)) members.reserve(nodes_.size());
          for (const auto &node : nodes_) {
            if (OB_FAIL(ret)) break;
            ObErrorInfo errors = node->arg_->error_info_;
            ret = ObPLDDLService::create_routine(node->routine_, nullptr, false, errors,
                node->dependencies_, &node->arg_->ddl_stmt_str_, guard_, ddl_, &transaction_,
                &node->identity_, &node->version_);
            if (OB_SUCC(ret)) members.push_back({static_cast<uint32_t>(ROUTINE_SCHEMA), node->routine_.get_routine_id()});
          }
        }
        if (OB_FAIL(ret)) members.clear();
        return ret;
      }
    private:
      struct Node {
        const ObCreateRoutineArg *arg_ = nullptr;
        ObRoutineInfo routine_;
        ObSArray<ObDependencyInfo> dependencies_;
        RoutineIdReservation identity_;
        RoutineVersionReservation version_;
      };
      int stage(const ExtensionInstallSpec &spec, const ObCreateRoutineArg &arg, std::string &error)
      {
        int ret = OB_SUCCESS;
        if (!arg.is_valid()) return OB_INVALID_ARGUMENT;
        if (OB_FAIL(check_routine(spec, arg, error))) return ret;
        auto node = std::make_unique<Node>();
        node->arg_ = &arg;
        if (OB_FAIL(node->routine_.assign(arg.routine_info_))) return ret;
        if (OB_FAIL(node->dependencies_.assign(arg.dependency_infos_))) return ret;
        node->routine_.set_database_id(spec.database_id_);
        node->routine_.set_routine_id(OB_INVALID_ID);
        auto *schema = ddl_.get_schema_service().get_schema_service();
        if (schema == nullptr) return OB_ERR_UNEXPECTED;
        if (OB_FAIL(RoutineIdReservation::reserve(*schema, node->routine_, node->identity_))) return ret;
        node->routine_.set_routine_id(node->identity_.id());
        if (OB_FAIL(RoutineVersionReservation::reserve(ddl_.get_schema_service(), transaction_,
            node->routine_, nullptr, node->version_))) return ret;
        node->routine_.set_schema_version(node->version_.version());
        auto &parameters = node->routine_.get_routine_params();
        for (int64_t i = 0; i < parameters.count(); ++i) {
          if (parameters.at(i) == nullptr) return OB_ERR_UNEXPECTED;
          parameters.at(i)->set_routine_id(node->routine_.get_routine_id());
          parameters.at(i)->set_schema_version(node->routine_.get_schema_version());
        }
        RoutineCatalogSavepoint view_savepoint(overlay_, privileges_);
        if (!view_savepoint.valid()) return OB_STATE_NOT_MATCH;
        if (OB_FAIL(overlay_->stage(node->routine_))) return ret;
        if (OB_FAIL(privileges_->record_create(node->routine_, automatic_privileges_))) return ret;
        nodes_.push_back(std::move(node));
        view_savepoint.release();
        return OB_SUCCESS;
      }
      const ObIArray<const ObCreateRoutineArg *> &args_;
      const ObSessionPrivInfo &priv_;
      const ObIArray<uint64_t> &roles_;
      ObSchemaGetterGuard &guard_;
      ObDDLService &ddl_;
      ObDDLSQLTransaction &transaction_;
      IExtensionRoutineScript *script_;
      const int64_t count_;
      bool consumed_ = false;
      bool automatic_privileges_ = false;
      std::shared_ptr<RoutinePrivilegeOverlay> privileges_;
      std::shared_ptr<RoutineSchemaOverlay> overlay_;
      std::vector<std::unique_ptr<Node>> nodes_;
    } installer(args, session_priv, enabled_roles, guard, ddl_service, transaction, script, count);

    ret = catalog.install_extension(spec, installer, extension_id, error, &transaction, schema_version);
    if (OB_SUCC(ret)) {
      // The transaction has committed. Never convert publication failure into
      // a retryable installation error, or claim that schema writes rolled back.
      try {
        publication_status = ddl_service.publish_schema();
      } catch (const std::bad_alloc &) {
        publication_status = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) {
        publication_status = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
#endif
}

int ObPLDDLService::drop_routines_extension(
    const share::plugin::ExtensionDropRequest &request, const ObSessionPrivInfo &session_priv,
    const ObIArray<uint64_t> &enabled_roles, share::plugin::IExtensionCatalogDropper &catalog,
    ObDDLService &ddl_service, uint64_t &dropped_extension_id, int &publication_status, std::string &error)
{
  dropped_extension_id = 0;
  publication_status = OB_NOT_INIT;
  error.clear();
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(request, session_priv, enabled_roles, catalog, ddl_service);
  return OB_NOT_SUPPORTED;
#else
  using namespace share::plugin;
  int ret = OB_SUCCESS;
  try {
    ObSchemaGetterGuard guard;
    int64_t schema_version = 0;
    if (OB_FAIL(ddl_service.check_inner_stat())) {
    } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
    } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
    } else {
      ObDDLSQLTransaction transaction(&ddl_service.get_schema_service());
      class RoutineDropper final : public IExtensionSchemaDropper {
      public:
        RoutineDropper(const ObSessionPrivInfo &priv, const ObIArray<uint64_t> &roles,
                       ObSchemaGetterGuard &guard, ObDDLService &ddl, ObDDLSQLTransaction &transaction)
          : priv_(priv), roles_(roles), guard_(guard), ddl_(ddl), transaction_(transaction) {}

        int preflight(const ExtensionDropRequest &request, std::string &error) override {
          if (!priv_.is_valid()) return OB_ERR_NO_PRIVILEGE;
          if (request.tenant_id_ != 1 || request.cascade_) {
            error = "routine Extension removal currently supports RESTRICT, not CASCADE";
            return OB_NOT_SUPPORTED;
          }
          return OB_SUCCESS;
        }

        int admit(ObPluginSqlConnection &connection, const ExtensionDropRequest &request,
                  const ExtensionDropSnapshot &snapshot, std::string &error) override {
          int ret = preflight(request, error);
          const auto &spec = snapshot.installed_;
          routines_.clear();
          const ObDatabaseSchema *database = nullptr;
          const bool super = (priv_.user_priv_set_ & OB_PRIV_SUPER) != 0;
          if (OB_FAIL(ret)) {
          } else if (!transaction_.is_started() || !connection.is_in_transaction()) {
            ret = OB_STATE_NOT_MATCH;
          } else if (priv_.user_id_ != spec.owner_id_ && !super) {
            ret = OB_ERR_NO_PRIVILEGE;
          } else if (OB_FAIL(guard_.get_database_schema(spec.database_id_, database))) {
          } else if (nullptr == database || database->is_in_recyclebin()) {
            ret = OB_ERR_BAD_DATABASE;
          } else if (!super && database->is_read_only()) {
            // Apply even when the extension currently has no members.
            ret = OB_ERR_DB_READ_ONLY;
            LOG_USER_ERROR(OB_ERR_DB_READ_ONLY, database->get_database_name_str().length(),
                           database->get_database_name_str().ptr());
          }
          std::map<uint64_t, ObObjectType> members;
          for (const auto &member : spec.members_) {
            if (OB_FAIL(ret)) break;
            const ObRoutineInfo *routine = nullptr;
            if (member.object_class_ != static_cast<uint32_t>(ROUTINE_SCHEMA)) {
              ret = OB_NOT_SUPPORTED;
              error = "Extension contains non-routine members; no objects have been detached";
            } else if (OB_FAIL(guard_.get_routine_info(member.object_id_, routine))) {
            } else if (nullptr == routine || routine->get_database_id() != spec.database_id_ ||
                       (routine->get_routine_type() != ROUTINE_FUNCTION_TYPE &&
                        routine->get_routine_type() != ROUTINE_PROCEDURE_TYPE)) {
              ret = OB_STATE_NOT_MATCH;
            } else {
              ObArenaAllocator allocator;
              ObStmtNeedPrivs privileges(allocator);
              ObNeedPriv need;
              need.db_ = database->get_database_name_str();
              need.table_ = routine->get_routine_name();
              need.obj_type_ = routine->get_object_type();
              need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
              need.priv_set_ = OB_PRIV_ALTER_ROUTINE;
              if (OB_FAIL(privileges.need_privs_.reserve(1))) {
              } else if (OB_FAIL(privileges.need_privs_.push_back(need))) {
              } else if (OB_FAIL(guard_.check_priv(priv_, roles_, privileges))) {
              } else if (!super && OB_FAIL(guard_.verify_read_only(privileges))) {
              } else {
                routines_.push_back(routine);
                members.emplace(member.object_id_, routine->get_object_type());
              }
            }
          }
          // Check incoming, typed schema dependencies before detaching anything.
          // Internal references between members do not block dropping the group.
          // Missing/corrupt/failed reads are errors, never 'no dependencies'.
          for (const auto *routine : routines_) {
            if (OB_FAIL(ret)) break;
            ret = connection.query(
                "SELECT dep_obj_id,dep_obj_type FROM __all_dependency WHERE ref_obj_id=? "
                "AND ref_obj_type=? ORDER BY dep_obj_id,dep_obj_type FOR UPDATE",
                [&](ObPluginSqlBinder &binder) {
                  int code = binder.bind_int64(routine->get_routine_id());
                  if (OB_SUCCESS == code) code = binder.bind_int64(static_cast<int64_t>(routine->get_object_type()));
                  return code;
                },
                [&](ObPluginSqlRowReader &reader) {
                  int64_t id = 0, type = 0;
                  int code = reader.read_int64(0, id);
                  if (OB_SUCCESS == code) code = reader.read_int64(1, type);
                  if (OB_SUCCESS != code) return code;
                  if (id <= 0 || type <= static_cast<int64_t>(ObObjectType::INVALID) ||
                      type >= static_cast<int64_t>(ObObjectType::MAX_TYPE)) return OB_INVALID_DATA;
                  const auto member = members.find(static_cast<uint64_t>(id));
                  if (member == members.end() || static_cast<int64_t>(member->second) != type) {
                    error = "Extension member has an external schema dependency; RESTRICT refuses removal";
                    return OB_OP_NOT_ALLOW;
                  }
                  return OB_SUCCESS;
                });
          }
          if (OB_FAIL(ret)) routines_.clear();
          return ret;
        }

        int apply(ObPluginSqlConnection &connection, const ExtensionDropSnapshot &snapshot,
                  std::string &error) override {
          UNUSED(error);
          if (!transaction_.is_started() || !connection.is_in_transaction() ||
              routines_.size() != snapshot.installed_.members_.size()) return OB_STATE_NOT_MATCH;
          int ret = OB_SUCCESS;
          for (const auto *routine : routines_) {
            ObErrorInfo errors;
            if (OB_FAIL(ObPLDDLService::drop_routine(*routine, errors, nullptr, guard_, ddl_, &transaction_))) break;
          }
          return ret;
        }
      private:
        const ObSessionPrivInfo &priv_;
        const ObIArray<uint64_t> &roles_;
        ObSchemaGetterGuard &guard_;
        ObDDLService &ddl_;
        ObDDLSQLTransaction &transaction_;
        std::vector<const ObRoutineInfo *> routines_;
      } dropper(session_priv, enabled_roles, guard, ddl_service, transaction);
      ret = catalog.drop_extension(request, dropper, dropped_extension_id, error, &transaction, schema_version);
      if (OB_SUCC(ret)) {
        try { publication_status = ddl_service.publish_schema(); }
        catch (const std::bad_alloc &) { publication_status = OB_ALLOCATE_MEMORY_FAILED; }
        catch (...) { publication_status = OB_ERR_UNEXPECTED; }
      }
    }
  } catch (const std::bad_alloc &) {
    if (dropped_extension_id != 0) publication_status = OB_ALLOCATE_MEMORY_FAILED;
    else ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (dropped_extension_id != 0) publication_status = OB_ERR_UNEXPECTED;
    else ret = OB_ERR_UNEXPECTED;
  }
  return ret;
#endif
}

int ObPLDDLService::update_routines_extension(
    const share::plugin::ExtensionUpdateRequest &request,
    const ObIArray<share::plugin::ExtensionRoutineUpdateOperation> &operations,
    const ObSessionPrivInfo &session_priv, const ObIArray<uint64_t> &enabled_roles,
    share::plugin::IExtensionCatalogUpdater &catalog, ObDDLService &ddl_service,
    uint64_t &extension_id, bool &changed, int &publication_status, std::string &error,
    share::plugin::IExtensionRoutineScript *script)
{
  extension_id = 0;
  changed = false;
  publication_status = OB_NOT_INIT;
  error.clear();
#if !defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  UNUSEDx(request, operations, session_priv, enabled_roles, catalog, ddl_service, script);
  return OB_NOT_SUPPORTED;
#else
  using namespace share::plugin;
  int ret = OB_SUCCESS;
  try {
    ObSchemaGetterGuard guard;
    int64_t schema_version = 0;
    if (OB_FAIL(ddl_service.check_inner_stat())) {
    } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(guard))) {
    } else if (OB_FAIL(guard.get_schema_version(schema_version))) {
    } else {
      ObDDLSQLTransaction transaction(&ddl_service.get_schema_service());
      class RoutineUpdater final : public IExtensionSchemaUpdater {
        using Operation = ExtensionRoutineUpdateOperation;
        using Kind = Operation::Kind;
        using Key = std::pair<ObRoutineType, std::string>;
        struct NameLess {
          bool operator()(const Key &a, const Key &b) const {
            ObSchemaNameComparator comparator;
            return a.first != b.first ? a.first < b.first : comparator.compare(
                ObString(a.second.size(), a.second.data()), ObString(b.second.size(), b.second.data())) < 0;
          }
        };
        struct Node {
          ObRoutineInfo routine_;
          RoutineIdReservation reservation_;
          RoutineVersionReservation version_reservation_;
          ObSArray<ObDependencyInfo> dependencies_;
          bool dependencies_loaded_ = false;
          bool member_ = false;
          bool published_ = false;
          bool altered_ = false;
        };
        struct Step {
          const Operation *operation_; Node *before_; Node *after_;
          std::unique_ptr<RoutineVersionReservation> deletion_;
        };
      public:
        RoutineUpdater(const ObIArray<Operation> &operations, const ObSessionPrivInfo &priv,
                       const ObIArray<uint64_t> &roles, ObSchemaGetterGuard &guard,
                       ObDDLService &ddl, ObDDLSQLTransaction &transaction, IExtensionRoutineScript *script)
          : operations_(operations), priv_(priv), roles_(roles), guard_(guard), ddl_(ddl), transaction_(transaction),
            script_(script), count_(script ? script->statement_count() : operations.count()) {}

        int preflight(const ExtensionUpdateRequest &request, std::string &error) override {
          if (!priv_.is_valid()) return OB_ERR_NO_PRIVILEGE;
          if (request.tenant_id_ != 1) return OB_NOT_SUPPORTED;
          if (count_ < 0 || count_ > 4096) return OB_SIZE_OVERFLOW;
          if (script_ != nullptr) {
            if (!operations_.empty() || count_ != script_->statement_count()) return OB_INVALID_ARGUMENT;
            const int ret = script_->preflight(request, error);
            if (ret != OB_SUCCESS) return ret;
          }
          if (request.from_version_ == request.to_version_ && count_ != 0) {
            error = "same-version update must not discard a nonempty DDL plan";
            return OB_INVALID_ARGUMENT;
          }
          for (int64_t i = 0; i < operations_.count(); ++i) {
            const auto &op = operations_.at(i);
            if (!op.has_valid_shape()) return OB_INVALID_ARGUMENT;
            if (op.kind_ == Kind::DROP) {
              if (!op.drop_arg_->is_valid()) return OB_INVALID_ARGUMENT;
            } else if (!op.create_arg_->is_valid()) {
              return OB_INVALID_ARGUMENT;
            } else if (op.create_arg_->is_or_replace_ || op.create_arg_->with_if_not_exist_ ||
                       (op.kind_ == Kind::CREATE && op.create_arg_->is_need_alter_) ||
                       (op.kind_ == Kind::ALTER && !op.create_arg_->is_need_alter_)) {
              error = "routine update requires explicit CREATE, DROP or resolved MySQL ALTER";
              return OB_NOT_SUPPORTED;
            }
          }
          return OB_SUCCESS;
        }

        int admit(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                  const ExtensionUpdateSnapshot &snapshot, std::string &error) override {
          admitted_ = false;
          active_.clear(); steps_.clear(); nodes_.clear();
          int ret = preflight(request, error);
          const auto &spec = snapshot.installed_;
          const bool super = (priv_.user_priv_set_ & OB_PRIV_SUPER) != 0;
          if (OB_FAIL(ret)) {
          } else if (!transaction_.is_started() || !connection.is_in_transaction()) {
            ret = OB_STATE_NOT_MATCH;
          } else if (snapshot.extension_id_ != request.expected_extension_id_ ||
                     spec.database_id_ != request.database_id_ || spec.version_ != request.from_version_) {
            ret = OB_STATE_NOT_MATCH;
          } else if (priv_.user_id_ != spec.owner_id_ && !super) {
            ret = OB_ERR_NO_PRIVILEGE;
          } else if (OB_FAIL(guard_.get_database_schema(spec.database_id_, database_))) {
          } else if (database_ == nullptr || database_->is_in_recyclebin()) {
            ret = OB_ERR_BAD_DATABASE;
          } else if (!super && database_->is_read_only()) {
            ret = OB_ERR_DB_READ_ONLY;
          }
          if (OB_SUCC(ret) && count_ != 0) {
            const ObSysVarSchema *variable = nullptr;
            ObMalloc allocator(ObModIds::OB_TEMP_VARIABLES);
            ObObj value;
            if (OB_FAIL(guard_.get_system_variable(SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable))) {}
            else if (variable == nullptr) ret = OB_ERR_UNEXPECTED;
            else if (OB_FAIL(variable->get_value(&allocator, nullptr, value))) {}
            else automatic_privileges_ = value.get_bool();
          }
          // Seed ALL members, including those untouched by this script. Only
          // CREATE produces new membership; modifying an ordinary external
          // routine does not silently adopt it into the Extension.
          for (const auto &member : spec.members_) {
            if (OB_FAIL(ret)) break;
            const ObRoutineInfo *routine = nullptr;
            Node *node = nullptr;
            if (member.object_class_ != static_cast<uint32_t>(ROUTINE_SCHEMA)) ret = OB_NOT_SUPPORTED;
            else if (OB_FAIL(guard_.get_routine_info(member.object_id_, routine))) {}
            else if (routine == nullptr || routine->get_database_id() != spec.database_id_ ||
                     !standalone(routine->get_routine_type())) ret = OB_STATE_NOT_MATCH;
            else if (OB_FAIL(copy_node(*routine, true, true, node))) {}
            else if (!active_.emplace(key(*routine), node).second) ret = OB_INVALID_DATA;
          }
          if (OB_SUCC(ret)) {
            privileges_ = std::make_shared<RoutinePrivilegeOverlay>(spec.database_id_, priv_.user_id_);
            overlay_ = std::make_shared<RoutineSchemaOverlay>(privileges_);
            ret = guard_.attach_routine_overlay(overlay_);
          }
          const auto admit_and_stage = [&](const Operation &op) {
            RoutineCatalogSavepoint view_savepoint(overlay_, privileges_);
            if (!view_savepoint.valid()) return OB_STATE_NOT_MATCH;
            int code = admit_operation(connection, op, error);
            if (code == OB_SUCCESS) {
              const auto &step = steps_.back();
              if (step.after_ != nullptr) code = overlay_->stage(step.after_->routine_);
              else if (step.before_ != nullptr) code = overlay_->erase(
                  step.before_->routine_.get_database_id(), step.before_->routine_.get_routine_name(),
                  step.before_->routine_.get_routine_type(), step.before_->routine_.get_routine_id());
              if (code == OB_SUCCESS && op.kind_ == Kind::CREATE && step.after_ != nullptr)
                code = privileges_->record_create(step.after_->routine_, automatic_privileges_);
              else if (code == OB_SUCCESS && op.kind_ == Kind::DROP && step.before_ != nullptr)
                code = privileges_->record_drop(step.before_->routine_);
            }
            if (code == OB_SUCCESS) view_savepoint.release();
            return code;
          };
          if (OB_SUCC(ret) && script_ != nullptr) {
            ret = resolve_extension_routine_sequence(*script_, count_, guard_, admit_and_stage, error);
          } else {
            for (int64_t i = 0; OB_SUCC(ret) && i < operations_.count(); ++i)
              ret = admit_and_stage(operations_.at(i));
          }
          size_t final_member_count = 0;
          for (const auto &entry : active_) {
            if (entry.second && entry.second->member_) ++final_member_count;
          }
          if (OB_SUCC(ret) && final_member_count > 4096) ret = OB_SIZE_OVERFLOW;
          // Validate incoming dependencies of the original objects being
          // removed. A same-name replacement has a NEW ID, not a repair of an
          // existing dependency. Removal of the dependent in this plan is OK.
          std::map<uint64_t, ObObjectType> removed;
          for (const auto &step : steps_) {
            if (step.operation_->kind_ == Kind::DROP && step.before_)
              removed.emplace(step.before_->routine_.get_routine_id(), step.before_->routine_.get_object_type());
          }
          for (const auto &item : removed) {
            if (OB_FAIL(ret)) break;
            ret = connection.query(
                "SELECT dep_obj_id,dep_obj_type FROM __all_dependency WHERE ref_obj_id=? "
                "AND ref_obj_type=? ORDER BY dep_obj_id,dep_obj_type FOR UPDATE",
                [&](ObPluginSqlBinder &b) {
                  int code = b.bind_int64(item.first);
                  if (OB_SUCCESS == code) code = b.bind_int64(static_cast<int64_t>(item.second));
                  return code;
                }, [&](ObPluginSqlRowReader &r) {
                  int64_t id = 0, type = 0;
                  int code = r.read_int64(0, id);
                  if (OB_SUCCESS == code) code = r.read_int64(1, type);
                  if (OB_SUCCESS != code) return code;
                  if (id <= 0 || type <= static_cast<int64_t>(ObObjectType::INVALID) ||
                      type >= static_cast<int64_t>(ObObjectType::MAX_TYPE)) return OB_INVALID_DATA;
                  auto found = removed.find(id);
                  if (found == removed.end() || static_cast<int64_t>(found->second) != type) {
                    error = "routine update cannot remove an object with a surviving schema dependent";
                    return OB_OP_NOT_ALLOW;
                  }
                  return OB_SUCCESS;
                });
          }
          // Newly resolved bodies must not reintroduce a reference to a removed
          // published identity. Rebinding provisional references is a resolver
          // responsibility, not a name substitution in the catalog adapter.
          for (const auto &step : steps_) {
            if (OB_FAIL(ret)) break;
            if (step.after_ && step.operation_->create_arg_ &&
                active_.at(key(step.after_->routine_)) == step.after_) {
              for (const auto &dep : step.after_->dependencies_) {
                const auto found = removed.find(dep.get_ref_obj_id());
                if (found != removed.end() && found->second == dep.get_ref_obj_type()) {
                  error = "resolved routine refers to an identity removed by the update";
                  ret = OB_OP_NOT_ALLOW;
                  break;
                }
              }
            }
          }
          if (OB_SUCC(ret)) admitted_ = true;
          return ret;
        }

        int apply(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                  const ExtensionUpdateSnapshot &snapshot, std::vector<ExtensionMemberIdentity> &members,
                  std::string &error) override {
          UNUSED(error);
          members.clear();
          if (!admitted_ || snapshot.extension_id_ != request.expected_extension_id_ ||
              !transaction_.is_started() || !connection.is_in_transaction()) return OB_STATE_NOT_MATCH;
          admitted_ = false; // an admitted plan is single-use, even after failure
          int ret = OB_SUCCESS;
          for (const auto &step : steps_) {
            const auto &op = *step.operation_;
            if (op.kind_ == Kind::DROP) {
              if (step.before_) {
                ObErrorInfo errors = op.drop_arg_->error_info_;
                ret = ObPLDDLService::drop_routine(step.before_->routine_, errors,
                    &op.drop_arg_->ddl_stmt_str_, guard_, ddl_, &transaction_, step.deletion_.get());
              }
            } else {
              ObErrorInfo errors = op.create_arg_->error_info_;
              ObSArray<ObDependencyInfo> dependencies;
              if (OB_FAIL(dependencies.assign(step.after_->dependencies_))) {}
              else ret = ObPLDDLService::create_routine(step.after_->routine_,
                  step.before_ ? &step.before_->routine_ : nullptr, op.kind_ == Kind::ALTER,
                  errors, dependencies, &op.create_arg_->ddl_stmt_str_, guard_, ddl_, &transaction_,
                  op.kind_ == Kind::CREATE ? &step.after_->reservation_ : nullptr,
                  &step.after_->version_reservation_);
            }
            if (OB_FAIL(ret)) break;
          }
          if (OB_SUCC(ret)) {
            for (const auto &entry : active_) {
              const auto *node = entry.second;
              if (node && node->member_) {
                const uint64_t id = node->routine_.get_routine_id();
                if (id == 0 || id == OB_INVALID_ID) { ret = OB_ERR_UNEXPECTED; break; }
                members.push_back({static_cast<uint32_t>(ROUTINE_SCHEMA), id});
              }
            }
          }
          if (OB_FAIL(ret)) members.clear();
          return ret;
        }
      private:
        static bool standalone(ObRoutineType type) {
          return type == ROUTINE_FUNCTION_TYPE || type == ROUTINE_PROCEDURE_TYPE;
        }
        static Key key(const ObRoutineInfo &routine) {
          const auto &name = routine.get_routine_name();
          return {routine.get_routine_type(), std::string(name.ptr(), name.length())};
        }
        int copy_node(const ObRoutineInfo &routine, bool member, bool published, Node *&node) {
          auto owned = std::make_unique<Node>();
          int ret = owned->routine_.assign(routine);
          if (OB_SUCC(ret)) {
            owned->member_ = member; owned->published_ = published;
            node = owned.get(); nodes_.push_back(std::move(owned));
          }
          return ret;
        }
        int admit_operation(ObPluginSqlConnection &connection, const Operation &op, std::string &error) {
          int ret = OB_SUCCESS;
          if (!op.has_valid_shape()) return OB_INVALID_ARGUMENT;
          const bool drop = op.kind_ == Kind::DROP;
          if (drop ? !op.drop_arg_->is_valid() : !op.create_arg_->is_valid()) return OB_INVALID_ARGUMENT;
          if (!drop && (op.create_arg_->is_or_replace_ || op.create_arg_->with_if_not_exist_ ||
              op.create_arg_->is_need_alter_ != (op.kind_ == Kind::ALTER))) return OB_NOT_SUPPORTED;
          if (op.kind_ == Kind::CREATE && op.create_arg_->error_info_.get_error_status() == ERROR_STATUS_HAS_ERROR) {
            error = "extension routine body contains unresolved compilation errors";
            return OB_ERR_RESOLVE_SQL;
          }
          const ObString &db = drop ? op.drop_arg_->db_name_ : op.create_arg_->db_name_;
          const ObString &name = drop ? op.drop_arg_->routine_name_ : op.create_arg_->routine_info_.get_routine_name();
          const auto type = drop ? op.drop_arg_->routine_type_ : op.create_arg_->routine_info_.get_routine_type();
          const ObDatabaseSchema *database = nullptr;
          if (!standalone(type)) return OB_NOT_SUPPORTED;
          if (OB_FAIL(guard_.get_database_schema(db, database))) return ret;
          if (database == nullptr || database->get_database_id() != database_->get_database_id()) return OB_ERR_BAD_DATABASE;
          ret = drop ? ddl_.check_parallel_ddl_conflict(guard_, *op.drop_arg_)
                     : ddl_.check_parallel_ddl_conflict(guard_, *op.create_arg_);
          if (OB_FAIL(ret)) return ret;
          ObArenaAllocator allocator;
          ObStmtNeedPrivs privileges(allocator);
          ObNeedPriv need;
          need.db_ = db; need.table_ = name;
          need.obj_type_ = type == ROUTINE_FUNCTION_TYPE ? ObObjectType::FUNCTION : ObObjectType::PROCEDURE;
          need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
          need.priv_set_ = op.kind_ == Kind::CREATE ? OB_PRIV_CREATE_ROUTINE : OB_PRIV_ALTER_ROUTINE;
          if (OB_FAIL(privileges.need_privs_.reserve(1))) return ret;
          if (OB_FAIL(privileges.need_privs_.push_back(need))) return ret;
          if (!(priv_.user_priv_set_ & OB_PRIV_SUPER) && OB_FAIL(guard_.verify_read_only(privileges))) return ret;
          Key object_key{type, std::string(name.ptr(), name.length())};
          auto found = active_.find(object_key);
          if (found == active_.end()) {
            const ObRoutineInfo *published = nullptr;
            ret = type == ROUTINE_FUNCTION_TYPE
                ? guard_.get_standalone_function_info(database_->get_database_id(), name, published)
                : guard_.get_standalone_procedure_info(database_->get_database_id(), name, published);
            if (OB_FAIL(ret)) return ret;
            Node *node = nullptr;
            if (published && OB_FAIL(copy_node(*published, false, true, node))) return ret;
            found = active_.emplace(std::move(object_key), node).first;
          }
          Node *before = found->second, *after = nullptr;
          // Use the same explicit transaction-local grants as Query resolution;
          // merely owning a provisional schema must not bypass authorization.
          if (OB_FAIL(guard_.check_priv(priv_, roles_, privileges))) return ret;
          if (op.kind_ == Kind::CREATE) {
            if (before) return OB_ERR_SP_ALREADY_EXISTS;
            if (op.create_arg_->routine_info_.get_owner_id() != priv_.user_id_) return OB_ERR_NO_PRIVILEGE;
            if (OB_FAIL(copy_node(op.create_arg_->routine_info_, true, false, after))) return ret;
            if (OB_FAIL(after->dependencies_.assign(op.create_arg_->dependency_infos_))) return ret;
            after->dependencies_loaded_ = true;
            after->routine_.set_database_id(database_->get_database_id());
            after->routine_.set_routine_id(OB_INVALID_ID);
            auto *schema = ddl_.get_schema_service().get_schema_service();
            if (schema == nullptr) return OB_ERR_UNEXPECTED;
            if (OB_FAIL(RoutineIdReservation::reserve(*schema, after->routine_, after->reservation_))) return ret;
            after->routine_.set_routine_id(after->reservation_.id());
            if (OB_FAIL(RoutineVersionReservation::reserve(ddl_.get_schema_service(), transaction_,
                after->routine_, nullptr, after->version_reservation_))) return ret;
            after->routine_.set_schema_version(after->version_reservation_.version());
          } else if (!before) {
            if (!drop || !op.drop_arg_->if_exist_) return OB_ERR_SP_DOES_NOT_EXIST;
          } else if (drop) {
            if (!before->member_ && before->published_) {
              // Never detach another Extension's protection. Run the normal
              // membership check now, before this update detaches its own set.
              ret = connection.query(
                  "SELECT extension_id FROM __all_extension_member WHERE tenant_id=1 "
                  "AND database_id=? AND object_class=? AND object_id=? FOR UPDATE",
                  [&](ObPluginSqlBinder &b) {
                    int code = b.bind_int64(database_->get_database_id());
                    if (OB_SUCCESS == code) code = b.bind_int64(static_cast<int64_t>(ROUTINE_SCHEMA));
                    if (OB_SUCCESS == code) code = b.bind_int64(before->routine_.get_routine_id());
                    return code;
                  }, [&](ObPluginSqlRowReader &r) {
                    int64_t owner = 0;
                    int code = r.read_int64(0, owner);
                    if (OB_SUCCESS != code) return code;
                    error = "routine belongs to another Extension";
                    return owner > 0 ? OB_OP_NOT_ALLOW : OB_INVALID_DATA;
                  });
              if (OB_FAIL(ret)) return ret;
            }
          } else {
            const auto &replacement = op.create_arg_->routine_info_;
            if (replacement.get_routine_id() != before->routine_.get_routine_id() ||
                replacement.get_owner_id() != before->routine_.get_owner_id()) return OB_STATE_NOT_MATCH;
            if (script_ == nullptr && (!before->published_ || before->altered_)) {
              error = "ALTER of a new or already altered routine requires transaction-aware semantic resolution";
              return OB_NOT_SUPPORTED;
            }
            if (script_ != nullptr && replacement.get_schema_version() != before->routine_.get_schema_version())
              return OB_STATE_NOT_MATCH;
            // MySQL ALTER changes attributes, not the body. Its resolver does
            // not rebuild dependency_infos_; treating an empty array as a new
            // body would erase the existing dependency graph on replacement.
            if (!before->dependencies_loaded_) {
              if (!before->published_) return OB_ERR_UNEXPECTED;
              if (OB_FAIL(ObDependencyInfo::collect_ref_infos(before->routine_.get_routine_id(),
                  transaction_, before->dependencies_))) return ret;
              before->dependencies_loaded_ = true;
            }
            if (OB_FAIL(copy_node(replacement, before->member_, before->published_, after))) return ret;
            if (OB_FAIL(after->dependencies_.assign(before->dependencies_))) return ret;
            after->dependencies_loaded_ = true;
            after->routine_.set_database_id(database_->get_database_id());
            after->altered_ = true;
            if (OB_FAIL(RoutineVersionReservation::reserve(ddl_.get_schema_service(), transaction_,
                after->routine_, &before->routine_, after->version_reservation_))) return ret;
            after->routine_.set_schema_version(after->version_reservation_.version());
          }
          std::unique_ptr<RoutineVersionReservation> deletion;
          if (after != nullptr) {
            // Match add_routine_params' eventual identity/version stamping now,
            // before a following statement can borrow this complete schema.
            auto &parameters = after->routine_.get_routine_params();
            for (int64_t i = 0; i < parameters.count(); ++i) {
              if (parameters.at(i) == nullptr) return OB_ERR_UNEXPECTED;
              parameters.at(i)->set_routine_id(after->routine_.get_routine_id());
              parameters.at(i)->set_schema_version(after->routine_.get_schema_version());
            }
          }
          if (drop && before) {
            deletion = std::make_unique<RoutineVersionReservation>();
            if (OB_FAIL(RoutineVersionReservation::reserve_drop(ddl_.get_schema_service(), transaction_,
                before->routine_, *deletion))) return ret;
          }
          steps_.push_back({&op, before, after, std::move(deletion)});
          found->second = after; // retain a tombstone: never reload a dropped name
          return OB_SUCCESS;
        }
        const ObIArray<Operation> &operations_;
        const ObSessionPrivInfo &priv_;
        const ObIArray<uint64_t> &roles_;
        ObSchemaGetterGuard &guard_;
        ObDDLService &ddl_;
        ObDDLSQLTransaction &transaction_;
        IExtensionRoutineScript *script_;
        const int64_t count_;
        std::shared_ptr<RoutineSchemaOverlay> overlay_;
        std::shared_ptr<RoutinePrivilegeOverlay> privileges_;
        const ObDatabaseSchema *database_ = nullptr;
        std::vector<std::unique_ptr<Node>> nodes_;
        std::vector<Step> steps_;
        std::map<Key, Node *, NameLess> active_;
        bool admitted_ = false;
        bool automatic_privileges_ = false;
      } updater(operations, session_priv, enabled_roles, guard, ddl_service, transaction, script);
      ret = catalog.update_extension(request, updater, extension_id, changed, error, &transaction, schema_version);
      if (OB_SUCC(ret)) {
        if (changed) {
          try { publication_status = ddl_service.publish_schema(); }
          catch (const std::bad_alloc &) { publication_status = OB_ALLOCATE_MEMORY_FAILED; }
          catch (...) { publication_status = OB_ERR_UNEXPECTED; }
        } else publication_status = OB_SUCCESS;
      }
    }
  } catch (const std::bad_alloc &) {
    if (extension_id != 0) publication_status = OB_ALLOCATE_MEMORY_FAILED;
    else ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (extension_id != 0) publication_status = OB_ERR_UNEXPECTED;
    else ret = OB_ERR_UNEXPECTED;
  }
  return ret;
#endif
}

int ObPLDDLService::create_routine(const obcall::ObCreateRoutineArg &arg,
                                   rootserver::ObDDLService &ddl_service,
                                   ObDDLSQLTransaction *external_trans,
                                   uint64_t *created_routine_id)
{
  int ret = OB_SUCCESS;
  if (nullptr != created_routine_id) *created_routine_id = OB_INVALID_ID;
  ObSchemaGetterGuard schema_guard;
  if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    ObRoutineInfo routine_info = arg.routine_info_;
    const ObRoutineInfo* old_routine_info = NULL;
    
    ObString database_name = arg.db_name_;
    bool is_or_replace = arg.is_need_alter_;
    bool is_inner = arg.is_or_replace_;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(schema_guard.get_database_schema(database_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
    } else if (!is_inner && db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("Can't not create routine of db in recyclebin", K(ret), K(arg), K(*db_schema));
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database id is invalid", K(*db_schema), K(ret));
    } else {
      routine_info.set_database_id(db_schema->get_database_id());
    }
    bool exist = false;
    if (OB_SUCC(ret)) {
      if (routine_info.get_routine_type() == ROUTINE_PROCEDURE_TYPE) {
        if (OB_FAIL(schema_guard.check_standalone_procedure_exist(db_schema->get_database_id(),
                                                                  routine_info.get_routine_name(), exist))) {
        } else if (exist && !is_or_replace) {
          ret = OB_ERR_SP_ALREADY_EXISTS;
          LOG_USER_ERROR(OB_ERR_SP_ALREADY_EXISTS, "PROCEDURE",
                          routine_info.get_routine_name().length(), routine_info.get_routine_name().ptr());
        } else if (exist && is_or_replace) {
          if (OB_FAIL(schema_guard.get_standalone_procedure_info(db_schema->get_database_id(),
                                                                  routine_info.get_routine_name(), old_routine_info))) {
          } else if (OB_ISNULL(old_routine_info)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("old routine info is NULL", K(ret));
          }
        }
      } else {
        if (OB_FAIL(schema_guard.check_standalone_function_exist(db_schema->get_database_id(),
                                                                  routine_info.get_routine_name(), exist))) {
        } else if (exist && !is_or_replace) {
          ret = OB_ERR_SP_ALREADY_EXISTS;
          LOG_USER_ERROR(OB_ERR_SP_ALREADY_EXISTS, "FUNCTION",
                          routine_info.get_routine_name().length(), routine_info.get_routine_name().ptr());
        } else if (exist && is_or_replace) {
          if (OB_FAIL(schema_guard.get_standalone_function_info(db_schema->get_database_id(),
                                                                routine_info.get_routine_name(), old_routine_info))) {
          } else if (OB_ISNULL(old_routine_info)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("old routine info is NULL", K(ret));
          }
        }
      }
      if (OB_SUCC(ret)) {
        ObErrorInfo error_info = arg.error_info_;
        ObSArray<ObDependencyInfo> &dep_infos = const_cast<ObSArray<ObDependencyInfo> &>(arg.dependency_infos_);
        if (OB_FAIL(create_routine(routine_info,
                                   old_routine_info,
                                   (exist && is_or_replace),
                                   error_info,
                                   dep_infos,
                                   &arg.ddl_stmt_str_,
                                   schema_guard,
                                   ddl_service,
                                   external_trans))) {
        } else if (nullptr != created_routine_id) {
          *created_routine_id = routine_info.get_routine_id();
        }
      }
    }
  }
  return ret;
}

int ObPLDDLService::create_routine(ObRoutineInfo &routine_info,
                                   const ObRoutineInfo* old_routine_info,
                                   bool replace,
                                   ObErrorInfo &error_info,
                                   ObIArray<ObDependencyInfo> &dep_infos,
                                   const ObString *ddl_stmt_str,
                                   share::schema::ObSchemaGetterGuard &schema_guard,
                                   rootserver::ObDDLService &ddl_service,
                                   ObDDLSQLTransaction *external_trans,
                                   RoutineIdReservation *reservation,
                                   RoutineVersionReservation *version_reservation)
{
  int ret = OB_SUCCESS;
  CK((replace && OB_NOT_NULL(old_routine_info)) || (!replace && OB_ISNULL(old_routine_info)));
  CK(reservation == nullptr || (!replace && external_trans != nullptr && external_trans->is_started()));
  CK(version_reservation == nullptr || (external_trans != nullptr && external_trans->is_started()));
  CK (OB_NOT_NULL(ddl_service.schema_service_) && OB_NOT_NULL(ddl_service.sql_proxy_));
  // The old reserved-ACL cleanup rejected parallel Root transactions. Preserve
  // that restriction at the owner boundary now that the writer is generic.
  if (OB_SUCC(ret) && !replace && version_reservation != nullptr && external_trans->is_enable_parallel())
    ret = OB_STATE_NOT_MATCH;
  if (OB_SUCC(ret)) {
    
    ObDDLSQLTransaction local_trans(ddl_service.schema_service_);
    ObDDLSQLTransaction &trans = nullptr == external_trans ? local_trans : *external_trans;

    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (nullptr == external_trans && OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    }
    if (OB_SUCC(ret)) {
      RoutineCatalogWriter writer(*ddl_service.schema_service_, *ddl_service.sql_proxy_, schema_guard,
                                  trans, nullptr != external_trans);
      ret = writer.create(routine_info, old_routine_info, error_info, dep_infos, ddl_stmt_str,
                          reservation, version_reservation);
    }
    if (nullptr == external_trans && trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret) && nullptr == external_trans) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::alter_routine(const obcall::ObCreateRoutineArg &arg,
                                  rootserver::ObDDLService &ddl_service,
                                  ObDDLSQLTransaction *external_trans)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    ObErrorInfo error_info = arg.error_info_;
    const ObRoutineInfo *routine_info = NULL;
    
    if (OB_FAIL(schema_guard.get_routine_info( arg.routine_info_.get_routine_id(), routine_info))) {
    } else if (OB_ISNULL(routine_info)) {
      ret = OB_ERR_SP_DOES_NOT_EXIST;
      LOG_WARN("routine info is not exist!", K(ret), K(arg.routine_info_));
    }
    if (OB_FAIL(ret)) {
    } else if (arg.is_need_alter_) {
      if (OB_FAIL(create_routine(arg, ddl_service, external_trans))) {
      }
    } else {
      if (OB_FAIL(alter_routine(*routine_info, error_info, &arg.ddl_stmt_str_, schema_guard,
                                ddl_service, external_trans))) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::alter_routine(const ObRoutineInfo &routine_info,
                                  ObErrorInfo &error_info,
                                  const ObString *ddl_stmt_str,
                                  share::schema::ObSchemaGetterGuard &schema_guard,
                                  rootserver::ObDDLService &ddl_service,
                                  ObDDLSQLTransaction *external_trans)
{
  int ret = OB_SUCCESS;
  if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    
    ObDDLSQLTransaction owned_trans(ddl_service.schema_service_);
    ObDDLSQLTransaction &trans = nullptr == external_trans ? owned_trans : *external_trans;
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (nullptr == external_trans && OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else {
      RoutineCatalogWriter writer(*ddl_service.schema_service_, *ddl_service.sql_proxy_, schema_guard,
                                  trans, nullptr != external_trans);
      ret = writer.alter(routine_info, error_info, ddl_stmt_str);
    }
    if (nullptr == external_trans && trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed!", K(ret), K(temp_ret));
        ret = OB_SUCCESS == ret ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret) && nullptr == external_trans) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_routine(const ObDropRoutineArg &arg,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    
    const ObString &db_name = arg.db_name_;
    const ObString &routine_name = arg.routine_name_;
    ObRoutineType routine_type = arg.routine_type_;
    ObSchemaGetterGuard schema_guard;
    const ObDatabaseSchema *db_schema = NULL;
    /*!
     * Compatible with MySQL behavior:
     * create database test;
     * use test;
     * drop database test;
     * drop function if exists no_such_func; -- warning 1035
     * drop procedure if exists no_such_proc; -- error 1046
     * drop function no_such_func; --error 1035
     * drop procedure no_such_proc; --error 1046
     */
    if (db_name.empty()) {
      ret = OB_ERR_NO_DB_SELECTED;
      LOG_WARN("no database selected", K(ret), K(db_name));
    } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
    } else if (OB_FAIL(ddl_service.check_parallel_ddl_conflict(schema_guard, arg))) {
    } else if (OB_FAIL(schema_guard.get_database_schema( db_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, db_name.length(), db_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("Can't not create procedure of db in recyclebin", K(ret), K(arg), K(*db_schema));
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database id is invalid", K(*db_schema), K(ret));
    }

    if (OB_SUCC(ret)) {
      bool exist = false;
      const ObRoutineInfo *routine_info = NULL;
      if (ROUTINE_PROCEDURE_TYPE == routine_type) {
        if (OB_FAIL(schema_guard.check_standalone_procedure_exist(db_schema->get_database_id(),
                                                                  routine_name, exist))) {
        } else if (exist) {
          if (OB_FAIL(schema_guard.get_standalone_procedure_info(db_schema->get_database_id(),
                                                                 routine_name, routine_info))) {
          }
        } else if (!arg.if_exist_) {
          ret = OB_ERR_SP_DOES_NOT_EXIST;
          LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "PROCEDURE", db_name.length(), db_name.ptr(),
                         routine_name.length(), routine_name.ptr());
        }
      } else {
        if (OB_FAIL(schema_guard.check_standalone_function_exist(db_schema->get_database_id(),
                                                                 routine_name, exist))) {
        } else if (exist) {
          if (OB_FAIL(schema_guard.get_standalone_function_info(db_schema->get_database_id(),
                                                                routine_name, routine_info))) {
          }
        } else if (!arg.if_exist_) {
          ret = OB_ERR_SP_DOES_NOT_EXIST;
          LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION", db_name.length(), db_name.ptr(),
                         routine_name.length(), routine_name.ptr());
        }
      }

      if (OB_SUCC(ret) && !OB_ISNULL(routine_info)) {
        ObErrorInfo error_info = arg.error_info_;
        if (OB_FAIL(drop_routine(*routine_info,
                                 error_info,
                                 &arg.ddl_stmt_str_,
                                 schema_guard,
                                 ddl_service))) {
        }
      }
    }
    if (OB_ERR_NO_DB_SELECTED == ret && ROUTINE_FUNCTION_TYPE == routine_type) {
      if (arg.if_exist_) {
        ret = OB_SUCCESS;
        LOG_USER_WARN(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION (UDF)",
                      db_name.length(), db_name.ptr(),
                      routine_name.length(), routine_name.ptr());
      } else {
        ret = OB_ERR_SP_DOES_NOT_EXIST;
        LOG_USER_ERROR(OB_ERR_SP_DOES_NOT_EXIST, "FUNCTION (UDF)",
                      db_name.length(), db_name.ptr(),
                      routine_name.length(), routine_name.ptr());
        LOG_WARN("FUNCTION (UDF) does not exists", K(ret), K(routine_name), K(db_name));
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_routine(const ObRoutineInfo &routine_info,
                                 ObErrorInfo &error_info,
                                 const ObString *ddl_stmt_str,
                                 share::schema::ObSchemaGetterGuard &schema_guard,
                                 rootserver::ObDDLService &ddl_service,
                                 ObDDLSQLTransaction *external_trans,
                                 RoutineVersionReservation *version_reservation)
{
  int ret = OB_SUCCESS;
  if (version_reservation != nullptr && external_trans == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (nullptr != external_trans && (!external_trans->is_started() || external_trans->is_enable_parallel())) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    
    ObDDLSQLTransaction owned_trans(ddl_service.schema_service_);
    ObDDLSQLTransaction &trans = nullptr == external_trans ? owned_trans : *external_trans;
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (nullptr == external_trans && OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else {
      RoutineCatalogWriter writer(*ddl_service.schema_service_, *ddl_service.sql_proxy_, schema_guard,
                                  trans, nullptr != external_trans);
      struct LegacyInvalidation final : IRoutineCacheInvalidation {
        ObMultiVersionSchemaService &service;
        explicit LegacyInvalidation(ObMultiVersionSchemaService &service) : service(service) {}
        int on_drop(uint64_t id, uint64_t database) override {
          return pl::ObPLCacheMgr::flush_pl_cache_by_sql(id, database, service);
        }
      } invalidation(*ddl_service.schema_service_);
      ret = writer.drop(routine_info, error_info, ddl_stmt_str, invalidation, version_reservation);
    }
    if (nullptr == external_trans && trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret) && nullptr == external_trans) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

//----Functions for managing package----
int ObPLDDLService::create_package(const obcall::ObCreatePackageArg &arg,
                                    rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    ObPackageInfo new_package_info;
    const ObPackageInfo *old_package_info = NULL;
    
    ObString database_name = arg.db_name_;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(new_package_info.assign(arg.package_info_))) {
    } else if (OB_FAIL(schema_guard.get_database_schema( database_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("Can't not create package of db in recyclebin", K(ret), K(arg), K(*db_schema));
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database id is invalid", K(*db_schema), K(ret));
    } else {
      new_package_info.set_database_id(db_schema->get_database_id());
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(schema_guard.get_package_info( db_schema->get_database_id(), new_package_info.get_package_name(),
                                                new_package_info.get_type(),
                                                old_package_info))) {
      } else if (OB_ISNULL(old_package_info) || arg.is_replace_) {
        bool need_create = true;
        // For system packages, to avoid multiple rebuilds, compare the new system package with the existing system package to see if they are the same
        if (OB_NOT_NULL(old_package_info)) {
          if (old_package_info->get_source().length() == new_package_info.get_source().length()
              && (0 == MEMCMP(old_package_info->get_source().ptr(),
                              new_package_info.get_source().ptr(),
                              old_package_info->get_source().length()))
              && old_package_info->get_exec_env() == new_package_info.get_exec_env()) {
            need_create = false;
            LOG_INFO("do not recreate package with same source",
                     K(ret),
                     K(old_package_info->get_source()),
                     K(new_package_info.get_source()), K(need_create));
          } else {
            LOG_INFO("recreate package with diff source",
                     K(ret),
                     K(old_package_info->get_source()),
                     K(new_package_info.get_source()), K(need_create));
          }
        }
        if (need_create) {
          ObSArray<ObRoutineInfo> &public_routine_infos = const_cast<ObSArray<ObRoutineInfo> &>(arg.public_routine_infos_);
          ObErrorInfo error_info = arg.error_info_;
          ObSArray<ObDependencyInfo> &dep_infos =
                               const_cast<ObSArray<ObDependencyInfo> &>(arg.dependency_infos_);
          if (OB_FAIL(create_package(schema_guard,
                                     old_package_info,
                                     new_package_info,
                                     public_routine_infos,
                                     error_info,
                                     dep_infos,
                                     &arg.ddl_stmt_str_,
                                     ddl_service))) {
          }
        }
      } else {
        ret = OB_ERR_PACKAGE_ALREADY_EXISTS;
        const char *type = (new_package_info.get_type() == ObPackageType::PACKAGE_TYPE ? "PACKAGE" : "PACKAGE BODY");
        LOG_USER_ERROR(OB_ERR_PACKAGE_ALREADY_EXISTS, type,
                       database_name.length(), database_name.ptr(),
                       new_package_info.get_package_name().length(), new_package_info.get_package_name().ptr());
      }
    }
  }
  return ret;
}

int ObPLDDLService::create_package(ObSchemaGetterGuard &schema_guard,
                                   const ObPackageInfo *old_package_info,
                                   ObPackageInfo &new_package_info,
                                   ObIArray<ObRoutineInfo> &public_routine_infos,
                                   ObErrorInfo &error_info,
                                   ObIArray<ObDependencyInfo> &dep_infos,
                                   const ObString *ddl_stmt_str,
                                   rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else if (OB_FAIL(pl_operator.create_package(old_package_info,
                                                   new_package_info,
                                                   trans,
                                                   schema_guard,
                                                   public_routine_infos,
                                                   error_info,
                                                   dep_infos,
                                                   ddl_stmt_str))) {
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_package(const obcall::ObDropPackageArg &arg,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else {
    
    const ObString &db_name = arg.db_name_;
    const ObString &package_name = arg.package_name_;
    ObPackageType package_type = arg.package_type_;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(schema_guard.get_database_schema( db_name, db_schema))) {
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, db_name.length(), db_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("Can't not create package of db in recyclebin", K(ret), K(arg), K(*db_schema));
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database id is invalid", K(*db_schema), K(ret));
    }
    if (OB_SUCC(ret)) {
      bool exist = false;
      if (OB_FAIL(schema_guard.check_package_exist(db_schema->get_database_id(),
          package_name, package_type, exist))) {
      } else if (exist) {
        const ObPackageInfo *package_info = NULL;
        ObErrorInfo error_info = arg.error_info_;
        if (OB_FAIL(schema_guard.get_package_info(db_schema->get_database_id(), package_name, package_type, package_info))) {
        } else if (OB_FAIL(drop_package(schema_guard,
                                        *package_info,
                                        error_info,
                                        &arg.ddl_stmt_str_,
                                        ddl_service))) {
        }
      } else {
        ret = OB_ERR_PACKAGE_DOSE_NOT_EXIST;
        const char *type = (package_type == ObPackageType::PACKAGE_TYPE ? "PACKAGE" : "PACKAGE BODY");
        LOG_USER_ERROR(OB_ERR_PACKAGE_DOSE_NOT_EXIST, type,
                       db_name.length(), db_name.ptr(),
                       package_name.length(), package_name.ptr());
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_package(share::schema::ObSchemaGetterGuard &schema_guard,
                                 const ObPackageInfo &package_info,
                                 ObErrorInfo &error_info,
                                 const ObString *ddl_stmt_str,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    } else if (OB_FAIL(pl_operator.drop_package(package_info,
                                                 trans,
                                                 schema_guard,
                                                 error_info,
                                                 ddl_stmt_str))) {
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}
//----End of functions for managing package----

//----Functions for managing trigger----
int ObPLDDLService::create_trigger(const obcall::ObCreateTriggerArg &arg,
                                    obcall::ObCreateTriggerRes *res,
                                    rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else if (OB_FAIL(create_trigger(arg, schema_guard, res, ddl_service))) {
  }
  return ret;
}

int ObPLDDLService::alter_trigger(const obcall::ObAlterTriggerArg &arg,
                                  rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  
  bool is_enable = false;
  int64_t refreshed_schema_version = 0;
  OZ (check_env_before_ddl(schema_guard, arg, ddl_service));
  OX (is_enable = arg.trigger_infos_.at(0).is_enable());
  OZ (schema_guard.get_schema_version(refreshed_schema_version));
  if (OB_SUCC(ret)) {
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    OZ (trans.start(ddl_service.sql_proxy_, refreshed_schema_version), refreshed_schema_version);
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.trigger_infos_.count(); ++i) {
      const ObTriggerInfo *old_tg_info = NULL;
      ObTriggerInfo new_tg_info;
      OZ (new_tg_info.assign(arg.trigger_infos_.at(i)));
      OZ (schema_guard.get_trigger_info( new_tg_info.get_trigger_id(), old_tg_info));
      CK (OB_NOT_NULL(old_tg_info), OB_ERR_TRIGGER_NOT_EXIST);
      if (OB_SUCC(ret)) {
        if (!arg.is_set_status_) {
          const ObTriggerInfo *other_trg_info = NULL;
          ObString new_trg_name = new_tg_info.get_trigger_name();
          ObString new_trg_body = new_tg_info.get_trigger_body();
          OZ (schema_guard.get_trigger_info(
                                            new_tg_info.get_database_id(),
                                            new_trg_name,
                                            other_trg_info));
          OV (OB_ISNULL(other_trg_info), OB_OBJ_ALREADY_EXIST, new_tg_info);
          OZ (new_tg_info.deep_copy(*old_tg_info));
          OZ (new_tg_info.set_trigger_name(new_trg_name));
          OZ (new_tg_info.set_trigger_body(new_trg_body));
        } else {
          OZ (new_tg_info.deep_copy(*old_tg_info));
          OX (new_tg_info.set_is_enable(is_enable));
        }
        OZ (pl_operator.alter_trigger(new_tg_info, trans, &arg.ddl_stmt_str_));
      }
    }
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_trigger(const obcall::ObDropTriggerArg &arg,
                                 rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  
  uint64_t trigger_database_id = OB_INVALID_ID;
  const ObString &trigger_database = arg.trigger_database_;
  const ObString &trigger_name = arg.trigger_name_;
  const ObTriggerInfo *trigger_info = NULL;
  if (OB_FAIL(check_env_before_ddl(schema_guard, arg, ddl_service))) {
  } else if (OB_FAIL(ddl_service.get_database_id(schema_guard, trigger_database, trigger_database_id))) {
  } else if (OB_FAIL(schema_guard.get_trigger_info( trigger_database_id, trigger_name, trigger_info))) {
  } else if (OB_ISNULL(trigger_info)) {
    ret = OB_ERR_TRIGGER_NOT_EXIST;
  } else if (trigger_info->is_in_recyclebin()) {
    ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
    LOG_WARN("trigger is in recyclebin", K(ret),
             K(trigger_info->get_trigger_id()), K(trigger_info->get_trigger_name()));
  } else if (OB_FAIL(drop_trigger_in_trans(*trigger_info, &arg.ddl_stmt_str_, schema_guard, ddl_service))) {
  }
  if (OB_ERR_TRIGGER_NOT_EXIST == ret || OB_ERR_BAD_DATABASE == ret) {
    ret = OB_ERR_TRIGGER_NOT_EXIST;
    if (arg.if_exist_) {
      ret = OB_SUCCESS;
      LOG_MYSQL_USER_NOTE(OB_ERR_TRIGGER_NOT_EXIST);
    } else {
      LOG_MYSQL_USER_ERROR(OB_ERR_TRIGGER_NOT_EXIST);
    }
    LOG_WARN("trigger not exist", K(arg.trigger_database_), K(arg.trigger_name_), K(ret));
  }
  return ret;
}

int ObPLDDLService::create_trigger(const obcall::ObCreateTriggerArg &arg,
                                   ObSchemaGetterGuard &schema_guard,
                                   obcall::ObCreateTriggerRes *res,
                                   rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  ObTriggerInfo new_trigger_info;
  //in_second_stage_ is false, Indicates that the trigger is created normally
  //true Indicates that the error message is inserted into the system table after the trigger is created
  //So the following steps can be skipped
  
  uint64_t trigger_database_id = OB_INVALID_ID;
  uint64_t base_object_id = OB_INVALID_ID;
  ObSchemaType base_object_type = static_cast<ObSchemaType>(arg.trigger_info_.get_base_object_type());
  const ObString &trigger_database = arg.trigger_database_;
  const ObString &base_object_database = arg.base_object_database_;
  const ObString &base_object_name = arg.base_object_name_;
  if (OB_FAIL(new_trigger_info.assign(arg.trigger_info_))) {
  } else {
    const ObTriggerInfo *old_trigger_info = NULL;
    if (OB_FAIL(ddl_service.get_database_id(schema_guard, trigger_database, trigger_database_id))) {
    } else if (OB_FAIL(get_object_info(schema_guard,
                                       base_object_database,
                                       base_object_name,
                                       base_object_type,
                                       base_object_id,
                                       ddl_service))) {
    } else if (FALSE_IT(new_trigger_info.set_database_id(trigger_database_id))) {
    } else if (FALSE_IT(new_trigger_info.set_base_object_type(base_object_type))) {
    } else if (FALSE_IT(new_trigger_info.set_base_object_id(base_object_id))) {
    } else if (OB_FAIL(try_get_exist_trigger(schema_guard, new_trigger_info, old_trigger_info, arg.with_replace_))) {
    } else {
      if (NULL != old_trigger_info) {
        new_trigger_info.set_trigger_id(old_trigger_info->get_trigger_id());
      }
    }
  }
  if (OB_SUCC(ret)) {
    int64_t table_schema_version = OB_INVALID_VERSION;
    if (OB_ISNULL(res)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("res is NULL", K(ret));
    } else if (OB_FAIL(create_trigger_in_trans(new_trigger_info,
                                               const_cast<ObErrorInfo &>(arg.error_info_),
                                               const_cast<ObSArray<ObDependencyInfo> &>(arg.dependency_infos_),
                                               &arg.ddl_stmt_str_,
                                               arg.in_second_stage_,
                                               schema_guard,
                                               table_schema_version,
                                               ddl_service))) {
    } else {
      res->table_schema_version_ = table_schema_version;
      res->trigger_schema_version_ = new_trigger_info.get_schema_version();
    }
  }
  return ret;
}

int ObPLDDLService::create_trigger_in_trans(share::schema::ObTriggerInfo &trigger_info,
                                            share::schema::ObErrorInfo &error_info,
                                            ObIArray<ObDependencyInfo> &dep_infos,
                                            const common::ObString *ddl_stmt_str,
                                            bool in_second_stage,
                                            share::schema::ObSchemaGetterGuard &schema_guard,
                                            int64_t &table_schema_version,
                                            rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    }
    if (OB_SUCC(ret) && !in_second_stage) {
        OZ (adjust_trigger_action_order(schema_guard, trans, pl_operator, trigger_info, true));
    }
    OZ (pl_operator.create_trigger(trigger_info, trans, error_info, dep_infos, table_schema_version, ddl_stmt_str));
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::drop_trigger_in_trans(const share::schema::ObTriggerInfo &trigger_info,
                                          const common::ObString *ddl_stmt_str,
                                          share::schema::ObSchemaGetterGuard &schema_guard,
                                          rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ddl_service.schema_service_) || OB_ISNULL(ddl_service.sql_proxy_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("argument is NULL", K(ret));
  } else {
    
    ObDDLSQLTransaction trans(ddl_service.schema_service_);
    ObPLDDLOperator pl_operator(*ddl_service.schema_service_, *ddl_service.sql_proxy_);
    int64_t refreshed_schema_version = 0;
    if (OB_FAIL(schema_guard.get_schema_version(refreshed_schema_version))) {
    } else if (OB_FAIL(trans.start(ddl_service.sql_proxy_, refreshed_schema_version))) {
    }
    OZ (adjust_trigger_action_order(schema_guard, trans, pl_operator, const_cast<ObTriggerInfo &>(trigger_info), false));
    OZ (pl_operator.drop_trigger(trigger_info, trans, ddl_stmt_str));
    if (trans.is_started()) {
      int temp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCC(ret)))) {
        LOG_ERROR("trans end failed", "is_commit", OB_SUCCESS == ret, K(temp_ret));
        ret = (OB_SUCC(ret)) ? temp_ret : ret;
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service.publish_schema())) {
      }
    }
  }
  return ret;
}

int ObPLDDLService::try_get_exist_trigger(share::schema::ObSchemaGetterGuard &schema_guard,
                                          const share::schema::ObTriggerInfo &new_trigger_info,
                                          const share::schema::ObTriggerInfo *&old_trigger_info,
                                          bool with_replace)
{
  int ret = OB_SUCCESS;
  const ObString &trigger_name = new_trigger_info.get_trigger_name();
  if (OB_FAIL(schema_guard.get_trigger_info(
                                            new_trigger_info.get_database_id(),
                                            trigger_name, old_trigger_info))) {
  } else if (NULL != old_trigger_info) {
    if (new_trigger_info.get_base_object_id() != old_trigger_info->get_base_object_id()) {
      ret = OB_ERR_TRIGGER_EXIST_ON_OTHER_TABLE;
      LOG_USER_ERROR(OB_ERR_TRIGGER_EXIST_ON_OTHER_TABLE, trigger_name.length(), trigger_name.ptr());
    } else if (!with_replace) {
      ret = OB_ERR_TRIGGER_ALREADY_EXIST;
      LOG_USER_ERROR(OB_ERR_TRIGGER_ALREADY_EXIST, trigger_name.length(), trigger_name.ptr());
    }
  }
  return ret;
}

int ObPLDDLService::rebuild_trigger_on_rename(share::schema::ObSchemaGetterGuard &schema_guard,
                                              const share::schema::ObTableSchema &table_schema,
                                              ObDDLOperator &ddl_operator,
                                              ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObDatabaseSchema *database_schema = NULL;
  const ObString *database_name = NULL;
  const ObString &table_name = table_schema.get_table_name_str();
  
  OZ (schema_guard.get_database_schema( table_schema.get_database_id(), database_schema),
      table_schema.get_database_id());
  OV (OB_NOT_NULL(database_schema), OB_ERR_UNEXPECTED, table_schema.get_database_id());
  OX (database_name = &database_schema->get_database_name_str());
  OZ (rebuild_trigger_on_rename(schema_guard,
                                table_schema.get_trigger_list(),
                                *database_name,
                                table_name,
                                ddl_operator,
                                trans));
  return ret;
}

int ObPLDDLService::rebuild_trigger_on_rename(share::schema::ObSchemaGetterGuard &schema_guard,
                                              const common::ObIArray<uint64_t> &trigger_list,
                                              const common::ObString &database_name,
                                              const common::ObString &table_name,
                                              ObDDLOperator &ddl_operator,
                                              ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTriggerInfo *trigger_info = NULL;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (schema_guard.get_trigger_info( trigger_list.at(i), trigger_info), trigger_list.at(i));
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_list.at(i));
    OZ (pl_operator.rebuild_trigger_on_rename(*trigger_info, database_name, table_name, trans));
  }
  return ret;
}

int ObPLDDLService::create_trigger_for_truncate_table(share::schema::ObSchemaGetterGuard &schema_guard,
                                                      const common::ObIArray<uint64_t> &origin_trigger_list,
                                                      share::schema::ObTableSchema &new_table_schema,
                                                      ObDDLOperator &ddl_operator,
                                                      ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObTriggerInfo *origin_trigger_info = NULL;
  ObTriggerInfo new_trigger_info;
  ObString spec_source;
  ObString body_source;
  ObErrorInfo error_info;
  ObArenaAllocator inner_alloc;
  new_table_schema.get_trigger_list().reset();
  bool is_update_table_schema_version = false;
  const ObDatabaseSchema *db_schema = NULL;
  
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  OZ (schema_guard.get_database_schema(
                                       new_table_schema.get_database_id(),
                                       db_schema));
  CK (db_schema != NULL);
  for (int64_t i = 0; OB_SUCC(ret) && i < origin_trigger_list.count(); i++) {
    is_update_table_schema_version = i == origin_trigger_list.count() - 1 ? true : false;
    uint64_t new_trigger_id = OB_INVALID_ID;
    OZ (schema_guard.get_trigger_info( origin_trigger_list.at(i), origin_trigger_info),
                                      origin_trigger_list.at(i));
    if (OB_SUCC(ret)) {
      if (OB_FAIL(new_trigger_info.deep_copy(*origin_trigger_info))) {
      } else if (OB_FAIL(pl_operator.get_multi_schema_service().get_schema_service()->fetch_new_trigger_id(new_trigger_id))) {
      } else {
        new_trigger_info.set_trigger_id(new_trigger_id);
        new_trigger_info.set_base_object_id(new_table_schema.get_table_id());
        new_table_schema.get_trigger_list().push_back(new_trigger_id);
        if (OB_SUCC(ret)) {
          ObSEArray<ObDependencyInfo, 1> dep_infos;
          int64_t table_schema_version = OB_INVALID_VERSION;
          if (OB_FAIL(pl_operator.create_trigger(new_trigger_info,
                                                 trans,
                                                 error_info,
                                                 dep_infos,
                                                 table_schema_version,
                                                 &origin_trigger_info->get_trigger_body(),
                                                 is_update_table_schema_version,
                                                 true))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObPLDDLService::adjust_trigger_action_order(share::schema::ObSchemaGetterGuard &schema_guard,
                                                ObDDLSQLTransaction &trans,
                                                ObPLDDLOperator &pl_operator,
                                                ObTriggerInfo &trigger_info,
                                                bool is_create_trigger)
{
  int ret = OB_SUCCESS;
#define ALTER_OLD_TRIGGER(source_trg_info) \
  ObTriggerInfo copy_trg_info;   \
  OZ (copy_trg_info.assign(*source_trg_info)); \
  OX (copy_trg_info.set_action_order(new_action_order)); \
  OZ (pl_operator.alter_trigger(copy_trg_info, trans, NULL, false/*is_update_table_schema_version*/));

  common::ObSArray<uint64_t> trg_list;
  if (OB_SUCC(ret)) {
    const ObTableSchema *table_schema = NULL;
    OZ (schema_guard.get_table_schema( trigger_info.get_base_object_id(), table_schema));
    OV (OB_NOT_NULL(table_schema));
    OZ (trg_list.assign(table_schema->get_trigger_list()));
  }
  if (OB_SUCC(ret)) {
    const ObTriggerInfo *old_trg_info = NULL;
    int64_t new_action_order = 0; // the old trigger's new action order
    if (is_create_trigger) {
      int64_t action_order = 1; // action order for the trigger being created
      const ObTriggerInfo *ref_trg_info = NULL;
      if (OB_SUCC(ret)) {
        if (!trigger_info.get_ref_trg_name().empty()) {
          OZ (schema_guard.get_trigger_info( trigger_info.get_database_id(),
                                            trigger_info.get_ref_trg_name(), ref_trg_info));
          OV (OB_NOT_NULL(ref_trg_info));
        }
        if (OB_FAIL(ret)) {
        } else {
          if (NULL == ref_trg_info) {
            for (int64_t i = 0; OB_SUCC(ret) && i < trg_list.count(); i++) {
              OZ (schema_guard.get_trigger_info( trg_list.at(i), old_trg_info));
              OV (OB_NOT_NULL(old_trg_info));
              if (OB_SUCC(ret) && ObTriggerInfo::is_same_timing_event(trigger_info, *old_trg_info)) {
                action_order++;
              }
            }
          } else {
            bool is_follows = trigger_info.is_order_follows();
            action_order = is_follows ? ref_trg_info->get_action_order() + 1 : ref_trg_info->get_action_order();
            // ref_trg_info need to modify
            for (int64_t i = 0; OB_SUCC(ret) && i < trg_list.count(); i++) {
              OZ (schema_guard.get_trigger_info( trg_list.at(i), old_trg_info));
              OV (OB_NOT_NULL(old_trg_info));
              if (OB_SUCC(ret) && ObTriggerInfo::is_same_timing_event(trigger_info, *old_trg_info)
                  && trigger_info.get_trigger_id() != old_trg_info->get_trigger_id()
                  && ref_trg_info->get_trigger_id() != old_trg_info->get_trigger_id()) {
                  if (ref_trg_info->get_action_order() < old_trg_info->get_action_order()) {
                    new_action_order = old_trg_info->get_action_order() + 1;
                    ALTER_OLD_TRIGGER(old_trg_info);
                }
              }
            }
            if (OB_SUCC(ret) && !is_follows) {
              // if `PRECEDES`, the ref_trg_info action_order need to +1
              new_action_order = ref_trg_info->get_action_order() + 1;
              ALTER_OLD_TRIGGER(ref_trg_info);
            }
          }
        }
      }
      OX (trigger_info.set_action_order(action_order));
    } else {
      if (OB_SUCC(ret)) {
        for (int64_t i = 0; OB_SUCC(ret) && i < trg_list.count(); i++) {
          OZ (schema_guard.get_trigger_info( trg_list.at(i), old_trg_info));
          OV (OB_NOT_NULL(old_trg_info));
          if (OB_SUCC(ret) && ObTriggerInfo::is_same_timing_event(trigger_info, *old_trg_info)
              && trigger_info.get_trigger_id() != old_trg_info->get_trigger_id()
              && trigger_info.get_action_order() < old_trg_info->get_action_order()) {
            new_action_order = old_trg_info->get_action_order() - 1;
            ALTER_OLD_TRIGGER(old_trg_info);
          }
        }
      }
    }
  }
#undef ALTER_OLD_TRIGGER
  return ret;
}

int ObPLDDLService::recursive_alter_ref_trigger(share::schema::ObSchemaGetterGuard &schema_guard,
                                                ObDDLSQLTransaction &trans,
                                                ObPLDDLOperator &pl_operator,
                                                const ObTriggerInfo &ref_trigger_info,
                                                const common::ObIArray<uint64_t> &trigger_list,
                                                const ObString &trigger_name,
                                                int64_t action_order)
{
  int ret = OB_SUCCESS;
  
  const ObTriggerInfo *trg_info = NULL;
  int64_t new_action_order = 0;
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (schema_guard.get_trigger_info( trigger_list.at(i), trg_info));
    OV (OB_NOT_NULL(trg_info));
    if (0 != trg_info->get_trigger_name().case_compare(trigger_name)) {
      if (OB_SUCC(ret) && 0 == trg_info->get_ref_trg_name().case_compare(ref_trigger_info.get_trigger_name())) {
        ObTriggerInfo copy_trg_info;
        OX (new_action_order = action_order + 1);
        OZ (copy_trg_info.assign(*trg_info));
        OX (copy_trg_info.set_action_order(new_action_order));
        OZ (pl_operator.alter_trigger(copy_trg_info, trans, NULL, false/*is_update_table_schema_version*/));
        OZ (SMART_CALL(recursive_alter_ref_trigger(schema_guard,
                                                   trans,
                                                   pl_operator,
                                                   *trg_info,
                                                   trigger_list,
                                                   trigger_name,
                                                   new_action_order)));
      }
    }
  }
  return ret;
}

int ObPLDDLService::recursive_check_trigger_ref_cyclic(share::schema::ObSchemaGetterGuard &schema_guard,
                                                        const ObTriggerInfo &ref_trigger_info,
                                                        const common::ObIArray<uint64_t> &trigger_list,
                                                        const ObString &create_trigger_name,
                                                        const ObString &generate_cyclic_name)
{
  int ret = OB_SUCCESS;
  
  const ObTriggerInfo *trg_info = NULL;
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (schema_guard.get_trigger_info( trigger_list.at(i), trg_info));
    OV (OB_NOT_NULL(trg_info));
    if (OB_SUCC(ret)) {
      if (0 != trg_info->get_trigger_name().case_compare(create_trigger_name)) {
        if (0 == trg_info->get_ref_trg_name().case_compare(ref_trigger_info.get_trigger_name())) {
          if (0 == trg_info->get_trigger_name().case_compare(generate_cyclic_name)) {
            ret = OB_ERR_REF_CYCLIC_IN_TRG;
            LOG_WARN("cyclic trigger dependency is not allowed", K(ret),
                     K(generate_cyclic_name), KPC(trg_info));
          }
          OZ (SMART_CALL(recursive_check_trigger_ref_cyclic(schema_guard,
                                                            *trg_info,
                                                            trigger_list,
                                                            create_trigger_name,
                                                            generate_cyclic_name)));
        }
      }
    }
  }
  return ret;
}
int ObPLDDLService::drop_trigger_in_drop_table(ObMySQLTransaction &trans,
                                               ObDDLOperator &ddl_operator,
                                               share::schema::ObSchemaGetterGuard &schema_guard,
                                               const share::schema::ObTableSchema &table_schema,
                                               const bool to_recyclebin)
                  {
  int ret = OB_SUCCESS;
  uint64_t trigger_id = OB_INVALID_ID;
  const ObTriggerInfo *trigger_info = NULL;
  
  const ObIArray<uint64_t> &trigger_id_list = table_schema.get_trigger_list();
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int64_t i = 0; OB_SUCC(ret) && i < trigger_id_list.count(); i++) {
    OX (trigger_id = trigger_id_list.at(i));
    OZ (schema_guard.get_trigger_info( trigger_id, trigger_info), trigger_id);
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
    OV (!trigger_info->is_in_recyclebin(), OB_ERR_UNEXPECTED, trigger_id);
    if (to_recyclebin && !table_schema.is_view_table()) {
      // Only non-view table triggers are moved to the recycle bin.
      OZ (pl_operator.drop_trigger_to_recyclebin(*trigger_info, schema_guard, trans));
    } else {
      OZ (pl_operator.drop_trigger(*trigger_info,
                                   trans,
                                   NULL,
                                   true /*is_update_table_schema_version, default true*/,
                                   table_schema.get_in_offline_ddl_white_list()));
    }
  }
  return ret;
}

int ObPLDDLService::restore_trigger(const share::schema::ObTableSchema &table_schema,
                                      const uint64_t new_database_id,
                                      const common::ObString &new_table_name,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      ObMySQLTransaction &trans,
                                      ObDDLOperator &ddl_operator)
{
  int ret = OB_SUCCESS;
  
  const ObIArray<uint64_t> &trigger_id_list = table_schema.get_trigger_list();
  const ObTriggerInfo *trigger_info = NULL;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int i = 0; OB_SUCC(ret) && i < trigger_id_list.count(); i++) {
    uint64_t trigger_id = trigger_id_list.at(i);
    OZ (schema_guard.get_trigger_info( trigger_id, trigger_info), trigger_id);
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
    OZ (pl_operator.restore_trigger(*trigger_info, new_database_id, new_table_name, schema_guard, trans));
  }
  return ret;
}

int ObPLDDLService::get_object_info(ObSchemaGetterGuard &schema_guard,
                                    const ObString &object_database,
                                    const ObString &object_name,
                                    ObSchemaType &object_type,
                                    uint64_t &object_id,
                                    rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  uint64_t database_id = OB_INVALID_ID;
  const ObTableSchema *table_schema = NULL;
  if (TABLE_SCHEMA == object_type || VIEW_SCHEMA == object_type) {
    const ObTableSchema *table_schema = NULL;
    if (OB_FAIL(ddl_service.get_database_id(schema_guard, object_database, database_id))) {
    } else if (OB_FAIL(schema_guard.get_table_schema( database_id,
                                                    object_name, false, table_schema))) {
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_BAD_TABLE;
      LOG_WARN("table schema is invalid", K(ret), K(object_name), K(object_name));
    } else if (table_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("table is in recyclebin", K(ret), K(object_name), K(object_name));
    } else if (!table_schema->is_user_table() && !table_schema->is_user_view()) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "not create on user table or user view in trigger now");
      LOG_WARN("trigger only support create on user table or user view now", K(ret));
    } else {
      object_type = table_schema->is_user_table() ? TABLE_SCHEMA : VIEW_SCHEMA;
      object_id = table_schema->get_table_id();
    }
  } else if (USER_SCHEMA == object_type || DATABASE_SCHEMA == object_type) {
    const ObUserInfo *user_info = NULL;
    ObString host_name("%");
    if (OB_FAIL(schema_guard.get_user_info(object_name, host_name, user_info))) {
    } else if (OB_ISNULL(user_info)) {
      ret = OB_ERR_BAD_TABLE;
      LOG_WARN("user_info is NULL", K(ret), K(object_name));
    } else {
      object_id = user_info->get_user_id();
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("object_type is invalid", K(ret), K(object_type));
  }
  return ret;
}

int ObPLDDLService::rebuild_triggers_on_hidden_table(
                  const ObTableSchema &orig_table_schema,
                  const ObTableSchema &hidden_table_schema,
                  ObSchemaGetterGuard &runtime_schema_guard,
                  ObDDLOperator &ddl_operator,
                  ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  const ObIArray<uint64_t> &trigger_list = orig_table_schema.get_trigger_list();
  const ObTriggerInfo *trigger_info = NULL;
  ObTriggerInfo new_trigger_info;
  ObErrorInfo error_info;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  for (int i = 0; OB_SUCC(ret) && i < trigger_list.count(); i++) {
    OZ (runtime_schema_guard.get_trigger_info( trigger_list.at(i), trigger_info));
    OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_list.at(i));
    OZ (new_trigger_info.assign(*trigger_info));
    OX (new_trigger_info.set_base_object_id(hidden_table_schema.get_table_id()));
    OX (new_trigger_info.set_trigger_id(OB_INVALID_ID));
    // Preserve the original trigger database id when rebuilding on the hidden table.
    OX (new_trigger_info.set_database_id(trigger_info->get_database_id()));
    // Offline DDL drops the original trigger before creating it on the hidden table.
    OZ (pl_operator.drop_trigger(*trigger_info, trans,
      nullptr, false/*is_update_table_schema_version*/));
    if (OB_SUCC(ret)) {
      ObSEArray<ObDependencyInfo, 1> dep_infos;
      int64_t table_schema_version = OB_INVALID_VERSION;
      OZ (pl_operator.create_trigger(new_trigger_info, trans, error_info, dep_infos,
        table_schema_version, nullptr, false/*is_update_table_schema_version*/));
    }
  }
  return ret;
}

int ObPLDDLService::drop_trigger_in_drop_user(ObMySQLTransaction &trans,
                                            rootserver::ObDDLOperator &ddl_operator,
                                            ObSchemaGetterGuard &schema_guard,
                                            const uint64_t user_id)
{
  int ret = OB_SUCCESS;
  uint64_t trigger_id = OB_INVALID_ID;
  const ObTriggerInfo *trigger_info = NULL;
  const ObUserInfo *user_info = NULL;
  ObPLDDLOperator pl_operator(ddl_operator.get_multi_schema_service(), ddl_operator.get_sql_proxy());
  OZ (schema_guard.get_user_info(user_id, user_info));
  OV (OB_NOT_NULL(user_info));
  if (OB_SUCC(ret)) {
    const ObIArray<uint64_t> &trigger_id_list = user_info->get_trigger_list();
    for (int64_t i = 0; OB_SUCC(ret) && i < trigger_id_list.count(); i++) {
      OX (trigger_id = trigger_id_list.at(i));
      OZ (schema_guard.get_trigger_info( trigger_id, trigger_info), trigger_id);
      OV (OB_NOT_NULL(trigger_info), OB_ERR_UNEXPECTED, trigger_id);
      OV (!trigger_info->is_in_recyclebin(), OB_ERR_UNEXPECTED, trigger_id);
      OZ (pl_operator.drop_trigger(*trigger_info, trans, NULL));
    }
  }
  return ret;
}
template <typename ArgType>
int ObPLDDLService::check_env_before_ddl(share::schema::ObSchemaGetterGuard &schema_guard,
                                         const ArgType &arg,
                                         rootserver::ObDDLService &ddl_service)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service.check_inner_stat())) {
  } else if (OB_FAIL(ddl_service.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
  } else if (OB_FAIL(ddl_service.check_parallel_ddl_conflict(schema_guard, arg))) {
  }
  return ret;
}

} // namespace rootserver
} // namespace oceanbase
