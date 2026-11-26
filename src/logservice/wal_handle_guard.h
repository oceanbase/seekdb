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

#ifndef OCEANBASE_LOGSERVICE_WAL_HANDLE_GUARD_
#define OCEANBASE_LOGSERVICE_WAL_HANDLE_GUARD_
#include "share/ob_delegate.h"
#include "wal_adapter/handle.h"
#include "wal_adapter/env.h"
namespace oceanbase
{
namespace wal
{
class HandleGuard
{
public:
  HandleGuard() : wal_handle_(), wal_env_(nullptr)
  {
  }
  ~HandleGuard()
  {
    reset();
  }
  void reset()
  {
    if (nullptr != wal_env_)
    {
      wal_env_->close(wal_handle_);
      wal_env_ = nullptr;
    }
  }

  bool is_valid() const
  {
    return wal_handle_.is_valid();
  }

  Handle *get_wal_handle() { return &wal_handle_; }

  void set(Handle &wal_handle, Env *wal_env)
  {
    wal_handle_ = wal_handle;
    wal_env_ = wal_env;
    wal_handle.wal_handle_impl_.reset();
  }
  DELEGATE_WITH_RET(wal_handle_, append, int);

  // @breif, query lsn by timestamp, note that this function may be time-consuming
  // @param[in] const int64_t, specified timestamp(ns).
  // @param[out] LSN&, the lower bound lsn which include timestamp.
  // @breif, query lsn by timestamp, note that this function may be time-consuming
  // @param[in] const int64_t, specified timestamp(ns).
  // @param[out] LSN&, the lower bound lsn which include timestamp.
  // int locate_by_scn_coarsely(const int64_t scn, LSN &lsn, int64_t &ts);
  DELEGATE_WITH_RET(wal_handle_, locate_by_scn_coarsely, int);

  DELEGATE_WITH_RET(wal_handle_, locate_by_lsn_coarsely, int);
  // @brief, set the recycable lsn, wal will ensure that the data before recycable lsn readable.
  // @param[in] const LSN&, recycable lsn.
  // int advance_base_lsn(const LSN &lsn);
  DELEGATE_WITH_RET(wal_handle_, advance_base_lsn, int);

  CONST_DELEGATE_WITH_RET(wal_handle_, get_base_lsn, int);
  // @brief, get end lsn.
  // @param[out] LSN&, end lsn.
  // int get_end_lsn(LSN &lsn) const;
  CONST_DELEGATE_WITH_RET(wal_handle_, get_end_lsn, int);
  // @brief, get timestamp of end lsn.
  // @param[out] int64_t, timestamp.
  // int get_end_scn(int64_t &ts) const;
  CONST_DELEGATE_WITH_RET(wal_handle_, get_end_scn, int);
  // @brief get readable end lsn; all logs before it are readable.
  // @param[out] lsn, readable end lsn.
  // -- OB_NOT_INIT           not_init
  // -- OB_SUCCESS
  CONST_DELEGATE_WITH_RET(wal_handle_, stat, int);
  TO_STRING_KV(K(wal_handle_));
private:
  Handle wal_handle_;
  Env *wal_env_;
};
}
}
#endif
