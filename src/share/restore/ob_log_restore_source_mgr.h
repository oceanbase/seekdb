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

#ifndef OCEANBASE_SHARE_OB_LOG_RESTORE_SOURCE_MGR_H_
#define OCEANBASE_SHARE_OB_LOG_RESTORE_SOURCE_MGR_H_
#include "lib/ob_define.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "share/backup/ob_backup_struct.h"
#include "ob_log_restore_source.h"
#include "share/ob_kv_storage.h"
#include "share/ob_server_struct.h"  // GCTX

namespace oceanbase
{
namespace common
{
class ObString;
class ObAddr;
}

namespace share
{
typedef common::ObSEArray<ObBackupPathString, 1> DirArray;
// For standby and restore tenant, set log source with the log archive destination explicitly.
class ObLogRestoreSourceMgr final
{
public:
  ObLogRestoreSourceMgr() : is_inited_(false), tenant_id_(OB_INVALID_TENANT_ID) {}
public:
  // Simplified init for single tenant/single LS scenario - no SQL proxy needed
  int init(const uint64_t tenant_id);
public:
  // add source with net service
  int add_service_source(const SCN &recovery_until_scn, const ObString &service_source);
  // add source with archive dest
  // 1. nfs example
  // file:///data/1/
  // 2. oss example
  // oss://backup_dir/?host=xxx.com&access_id=111&access_key=222
  // 3. cos example
  int add_location_source(const SCN &recovery_until_scn, const ObString &archive_dest);
  // add source with raw pieces
  int add_rawpath_source(const SCN &recovery_until_scn, const DirArray &array);

  // modify log restore source recovery until scn
  int update_recovery_until_scn(const SCN &recovery_until_scn);

  // delete all log restore source
  int delete_source();

  // get log restore source
  int get_source(ObLogRestoreSourceItem &item);
  
  // Removed get_source_for_update - not needed for KV storage

  static int get_backup_dest(const ObLogRestoreSourceItem &item, ObBackupDest& dest);
private:
  const int64_t OB_DEFAULT_LOG_RESTORE_SOURCE_ID = 1;

  // Helper methods for KV storage
  static int get_kv_key_(const uint64_t tenant_id, ObString &key);
  static int serialize_to_kv_(const ObLogRestoreSourceItem &item, ObString &value, common::ObIAllocator &allocator);
  static int deserialize_from_kv_(const ObString &value, ObLogRestoreSourceItem &item);

private:
  bool is_inited_;
  uint64_t tenant_id_;       // user tenant id
private:
  DISALLOW_COPY_AND_ASSIGN(ObLogRestoreSourceMgr);
};
} // namespace share
} // namespace oceanbase
#endif /* OCEANBASE_SHARE_OB_LOG_RESTORE_SOURCE_MGR_H_ */
