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

#ifndef OCEANBASE_ROOTSERVER_OB_BACKUP_SERVICE_PROXY_H_
#define OCEANBASE_ROOTSERVER_OB_BACKUP_SERVICE_PROXY_H_

namespace oceanbase
{
namespace obrpc
{
struct ObBackupDatabaseArg;
struct ObBackupCleanArg;
struct ObDeletePolicyArg;
struct ObBackupCleanArg;
struct ObArchiveLogArg;
struct ObBackupManageArg;
struct ObArchiveLogArg;
}

namespace rootserver
{

class ObBackupServiceProxy
{
public:
  static int handle_backup_database(const obrpc::ObBackupDatabaseArg &arg);
  static int handle_backup_database_cancel(const obrpc::ObBackupManageArg &arg);
  static int handle_backup_delete(const obrpc::ObBackupCleanArg &arg);
  static int handle_delete_policy(const obrpc::ObDeletePolicyArg &arg);
  static int handle_backup_delete_obsolete(const obrpc::ObBackupCleanArg &arg);
  static int handle_archive_log(const obrpc::ObArchiveLogArg &arg);
};

}
}

#endif
