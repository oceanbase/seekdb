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

#ifndef OCEANBASE_LOGSERVICE_PALF_HANDLE_GUARD_
#define OCEANBASE_LOGSERVICE_PALF_HANDLE_GUARD_
#include "share/ob_delegate.h"
#include "palf/palf_handle.h"
#include "palf/palf_env.h"
namespace oceanbase
{
namespace palf
{
class PalfHandleGuard
{
public:
  PalfHandleGuard() : palf_handle_(), palf_env_(nullptr)
  {
  }
  ~PalfHandleGuard()
  {
    reset();
  }
  void reset()
  {
    if (nullptr != palf_env_)
    {
      palf_env_->close(palf_handle_);
      palf_env_ = nullptr;
    }
  }
  
  bool is_valid() const
  {
    return palf_handle_.is_valid();
  }

  PalfHandle *get_palf_handle() { return &palf_handle_; }

  void set(PalfHandle &palf_handle, PalfEnv *palf_env)
  {
    palf_handle_ = palf_handle;
    palf_env_ = palf_env;
    palf_handle.palf_handle_impl_ = NULL;
  }
  // Append a sealed PalfLogBuffer. PALF takes ownership after accepting it.
  DELEGATE_WITH_RET(palf_handle_, append, int);
  DELEGATE_WITH_RET(palf_handle_, raw_read, int);

  // @breif, query lsn by timestamp, note that this function may be time-consuming
  // @param[in] const int64_t, specified timestamp(ns).
  // @param[out] LSN&, the lower bound lsn which include timestamp.
  // @breif, query lsn by timestamp, note that this function may be time-consuming
  // @param[in] const int64_t, specified timestamp(ns).
  // @param[out] LSN&, the lower bound lsn which include timestamp.
  // int locate_by_scn_coarsely(const int64_t scn, LSN &lsn, int64_t &ts);
  DELEGATE_WITH_RET(palf_handle_, locate_by_scn_coarsely, int);

  DELEGATE_WITH_RET(palf_handle_, locate_by_lsn_coarsely, int);
  // @brief, set the recycable lsn, palf will ensure that the data before recycable lsn readable.
  // @param[in] const LSN&, recycable lsn.
  // int advance_base_lsn(const LSN &lsn);
  DELEGATE_WITH_RET(palf_handle_, advance_base_lsn, int);

  CONST_DELEGATE_WITH_RET(palf_handle_, get_base_lsn, int);
  // @breif, get begin lsn, begin lsn maybe smaller than recycable lsn, because palf will not delete data before
  //         recycable lsn immediately.
  // @param[out] int64_t&, begin lsn.
  // int get_base_scn(int64_t &ts) const;
  CONST_DELEGATE_WITH_RET(palf_handle_, get_begin_scn, int);
  // int get_begin_lsn(palf::LSN &lsn) const;
  CONST_DELEGATE_WITH_RET(palf_handle_, get_begin_lsn, int);
  // @brief, get timestamp of begin lsn.
  // @param[out] int64_t&, timestmap.
  // int get_begin_scn(int64_t &ts) const;
  // CONST_DELEGATE_WITH_RET(palf_handle_, get_begin_scn, int);
  // @brief, get end lsn.
  // @param[out] LSN&, end lsn.
  // int get_end_lsn(LSN &lsn) const;
  CONST_DELEGATE_WITH_RET(palf_handle_, get_end_lsn, int);
  // @brief, get timestamp of end lsn.
  // @param[out] int64_t, timestamp.
  // int get_end_scn(int64_t &ts) const;
  CONST_DELEGATE_WITH_RET(palf_handle_, get_end_scn, int);
  // @brief, get max timestamp.
  // @param[out] int64_t, timestamp.
  // int get_max_scn(int64_t &ts) const;
  CONST_DELEGATE_WITH_RET(palf_handle_, get_max_scn, int);
  // @brief, get max lsn.
  // @param[out] int64_t, LSN.
  // int get_max_lsn(LSN &lsn) const;
  CONST_DELEGATE_WITH_RET(palf_handle_, get_max_lsn, int);
  // @brief get readable end lsn; all logs before it are readable.
  // @param[out] lsn, readable end lsn.
  // -- OB_NOT_INIT           not_init
  // -- OB_SUCCESS
  CONST_DELEGATE_WITH_RET(palf_handle_, get_readable_end_lsn, int);

  CONST_DELEGATE_WITH_RET(palf_handle_, stat, int);
  CONST_DELEGATE_WITH_RET(palf_handle_, get_palf_epoch, int);
  TO_STRING_KV(K(palf_handle_));
private:
  PalfHandle palf_handle_;
  PalfEnv *palf_env_;
};
}
}
#endif
