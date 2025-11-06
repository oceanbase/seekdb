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

#ifndef OCEANBASE_SHARE_OB_BACKUP_DATA_SERVER_MGR_H_
#define OCEANBASE_SHARE_OB_BACKUP_DATA_SERVER_MGR_H_

#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "lib/container/ob_iarray.h"
#include "share/ob_server_table_operator.h"
#include "share/ob_unit_table_operator.h"
#include "lib/lock/ob_mutex.h"
namespace oceanbase
{

namespace share
{

class ObBackupServerMgr final
{
public:
  ObBackupServerMgr();
  ~ObBackupServerMgr() {}
  int init(const uint64_t tenant_id, common::ObMySQLProxy &sql_proxy);
  int get_alive_servers(
      const bool force_update, const common::ObZone &zone, common::ObIArray<common::ObAddr> &server_list);
  int get_alive_servers(const bool force_update, common::ObIArray<common::ObAddr> &server_list);
  int get_zone_list(const bool force_update, ObIArray<common::ObZone> &zone_list);

  int get_server_status(const common::ObAddr &server, const bool force_update, ObServerStatus &server_status);
  int is_server_exist(const common::ObAddr &server, const bool force_update, bool &exist);
private:
  int find_(const ObAddr &server, const ObServerStatus *&status) const;
  int update_zone_();
  int update_server_();
private:
  bool is_inited_; 
  lib::ObMutex mtx_;

  uint64_t tenant_id_;
  ObServerTableOperator server_op_;
  share::ObUnitTableOperator unit_op_;
  ObArray<share::ObServerStatus> server_status_array_;
  ObArray<common::ObZone> zone_array_;
  DISALLOW_COPY_AND_ASSIGN(ObBackupServerMgr);
};

}//end namespace share
}//end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_BACKUP_DATA_SERVER_MGR_H_
