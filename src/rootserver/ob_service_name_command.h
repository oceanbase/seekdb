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

#ifndef OCEANBASE_ROOTSERVER_OB_SERVICE_NAME_COMMAND_H
#define OCEANBASE_ROOTSERVER_OB_SERVICE_NAME_COMMAND_H

#include "lib/ob_define.h"
#include "share/ob_define.h"
#include "share/ob_service_name_proxy.h"
#include "sql/session/ob_sql_session_mgr.h"
namespace oceanbase
{
namespace rootserver
{
class ObServiceNameKillSessionFunctor
{
public:
    ObServiceNameKillSessionFunctor()
        : tenant_id_(OB_INVALID_TENANT_ID), service_name_(), killed_connection_list_(NULL) {};
    ~ObServiceNameKillSessionFunctor() {};
    int init(const uint64_t tenant_id,
    const share::ObServiceNameString &service_name,
    ObArray<uint64_t> *killed_connection_list);
    bool operator()(sql::ObSQLSessionMgr::Key key, sql::ObSQLSessionInfo *sess_info);
private:
    uint64_t tenant_id_;
    ObServiceNameString service_name_;
    ObArray<uint64_t> *killed_connection_list_;
};
class ObServiceNameCommand
{
public:
  ObServiceNameCommand();
  ~ObServiceNameCommand();
  static int create_service(
      const uint64_t tenant_id,
      const share::ObServiceNameString &service_name_str);
  static int delete_service(
      const uint64_t tenant_id,
      const share::ObServiceNameString &service_name_str);
  static int start_service(
      const uint64_t tenant_id,
      const share::ObServiceNameString &service_name_str);
  static int stop_service(
      const uint64_t tenant_id,
      const share::ObServiceNameString &service_name_str);
  static int kill_local_connections(
    const uint64_t tenant_id,
    const share::ObServiceName &service_name);
private:
  static int check_and_get_tenants_servers_(
      const uint64_t tenant_id,
      const bool include_temp_offline,
      common::ObIArray<common::ObAddr> &target_servers);
  static int server_check_and_push_back_(
      const common::ObAddr &server,
      const bool include_temp_offline,
      common::ObIArray<common::ObAddr> &target_servers);
  static int broadcast_refresh_(
      const uint64_t tenant_id,
      const share::ObServiceNameID &target_service_name_id,
      const share::ObServiceNameArg::ObServiceOp &service_op,
      const common::ObIArray<common::ObAddr> &target_servers,
      const int64_t epoch,
      const ObArray<share::ObServiceName> &all_service_names);
  static int extract_service_name_(
      const ObArray<share::ObServiceName> &all_service_names,
      const share::ObServiceNameString &service_name_str,
      share::ObServiceName &service_name);
};
} // end namespace rootserver
} // end namespace oceanbase
#endif
