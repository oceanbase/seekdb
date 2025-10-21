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

#ifndef OCEANBASE_SHARE_DEVICE_OB_DEVICE_MANIFEST_TASK_H_
#define OCEANBASE_SHARE_DEVICE_OB_DEVICE_MANIFEST_TASK_H_

#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "lib/task/ob_timer.h"
#include "share/object_storage/ob_object_storage_struct.h"

namespace oceanbase
{
namespace share
{
class ObZoneStorageTableInfo;
class ObZoneStorageOperationTableInfo;
class ObDeviceManifestTask : public common::ObTimerTask
{
public:
  static ObDeviceManifestTask &get_instance();
  ObDeviceManifestTask();
  virtual ~ObDeviceManifestTask() = default;
  int init(common::ObMySQLProxy *proxy);
  virtual void runTimerTask() override;
  int run();
  int try_update_new_device_configs();
  int add_new_device_configs(const ObIArray<ObZoneStorageTableInfo> &storage_infos);
  static const int64_t SCHEDULE_INTERVAL_US = 60 * 1000 * 1000L; // 60s

private:
  int do_work();
  int try_update_all_device_config();
  int try_update_next_device_config(const uint64_t last_op_id, const uint64_t last_sub_op_id);
  int check_if_need_update(const ObZoneStorageState::STATE &op_type, bool &need_update);
  int check_connectivity(const ObZoneStorageTableInfo &zone_storage_info,
                         bool &is_connective);
  int add_device_config(const ObZoneStorageTableInfo &zone_storage_info,
                        const ObZoneStorageOperationTableInfo &storage_op_info,
                        const bool is_connective);
  int add_device_config(const ObZoneStorageTableInfo &zone_storage_info,
                        const bool is_connective);
  int update_device_manifest(const ObZoneStorageOperationTableInfo &storage_op_info);
  int remove_device_config(const ObZoneStorageTableInfo &zone_storage_info,
                           const ObZoneStorageOperationTableInfo &storage_op_info);
  int update_device_config(const ObZoneStorageTableInfo &zone_storage_info,
                           const ObZoneStorageOperationTableInfo &storage_op_info,
                           const bool is_connective);

private:
  bool is_inited_;
  common::ObMySQLProxy *sql_proxy_;
  lib::ObMutex manifest_task_lock_;
};

}  // namespace share
}  // namespace oceanbase

#endif  // OCEANBASE_SHARE_DEVICE_OB_DEVICE_MANIFEST_TASK_H_
