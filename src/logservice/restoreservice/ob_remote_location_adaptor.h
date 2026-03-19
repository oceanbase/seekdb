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
#ifndef OCEANBASE_LOGSERVICE_OB_REMOTE_LOCATION_ADAPTOR_H_
#define OCEANBASE_LOGSERVICE_OB_REMOTE_LOCATION_ADAPTOR_H_

#include <cstdint>
#include "lib/utility/ob_macro_utils.h"
namespace oceanbase
{
namespace common
{
class ObAddr;
}
namespace share
{
class ObLSID;
struct ObLogRestoreSourceItem;
class ObBackupDest;
}
namespace storage
{
class ObLS;
class ObLSService;
}
namespace logservice
{
class ObLogRestoreHandler;
class ObLogRestoreNetDriver;
class ObRemoteLocationAdaptor
{
public:
  ObRemoteLocationAdaptor();
  ~ObRemoteLocationAdaptor();
public:
  int init(const uint64_t tenant_id, storage::ObLSService *ls_svr, ObLogRestoreNetDriver *net_driver);
  void destroy();
  int update_upstream(share::ObLogRestoreSourceItem &source, bool &source_exist);

private:
  bool is_tenant_primary_();
  int do_update_(const bool is_add_source, const share::ObLogRestoreSourceItem &item);
  int get_source_(share::ObLogRestoreSourceItem &item, bool &exist);
  int check_replica_status_(storage::ObLS &ls, bool &need_update);
  int clean_source_(ObLogRestoreHandler &restore_handler);
  int add_source_(const share::ObLogRestoreSourceItem &item, ObLogRestoreHandler &restore_handler);
  int add_service_source_(const share::ObLogRestoreSourceItem &item, ObLogRestoreHandler &restore_handler);
  int add_location_source_(const share::ObLogRestoreSourceItem &item, ObLogRestoreHandler &restore_handler);
  int add_rawpath_source_(const share::ObLogRestoreSourceItem &item, ObLogRestoreHandler &restore_handler);

private:
  static const int64_t LOCATION_REFRESH_INTERVAL = 5 * 1000 * 1000L;
private:
  bool inited_;
  uint64_t tenant_id_;
  int64_t last_refresh_ts_;
  storage::ObLSService *ls_svr_;
  ObLogRestoreNetDriver *net_driver_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObRemoteLocationAdaptor);
};
} // namespace logservice
} // namespace oceanbase

#endif /* OCEANBASE_LOGSERVICE_OB_REMOTE_LOCATION_ADAPTOR_H_ */
