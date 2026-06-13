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

#ifndef OCEANBASE_STORAGE_TENANT_RESTORE_INFO_MGR_H_
#define OCEANBASE_STORAGE_TENANT_RESTORE_INFO_MGR_H_

#include "lib/lock/ob_mutex.h"
#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace storage
{

class ObTenantRestoreInfoMgr final
{
public:
  ObTenantRestoreInfoMgr();
  ~ObTenantRestoreInfoMgr();

  static int mtl_init(ObTenantRestoreInfoMgr *&restore_info_mgr);
  int init(const uint64_t tenant_id);
  int start();
  void wait();
  void stop();
  void destroy();

  int refresh_restore_info();
  bool is_refreshed() const { return is_refreshed_; }
  int get_restore_dest_id(int64_t &dest_id);

private:
  void set_refreshed_() { is_refreshed_ = true; }

private:
  class RestoreInfoRefresher : public common::ObTimerTask
  {
  public:
    RestoreInfoRefresher(ObTenantRestoreInfoMgr &mgr) : mgr_(mgr) {}
    virtual ~RestoreInfoRefresher() {}
    virtual void runTimerTask();
  private:
    ObTenantRestoreInfoMgr &mgr_;
  };

private:
  static constexpr const int64_t REFRESH_INFO_INTERVAL = 5 * 1000L * 1000L; //5s

private:
  bool is_inited_;
  lib::ObMutex mutex_;
  RestoreInfoRefresher refresh_info_task_;
  bool is_refreshed_;
  uint64_t tenant_id_;
  int64_t restore_job_id_;
  int64_t dest_id_;
};

}
}

#endif
