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

#ifndef OCEANBASE_SHARE_OB_TIME_ZONE_INFO_MGR_H
#define OCEANBASE_SHARE_OB_TIME_ZONE_INFO_MGR_H

#include "lib/hash/ob_hashmap.h"
#include "lib/thread/ob_simple_thread_pool.h"
#include "lib/net/ob_addr.h"
#include "lib/timezone/ob_timezone_info.h"
namespace oceanbase
{
namespace rootserver
{
class ObRootService;
}
namespace common
{
class ObISQLClient;
namespace sqlclient
{
class ObMySQLResult;
}
class ObMySQLProxy;
}
namespace obrpc
{
class ObCommonRpcProxy;
}
namespace common
{

class ObRequestTZInfoArg
{
  OB_UNIS_VERSION(1);
public:
  explicit ObRequestTZInfoArg(const common::ObAddr &addr, uint64_t tenant_id) : obs_addr_(addr),
  tenant_id_(tenant_id) {}
  ObRequestTZInfoArg() : obs_addr_() {}
  ~ObRequestTZInfoArg() {}
public:
  common::ObAddr obs_addr_;
  uint64_t tenant_id_;
};

class ObRequestTZInfoResult
{
  OB_UNIS_VERSION(1);
public:
  ObRequestTZInfoResult()
      :last_version_(-1),
      tz_array_()
  {
  }
  ~ObRequestTZInfoResult() {}
  TO_STRING_KV(K_(last_version), K_(tz_array));
public:
  int64_t last_version_;
  common::ObSArray<ObTimeZoneInfoPos> tz_array_;
};


class ObTZAbbrIDStruct;
class ObTZAbbrNameStruct;
class ObTimeZoneInfoManager
{
  const int64_t TZ_INFO_BUCKET_NUM = 600;
  const int64_t TASK_THREAD_NUM = 1;
  const int64_t TASK_NUM_LIMIT = 512;
  static const char *UPDATE_TZ_INFO_VERSION_SQL;
private:
  class TaskProcessThread : public common::ObSimpleThreadPool
  {
  public:
    virtual void handle(void *task);
  };

  class TZInfoTask
  {
  public:
  explicit TZInfoTask(ObTimeZoneInfoManager &tz_mgr) : tz_mgr_(tz_mgr) {}
    virtual ~TZInfoTask() {}
    virtual int run_task() = 0;
  protected:
    ObTimeZoneInfoManager &tz_mgr_;
  private:
    DISALLOW_COPY_AND_ASSIGN(TZInfoTask);
  };

  class FillRequestTZInfoResult
  {
  public:
    FillRequestTZInfoResult(ObRequestTZInfoResult &tz_result)
        : tz_result_(tz_result)
    {}
    bool operator() (ObTZIDKey key, ObTimeZoneInfoPos *tz_info);
  private:
    ObRequestTZInfoResult &tz_result_;
  };

public:
ObTimeZoneInfoManager(common::ObMySQLProxy &sql_proxy,
                      int64_t tenant_id)
    : sql_proxy_(sql_proxy),
      tz_info_map_(),
      tz_info_map_buf_(),
      inited_(false),
      is_usable_(false),
      last_version_(-1),
      tenant_id_(tenant_id)
      {}
  ~ObTimeZoneInfoManager()
  {}
  int init();
  int is_usable() const { return is_usable_; }
  void set_usable() { is_usable_ = true;  }
  //rs fetch tz_info from time_zone tables
  int fetch_time_zone_info();
  int response_time_zone_info(ObRequestTZInfoResult &tz_result);
  int update_sys_time_zone_info_version();
  int get_time_zone();
  int find_time_zone_info(const common::ObString &tz_name, ObTimeZoneInfoPos &tz_info);
  int64_t get_version() const { return last_version_; }
  ObTZInfoMap *get_tz_info_map() { return &tz_info_map_; }

  static const char *FETCH_TZ_INFO_SQL;
  static const char *FETCH_TENANT_TZ_INFO_SQL;
  static const char *FETCH_LATEST_TZ_VERSION_SQL;
  // calculate the offset between any two time zones
  static int calc_tz_info_offsets(ObTZInfoMap &tz_info_map);
  static int fill_tz_info_map(common::sqlclient::ObMySQLResult &result, ObTZInfoMap &tz_info_map,
                              uint64_t tenant_id = common::OB_SERVER_TENANT_ID);
  static int set_tz_info_map(
      ObTimeZoneInfoPos *&stored_tz_info,
      ObTimeZoneInfoPos &new_tz_info,
      ObTZInfoMap &tz_info_map);
  static bool cmp_tz_info_map(ObTZInfoMap &tz_info_map1, ObTZInfoMap &tz_info_map2);
private:

  int fetch_time_zone_info_from_tenant_table(const int64_t current_tz_version);
  static int calc_default_tran_type(const common::ObIArray<ObTZTransitionTypeInfo> &types_with_null,
                             ObTimeZoneInfoPos &type_info);
  static int prepare_tz_info(const common::ObIArray<ObTZTransitionTypeInfo> &types_with_null,
                      ObTimeZoneInfoPos &type_info);

private:
  static ObTZInfoMap shared_tz_info_map_;
  static int64_t loaded_tz_info_count_;
  static SpinRWLock sys_rwlock_;
  common::ObMySQLProxy &sql_proxy_;
  ObTZInfoMap tz_info_map_;
  ObTZInfoMap tz_info_map_buf_;
  bool inited_;
  //is_usable_ == true when the server can provide services to the outside; situations where it is set to true
  //If time_zone_info_version in __all_zone is 0, set to true upon receiving the first heartbeat
  //If the time_zone_info_version in __all_zone is greater than 0, set to true after brushing timezone info
  volatile bool is_usable_;
  int64_t last_version_;
  // Record tenant_id_ for obtaining the tz_info_version of this tenant
  int64_t tenant_id_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTimeZoneInfoManager);
};


}// common
}// oceanbase
#endif
