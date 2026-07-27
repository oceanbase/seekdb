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

#ifndef OCEANBASE_ROOTSERVER_FREEZE_OB_GLOBAL_MERGE_MANAGER_
#define OCEANBASE_ROOTSERVER_FREEZE_OB_GLOBAL_MERGE_MANAGER_

#include <utility>

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "lib/lock/ob_spin_rwlock.h"
#include "share/ob_merge_info.h"

namespace oceanbase
{
namespace rootserver
{

class ObGlobalMergeManagerBase
{
public:
  ObGlobalMergeManagerBase();
  virtual ~ObGlobalMergeManagerBase() = default;

  int init(common::ObMySQLProxy &proxy);
  virtual int reload();
  virtual int try_reload();
  void reset_merge_info();
  void reset_merge_info_without_lock();

  int get_snapshot(share::ObGlobalMergeInfo &global_info);
  int suspend_merge();
  int resume_merge();
  int set_merge_status(const int64_t error_type);
  int check_need_broadcast(const share::SCN &frozen_scn, bool &need_broadcast);
  int set_global_freeze_info(const share::SCN &frozen_scn);

  int get_global_broadcast_scn(share::SCN &global_broadcast_scn) const;
  int get_global_last_merged_scn(share::SCN &global_last_merged_scn) const;
  int get_global_merge_status(share::ObGlobalMergeInfo::MergeStatus &global_merge_status) const;
  int get_global_last_merged_time(int64_t &global_last_merged_time) const;
  int get_global_merge_start_time(int64_t &global_merge_start_time) const;

  virtual int generate_next_global_broadcast_scn(share::SCN &next_scn);
  virtual int try_update_global_last_merged_scn();
  virtual int adjust_global_merge_info();

protected:
  static int copy_info(ObGlobalMergeManagerBase &dest,
                       const ObGlobalMergeManagerBase &src);

private:
  int check_inner_stat() const;
  int suspend_or_resume_merge(const bool suspend);
  int inner_adjust_global_merge_info(const share::SCN &frozen_scn);

protected:
  common::SpinRWLock lock_;

private:
  bool is_inited_;
  bool is_loaded_;
  share::ObGlobalMergeInfo global_merge_info_;
  common::ObMySQLProxy *proxy_;

  DISALLOW_COPY_AND_ASSIGN(ObGlobalMergeManagerBase);
};

#define GLOBAL_MERGE_MANAGER_FUNC(func_name)                                   \
  template <typename... Args> int func_name(Args &&...args) {                  \
    int ret = OB_SUCCESS;                                                      \
    common::SpinWLockGuard guard(write_lock_);                                 \
    {                                                                          \
      ObGlobalMergeMgrGuard shadow_guard(                                      \
          lock_, *(static_cast<ObGlobalMergeManagerBase *>(this)), shadow_,    \
          ret);                                                                \
      if (OB_SUCC(ret)) {                                                      \
        ret = shadow_.func_name(std::forward<Args>(args)...);                  \
      }                                                                        \
    }                                                                          \
    return ret;                                                                \
  }

class ObGlobalMergeManager : public ObGlobalMergeManagerBase
{
public:
  ObGlobalMergeManager();
  virtual ~ObGlobalMergeManager() = default;

  int init(common::ObMySQLProxy &proxy);
  GLOBAL_MERGE_MANAGER_FUNC(reload);
  GLOBAL_MERGE_MANAGER_FUNC(try_reload);
  GLOBAL_MERGE_MANAGER_FUNC(suspend_merge);
  GLOBAL_MERGE_MANAGER_FUNC(resume_merge);
  GLOBAL_MERGE_MANAGER_FUNC(set_merge_status);
  GLOBAL_MERGE_MANAGER_FUNC(check_need_broadcast);
  GLOBAL_MERGE_MANAGER_FUNC(set_global_freeze_info);
  GLOBAL_MERGE_MANAGER_FUNC(generate_next_global_broadcast_scn);
  GLOBAL_MERGE_MANAGER_FUNC(try_update_global_last_merged_scn);
  GLOBAL_MERGE_MANAGER_FUNC(adjust_global_merge_info);

private:
  class ObGlobalMergeMgrGuard
  {
  public:
    ObGlobalMergeMgrGuard(const common::SpinRWLock &lock,
                          ObGlobalMergeManagerBase &global_merge_mgr,
                          ObGlobalMergeManagerBase &shadow,
                          int &ret);
    ~ObGlobalMergeMgrGuard();

  private:
    common::SpinRWLock &lock_;
    ObGlobalMergeManagerBase &global_merge_mgr_;
    ObGlobalMergeManagerBase &shadow_;
    int &ret_;
    DISALLOW_COPY_AND_ASSIGN(ObGlobalMergeMgrGuard);
  };

  common::SpinRWLock write_lock_;
  ObGlobalMergeManagerBase shadow_;
  common::ObMySQLProxy illegal_proxy_;
};

} // end rootserver
} // end oceanbase

#endif // OCEANBASE_ROOTSERVER_FREEZE_OB_GLOBAL_MERGE_MANAGER_
