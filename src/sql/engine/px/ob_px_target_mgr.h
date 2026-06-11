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

#ifndef __SQL_ENG_PX_TARGET_MGR_H__
#define __SQL_ENG_PX_TARGET_MGR_H__

#include "share/ob_thread_pool.h"
#include "share/ob_define.h"
#include "ob_px_tenant_target_monitor.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/hash/ob_link_hashmap.h"
#include "ob_px_rpc_proxy.h"

namespace oceanbase
{
using namespace share;
namespace sql
{
typedef common::ObIntWarp ObPxTenantInfo;
typedef common::LinkHashNode<ObPxTenantInfo> ObPxTenantInfoNode;
typedef common::LinkHashValue<ObPxTenantInfo> ObPxTenantInfoValue;

class ObPxResInfo : public ObPxTenantInfoValue
{
public:
  ObPxResInfo() {}
  ~ObPxResInfo() {}
  void destroy();
  ObPxTenantTargetMonitor* get_target_monitor() { return &target_monitor_; }
private:
  bool is_inited_;
  ObPxTenantTargetMonitor target_monitor_;
};

class ObPxInfoAlloc
{
public:
  static ObPxResInfo *alloc_value() { return NULL; }
  static void free_value(ObPxResInfo *info)
  {
    if (NULL != info) {
      info->~ObPxResInfo();
      ob_free(info);
      info = NULL;
    }
  }
  static ObPxTenantInfoNode *alloc_node(ObPxResInfo *p)
  {
    UNUSED(p);
    return OB_NEW(ObPxTenantInfoNode, "PxTargetMgr");
  }
  static void free_node(ObPxTenantInfoNode *node)
  {
    if (NULL != node) {
      OB_DELETE(ObPxTenantInfoNode, "PxTargetMgr", node);
      node = NULL;
    }
  }
};

typedef common::ObLinkHashMap<ObPxTenantInfo, ObPxResInfo, ObPxInfoAlloc> ObPxInfoMap;

class ObPxGlobalResGather
{
public:
  ObPxGlobalResGather(ObPxRpcFetchStatResponse &result) : result_(result) {}
  ~ObPxGlobalResGather() {}
  ObPxRpcFetchStatResponse &result_;
};

class ObPxTargetMgr
{

#define PX_MAX_ALIVE_SERVER_NUM (2000)

public:
  ObPxTargetMgr() { reset(); }
  ~ObPxTargetMgr() { destroy(); }
  int init(const common::ObAddr &server);
  void reset();
  int start();
  void stop();
  void wait();
  void destroy();
public:
  static ObPxTargetMgr &get_instance();

  // for target_mgr or monitor
  int add_tenant(uint64_t tenant_id);
  int delete_tenant(uint64_t tenant_id);
  int set_parallel_servers_target(uint64_t tenant_id, int64_t parallel_servers_target);
  int get_parallel_servers_target(uint64_t tenant_id, int64_t &parallel_servers_target);
  int get_parallel_session_count(uint64_t tenant_id, int64_t &parallel_session_count);

  // for rpc
  int is_leader(uint64_t tenant_id,  bool &is_leader);
  int get_version(uint64_t tenant_id, uint64_t &version);
  int update_peer_target_used(uint64_t tenant_id, const ObAddr &server, int64_t peer_used, uint64_t version);
  int gather_global_target_usage(uint64_t tenant_id, ObPxGlobalResGather &gather);
  int reset_leader_statistics(uint64_t tenant_id);
  
  // for px_admission
  int apply_target(uint64_t tenant_id, hash::ObHashMap<ObAddr, int64_t> &worker_map,
                   int64_t wait_time_us, int64_t session_target, int64_t req_cnt,
                   int64_t &admit_count, uint64_t &admit_version);
  int release_target(uint64_t tenant_id, hash::ObHashMap<ObAddr, int64_t> &worker_map, uint64_t admit_version);

  // for virtual_table iter
  int get_all_tenant(common::ObSEArray<uint64_t, 4> &tenant_array);
  int get_all_target_info(uint64_t tenant_id, common::ObIArray<ObPxTargetInfo> &target_info_array);

private:
  bool is_inited_;
  bool is_running_;
  common::ObAddr server_;
  ObPxInfoMap px_info_map_; // If considering deleting tenant, need to add lock
  hash::ObHashSet<ObAddr> alive_server_set_;
};

#define OB_PX_TARGET_MGR (::oceanbase::sql::ObPxTargetMgr::get_instance())

}
}

#endif /* __SQL_ENG_PX_RESOURCE_MGR_H__ */
//// end of header file
