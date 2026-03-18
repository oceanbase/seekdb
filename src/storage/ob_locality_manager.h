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

#ifndef OCEANBASE_STORAGE_OB_LOCALITY_MANAGER_H_
#define OCEANBASE_STORAGE_OB_LOCALITY_MANAGER_H_

#include "lib/mysqlclient/ob_mysql_proxy.h"
#include "share/ob_locality_info.h"
#include "common/ob_zone_type.h"
#include "share/ob_i_server_auth.h"
#include "lib/queue/ob_dedup_queue.h"

namespace oceanbase
{
namespace share
{
class ObRemoteSqlProxy;
}
namespace storage
{
class ObLocalityManager : public share::ObIServerAuth
{
  static const int64_t REFRESH_LOCALITY_TASK_NUM = 5;
  static const int64_t RELOAD_LOCALITY_INTERVAL = 10 * 1000 * 1000L; //10S
public:
  ObLocalityManager();
  virtual ~ObLocalityManager() { destroy(); }
  int init(const common::ObAddr &self, common::ObMySQLProxy *sql_proxy);
  int start();
  int stop();
  int wait();
  void reset();
  void destroy();
  int is_server_legitimate(const common::ObAddr& addr, bool& is_valid);
  void set_ssl_invited_nodes(const common::ObString &new_value);
  int load_region();
  virtual int get_server_zone_type(const common::ObAddr &server, common::ObZoneType &zone_type) const;
  virtual int get_server_region(const common::ObAddr &server, common::ObRegion &region) const;
  virtual int get_server_idc(const common::ObAddr &server, common::ObIDC &idc) const;
  int get_server_zone(const common::ObAddr &server, common::ObZone &zone) const;
  int is_local_zone_read_only(bool &is_readonly);
  int is_local_server(const common::ObAddr &server, bool &is_local);
  int is_same_zone(const common::ObAddr &server, bool &is_same_zone);
private:
  class ReloadLocalityTask : public common::ObTimerTask
  {
  public:
    ReloadLocalityTask();
    virtual ~ReloadLocalityTask() {}
    int init(ObLocalityManager *locality_mgr);
    virtual void runTimerTask();
  private:
    bool is_inited_;
    ObLocalityManager *locality_mgr_;
  };
  class ObRefreshLocalityTask : public common::IObDedupTask
  {
  public:
    explicit ObRefreshLocalityTask(ObLocalityManager *locality_mgr);
    virtual ~ObRefreshLocalityTask();
    virtual int64_t hash() const;
    virtual bool operator ==(const IObDedupTask &other) const;
    virtual int64_t get_deep_copy_size() const;
    virtual IObDedupTask *deep_copy(char *buffer, const int64_t buf_size) const;
    virtual int64_t get_abs_expired_time() const { return 0; }
    virtual int process();
  private:
    ObLocalityManager *locality_mgr_;
  };
private:
  int check_if_locality_has_been_loaded();
  int add_refresh_locality_task();
private:
  bool is_inited_;
  mutable common::SpinRWLock rwlock_;
  common::ObAddr self_;
  common::ObMySQLProxy *sql_proxy_;
  common::ObDedupQueue refresh_locality_task_queue_;
  ReloadLocalityTask reload_locality_task_;
  char *ssl_invited_nodes_buf_;//common::OB_MAX_CONFIG_VALUE_LEN, use new
  bool is_loaded_;
  static const int64_t FAIL_TO_LOAD_LOCALITY_CACHE_TIMEOUT = 60L * 1000L * 1000L;
};
}// storage
}// oceanbase
#endif // OCEANBASE_STORAGE_OB_LOCALITY_MANAGER_H_
