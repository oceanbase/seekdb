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
#ifndef OCEANBASE_LOG_ROUTE_STRUCT_H_
#define OCEANBASE_LOG_ROUTE_STRUCT_H_

#include "ob_log_route_key.h"     // ObLSRouterKey
#include "ob_ls_server_list.h"    // LSSvrList
#include "ob_server_blacklist.h"  // BlackList

namespace oceanbase
{
namespace logservice
{
class ObLSRouterValue
{
public:
  ObLSRouterValue();
  ~ObLSRouterValue();
  void reset()
  {
    ls_svr_list_.reset();
    blacklist_.reset();
  }

public:
  int next_server(
      const ObLSRouterKey &router_key,
      const palf::LSN &next_lsn,
      common::ObAddr &addr);

  int get_leader(
      const ObLSRouterKey &router_key,
      common::ObAddr &leader);

  bool need_switch_server(
      const ObLSRouterKey &router_key,
      const palf::LSN &next_lsn,
      const common::ObAddr &cur_svr);

  int add_into_blacklist(
      ObLSRouterKey &router_key,
      const common::ObAddr &svr,
      const int64_t svr_service_time,
      const int64_t blacklist_survival_time_sec,
      const int64_t blacklist_survival_time_upper_limit_min,
      const int64_t blacklist_survival_time_penalty_period_min,
      const int64_t blacklist_history_overdue_time,
      const int64_t blacklist_history_clear_interval,
      int64_t &survival_time);

  int get_server_array_for_locate_start_lsn(ObIArray<common::ObAddr> &svr_array);

  void refresh_ls_svr_list(const LSSvrList &svr_list);

  LSSvrList &get_ls_svr_list() { return ls_svr_list_; }

  int64_t get_server_count() const { return ls_svr_list_.count(); }

  TO_STRING_KV(K_(ls_svr_list),
      K_(blacklist));

private:
  common::ObByteLock lock_;
  LSSvrList ls_svr_list_;
  BlackList blacklist_;
};

// Router Asynchronous Task
struct ObLSRouterAsynTask
{
  ObLSRouterAsynTask() { reset(); }
  ~ObLSRouterAsynTask() { reset(); }

  void reset()
  {
    router_key_.reset();
  }

  ObLSRouterKey router_key_;

  TO_STRING_KV(K_(router_key));
};

} // namespace logservice
} // namespace oceanbase

#endif
