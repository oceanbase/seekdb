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

#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_MDS_TENANT_SERVICE_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_MDS_TENANT_SERVICE_H

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string_holder.h"
#include "lib/task/ob_timer.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/allocator/ob_vslice_alloc.h"
#include "share/ob_occam_timer.h"
#include "storage/allocator/ob_mds_allocator.h"
#include "lib/hash/ob_linear_hash_map.h"

namespace oceanbase
{
namespace share
{
class SCN;
}
namespace storage
{
class ObLS;
class ObTablet;
class ObTabletHandle;
namespace mds
{
class MdsWriter;
class MdsTableHandle;
class ObTenantMdsService;


class ObTenantMdsService
{
public:
  ObTenantMdsService() : is_inited_(false),
                         recyle_timer_task_(*this),
                         recyle_timer_(),
                         dump_status_timer_() {}
  ~ObTenantMdsService() {
    recyle_timer_.destroy();
    dump_status_timer_.destroy();
    MDS_LOG_RET(INFO, OB_SUCCESS, "ObTenantMdsAllocator destructed");
  }
  static int mtl_init(ObTenantMdsService* &);
  static int mtl_start(ObTenantMdsService* &);
  static void mtl_stop(ObTenantMdsService* &);
  static void mtl_wait(ObTenantMdsService* &);
  void destroy() { this->~ObTenantMdsService(); }
  share::ObTenantBufferCtxAllocator &get_buffer_ctx_allocator() { return buffer_ctx_allocator_; }
  TO_STRING_KV(KP(this), K_(is_inited))
public:
public:
  void run_recyle_timer_task();
  static void run_dump_status_timer_task();

private:
  static void try_recycle_mds_table_task();
  static void dump_special_mds_table_status_task();
  static int for_each_mds_table_(ObLS &ls, const ObFunction<int(ObTablet &)> &op);

  static int process_with_tablet_(ObTablet &tablet);
  static int get_tablet_oldest_scn_(ObTablet &tablet, share::SCN &oldest_scn);
  static int try_recycle_mds_table_(ObTablet &tablet, const share::SCN &recycle_scn);
  static int try_gc_mds_table_(ObTablet &tablet);

  class RecyleTimerTask : public common::ObTimerTask
  {
  public:
    RecyleTimerTask(ObTenantMdsService &service) : service_(service) {}
    virtual ~RecyleTimerTask() = default;

    void runTimerTask() override { service_.run_recyle_timer_task(); }
  private:
    ObTenantMdsService &service_;
  };

  class DumpStatusTimerTask : public common::ObTimerTask
  {
  public:
    DumpStatusTimerTask() = default;
    virtual ~DumpStatusTimerTask() = default;

    void runTimerTask() override { ObTenantMdsService::run_dump_status_timer_task(); }
  };

private:
  bool is_inited_;
  share::ObTenantBufferCtxAllocator buffer_ctx_allocator_;
  RecyleTimerTask recyle_timer_task_;
  DumpStatusTimerTask dump_status_timer_task_;

  common::ObTimer recyle_timer_;
  common::ObTimer dump_status_timer_;
};

}  // namespace mds
}  // namespace storage
}  // namespace oceanbase


#endif
