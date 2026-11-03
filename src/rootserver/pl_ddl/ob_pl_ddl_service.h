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

#ifndef _OCEANBASE_ROOTSERVER_OB_PL_DDL_SERVICE_H_
#define _OCEANBASE_ROOTSERVER_OB_PL_DDL_SERVICE_H_

#include "rootserver/ob_ddl_service.h"
#include "ob_pl_ddl_operator.h"
#include "rootserver/ob_root_utils.h" // for RS_TRACE
#include "share/ob_rpc_struct.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_dependency_info.h"
#include <string>
#include <memory>

namespace oceanbase
{
namespace share { namespace plugin {
class IExtensionCatalogInstaller;
class IExtensionSchemaInstaller;
struct ExtensionInstallSpec;
class IExtensionCatalogDropper;
struct ExtensionDropRequest;
class IExtensionCatalogUpdater;
class IExtensionSchemaUpdater;
struct ExtensionUpdateRequest;
struct ExtensionRoutineUpdateOperation;
class IExtensionRoutineScript;
} }
using namespace obcall;
using namespace share;

namespace rootserver
{
class NativeRoutineAclVersionReservation;
class ObDDLSQLTransaction;
class ObDDLService;
class ObDDLOperator;
class RoutineIdReservation;

class ObPLDDLService
{
public:
  // Same Root updater used by the catalog coordinator; borrowed inputs must
  // outlive it. Host-only, not authorization or transaction ownership.
  static std::unique_ptr<share::plugin::IExtensionSchemaUpdater> make_routine_extension_updater(
      const ObIArray<share::plugin::ExtensionRoutineUpdateOperation> &operations,
      const ObSessionPrivInfo &priv, const ObIArray<uint64_t> &roles,
      share::schema::ObSchemaGetterGuard &guard, ObDDLService &ddl,
      ObDDLSQLTransaction &transaction, share::plugin::IExtensionRoutineScript *script);
  //----Functions for managing routine----
  // With external_trans, only writes schema/dependencies/automatic privileges;
  // never starts, ends, or publishes that transaction. Returned ID is provisional.
  static int create_routine(const obcall::ObCreateRoutineArg &arg,
                            rootserver::ObDDLService &ddl_service,
                            ObDDLSQLTransaction *external_trans = nullptr,
                            uint64_t *created_routine_id = nullptr);
  // Core bridge for a resolved pure-SQL routine package. Not a SQL parser or
  // native-plugin entry point. session_priv/roles MUST come from the authenticated
  // session, and arg must have passed the ordinary SQL resolver/definer checks.
  // Installation success and post-commit publication status are separate: a
  // failed publication must never cause retry of an already committed install.
  static int install_routine_extension(
      const share::plugin::ExtensionInstallSpec &spec,
      const obcall::ObCreateRoutineArg &arg,
      const share::schema::ObSessionPrivInfo &session_priv,
      const common::ObIArray<uint64_t> &enabled_roles,
      share::plugin::IExtensionCatalogInstaller &catalog,
      ObDDLService &ddl_service,
      uint64_t &extension_id, int &publication_status, std::string &error);
  // All resolved routines share ONE installation/DDL transaction and schema
  // publication. Input pointers remain live throughout the synchronous call.
  // Alternatively a script and empty args use the evolving transaction view.
  static int install_routines_extension(
      const share::plugin::ExtensionInstallSpec &spec,
      const common::ObIArray<const obcall::ObCreateRoutineArg *> &args,
      const share::schema::ObSessionPrivInfo &session_priv,
      const common::ObIArray<uint64_t> &enabled_roles,
      share::plugin::IExtensionCatalogInstaller &catalog,
      ObDDLService &ddl_service,
      uint64_t &extension_id, int &publication_status, std::string &error,
      share::plugin::IExtensionRoutineScript *script = nullptr);
  // Core-only adapter shared by the Root entry and controlled-transport tests.
  // Caller holds Root serialization and keeps all borrowed services, arguments
  // and authenticated identity alive. The returned adapter never owns commit
  // or schema publication; use the catalog installation coordinator to run it.
  static std::unique_ptr<share::plugin::IExtensionSchemaInstaller> make_routine_extension_installer(
      const common::ObIArray<const obcall::ObCreateRoutineArg *> &args,
      const share::schema::ObSessionPrivInfo &session_priv,
      const common::ObIArray<uint64_t> &enabled_roles,
      share::schema::ObSchemaGetterGuard &guard, ObDDLService &ddl_service,
      ObDDLSQLTransaction &transaction, share::plugin::IExtensionRoutineScript *script = nullptr);
  // Same external-transaction ownership as create_routine, including the MySQL
  // alter-via-replacement branch. This entry resolves existing published objects;
  // an update adapter must separately handle provisional script objects.
  static int alter_routine(const obcall::ObCreateRoutineArg &arg,
                           rootserver::ObDDLService &ddl_service,
                           ObDDLSQLTransaction *external_trans = nullptr);
  // Core-only resolved script bridge. Caller holds Root serialization, supplies
  // authenticated privileges and keeps the complete ordered arguments alive.
  // A host-bound script alternatively supplies sequential semantic callbacks;
  // operations must then be empty. No public plugin authority or second catalog.
  static int update_routines_extension(
      const share::plugin::ExtensionUpdateRequest &request,
      const common::ObIArray<share::plugin::ExtensionRoutineUpdateOperation> &operations,
      const share::schema::ObSessionPrivInfo &session_priv,
      const common::ObIArray<uint64_t> &enabled_roles,
      share::plugin::IExtensionCatalogUpdater &catalog, ObDDLService &ddl_service,
      uint64_t &extension_id, bool &changed, int &publication_status, std::string &error,
      share::plugin::IExtensionRoutineScript *script = nullptr);
  // Caller holds Root DDL serialization and supplies authenticated privileges.
  // This initial schema adapter supports pure-SQL routine members and RESTRICT.
  // No source files/native module are needed to remove an installed package.
  static int drop_routines_extension(
      const share::plugin::ExtensionDropRequest &request,
      const share::schema::ObSessionPrivInfo &session_priv,
      const common::ObIArray<uint64_t> &enabled_roles,
      share::plugin::IExtensionCatalogDropper &catalog, ObDDLService &ddl_service,
      uint64_t &dropped_extension_id, int &publication_status, std::string &error);
  static int drop_routine(const ObDropRoutineArg &arg,
                          rootserver::ObDDLService &ddl_service);
  //----End of functions for managing routine----


  //----Functions for managing package----
  static int create_package(const obcall::ObCreatePackageArg &arg,
                            rootserver::ObDDLService &ddl_service);
  static int drop_package(const obcall::ObDropPackageArg &arg,
                          rootserver::ObDDLService &ddl_service);
  //----End of functions for managing package----

  //----Functions for managing trigger----
  static int create_trigger(const obcall::ObCreateTriggerArg &arg,
                            obcall::ObCreateTriggerRes *res,
                            rootserver::ObDDLService &ddl_service);
  static int alter_trigger(const obcall::ObAlterTriggerArg &arg,
                           rootserver::ObDDLService &ddl_service);
  static int drop_trigger(const obcall::ObDropTriggerArg &arg,
                          rootserver::ObDDLService &ddl_service);
  static int drop_trigger_in_drop_table(ObMySQLTransaction &trans,
                                        rootserver::ObDDLOperator &ddl_operator,
                                        share::schema::ObSchemaGetterGuard &schema_guard,
                                        const share::schema::ObTableSchema &table_schema,
                                        const bool to_recyclebin);
  static int drop_trigger_in_drop_user(ObMySQLTransaction &trans,
                                      rootserver::ObDDLOperator &ddl_operator,
                                      ObSchemaGetterGuard &schema_guard,
                                      const uint64_t user_id);
  static int rebuild_triggers_on_hidden_table(const ObTableSchema &orig_table_schema,
                                              const ObTableSchema &hidden_table_schema,
                                              ObSchemaGetterGuard &runtime_schema_guard,
                                              rootserver::ObDDLOperator &ddl_operator,
                                              ObMySQLTransaction &trans);
  static int rebuild_trigger_on_rename(share::schema::ObSchemaGetterGuard &schema_guard,
                                       const share::schema::ObTableSchema &table_schema,
                                       rootserver::ObDDLOperator &ddl_operator,
                                       ObMySQLTransaction &trans);
  static int rebuild_trigger_on_rename(share::schema::ObSchemaGetterGuard &schema_guard,
                                       const common::ObIArray<uint64_t> &trigger_list,
                                       const common::ObString &database_name,
                                       const common::ObString &table_name,
                                       rootserver::ObDDLOperator &ddl_operator,
                                       ObMySQLTransaction &trans);
  static int create_trigger_for_truncate_table(share::schema::ObSchemaGetterGuard &schema_guard,
                                               const common::ObIArray<uint64_t> &origin_trigger_list,
                                               share::schema::ObTableSchema &new_table_schema,
                                               rootserver::ObDDLOperator &ddl_operator,
                                               ObMySQLTransaction &trans);
  static int restore_trigger(const share::schema::ObTableSchema &table_schema,
                               const uint64_t new_database_id,
                               const common::ObString &new_table_name,
                               share::schema::ObSchemaGetterGuard &schema_guard,
                               ObMySQLTransaction &trans,
                               rootserver::ObDDLOperator &ddl_operator);
  //----End of functions for managing trigger----
private:
  template <typename ArgType>
  static int check_env_before_ddl(share::schema::ObSchemaGetterGuard &schema_guard,
                                  const ArgType &arg,
                                  rootserver::ObDDLService &ddl_service);
  //----Functions for managing routine----
  static int create_routine(ObRoutineInfo &routine_info,
                            const ObRoutineInfo* old_routine_info,
                            bool replace,
                            ObErrorInfo &error_info,
                            ObIArray<ObDependencyInfo> &dep_infos,
                            const ObString *ddl_stmt_str,
                            share::schema::ObSchemaGetterGuard &schema_guard,
                            rootserver::ObDDLService &ddl_service,
                            ObDDLSQLTransaction *external_trans = nullptr,
                            RoutineIdReservation *reservation = nullptr,
                            RoutineVersionReservation *version_reservation = nullptr,
                            NativeRoutineAclVersionReservation *owner_grant = nullptr);
  static int alter_routine(const ObRoutineInfo &routine_info,
                           ObErrorInfo &error_info,
                           const ObString *ddl_stmt_str,
                           share::schema::ObSchemaGetterGuard &schema_guard,
                           rootserver::ObDDLService &ddl_service,
                           ObDDLSQLTransaction *external_trans = nullptr);
  static int drop_routine(const ObRoutineInfo &routine_info,
                          ObErrorInfo &error_info,
                          const ObString *ddl_stmt_str,
                          share::schema::ObSchemaGetterGuard &schema_guard,
                          rootserver::ObDDLService &ddl_service,
                          ObDDLSQLTransaction *external_trans = nullptr,
                          RoutineVersionReservation *version_reservation = nullptr);
  //----End of functions for managing routine----

  //----Functions for managing package----
  static int create_package(ObSchemaGetterGuard &schema_guard,
                            const ObPackageInfo *old_package_info,
                            ObPackageInfo &new_package_info,
                            ObIArray<ObRoutineInfo> &public_routine_infos,
                            ObErrorInfo &error_info,
                            ObIArray<ObDependencyInfo> &dep_infos,
                            const ObString *ddl_stmt_str,
                            rootserver::ObDDLService &ddl_service);
  static int drop_package(ObSchemaGetterGuard &schema_guard,
                          const ObPackageInfo &package_info,
                          ObErrorInfo &error_info,
                          const ObString *ddl_stmt_str,
                          rootserver::ObDDLService &ddl_service);
  //----Functions for managing trigger----
  static int create_trigger(const obcall::ObCreateTriggerArg &arg,
                            ObSchemaGetterGuard &schema_guard,
                            obcall::ObCreateTriggerRes *res,
                            rootserver::ObDDLService &ddl_service);
  static int create_trigger_in_trans(share::schema::ObTriggerInfo &trigger_info,
                                      share::schema::ObErrorInfo &error_info,
                                      ObIArray<ObDependencyInfo> &dep_infos,
                                      const common::ObString *ddl_stmt_str,
                                      bool in_second_stage,
                                      share::schema::ObSchemaGetterGuard &schema_guard,
                                      int64_t &table_schema_version,
                                      rootserver::ObDDLService &ddl_service);
  static int drop_trigger_in_trans(const share::schema::ObTriggerInfo &trigger_info,
                                    const common::ObString *ddl_stmt_str,
                                    share::schema::ObSchemaGetterGuard &schema_guard,
                                    rootserver::ObDDLService &ddl_service);
  static int try_get_exist_trigger(share::schema::ObSchemaGetterGuard &schema_guard,
                                    const share::schema::ObTriggerInfo &new_trigger_info,
                                    const share::schema::ObTriggerInfo *&old_trigger_info,
                                    bool with_replace);
  static int adjust_trigger_action_order(share::schema::ObSchemaGetterGuard &schema_guard,
                                          rootserver::ObDDLSQLTransaction &trans,
                                          ObPLDDLOperator &pl_operator,
                                          ObTriggerInfo &trigger_info,
                                          bool is_create_trigger);
  static int recursive_alter_ref_trigger(share::schema::ObSchemaGetterGuard &schema_guard,
                                          rootserver::ObDDLSQLTransaction &trans,
                                          ObPLDDLOperator &pl_operator,
                                          const ObTriggerInfo &ref_trigger_info,
                                          const common::ObIArray<uint64_t> &trigger_list,
                                          const ObString &trigger_name,
                                          int64_t action_order);
  static int recursive_check_trigger_ref_cyclic(share::schema::ObSchemaGetterGuard &schema_guard,
                                                const ObTriggerInfo &ref_trigger_info,
                                                const common::ObIArray<uint64_t> &trigger_list,
                                                const ObString &create_trigger_name,
                                                const ObString &generate_cyclic_name);
  static int get_object_info(ObSchemaGetterGuard &schema_guard,
                             const ObString &object_database,
                             const ObString &object_name,
                             ObSchemaType &object_type,
                             uint64_t &object_id,
                             rootserver::ObDDLService &ddl_service);
  //----End of functions for managing trigger----

};

} // namespace rootserver
} // namespace oceanbase

#endif // _OCEANBASE_ROOTSERVER_OB_PL_DDL_SERVICE_H_
