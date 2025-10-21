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

#ifndef OCEANBASE_LOGSERVICE_LOG_OFFSET_ALLOCATOR_
#define OCEANBASE_LOGSERVICE_LOG_OFFSET_ALLOCATOR_

#include "lib/atomic/atomic128.h"
#include "lib/lock/ob_tc_rwlock.h"
#include "lib/utility/ob_macro_utils.h"
#include "share/scn.h"
#include "lsn.h"

namespace oceanbase
{
namespace palf
{
class LSNAllocator
{
public:
  LSNAllocator();
  ~LSNAllocator();
public:
  int init(const int64_t log_id, const share::SCN &scn, const LSN &start_lsn);
  void reset();
  int get_log_block_size(const uint64_t block_id, int64_t &block_size) const
  {
    // TODO: by haofan
    // To support configurable log file size, it is necessary to obtain the corresponding size for each file
    UNUSED(block_id);
    block_size = PALF_BLOCK_SIZE;
    return OB_SUCCESS;
  }
  int64_t get_max_log_id() const;
  share::SCN get_max_scn() const;
  int get_curr_end_lsn(LSN &curr_end_lsn) const;
  int try_freeze_by_time(LSN &last_lsn, int64_t &last_log_id);
  int try_freeze(LSN &last_lsn, int64_t &last_log_id);
  // Function: Assign lsn, scn to a log entry.
  //
  // @param [in] base_scn: lower bound value of scn
  // @param [in] size: length of the log body, if it is an aggregated log, it should include the LogHeader length
  //
  // @param [out] lsn: allocated lsn
  // @param [out] scn: allocated scn
  // @param [out] is_new_log: whether a new log needs to be generated
  // @param [out] need_gen_padding_entry: whether a padding_entry needs to be generated before this log entry
  // @param [out] padding_len: total length of the padding part
  //
  int alloc_lsn_scn(const share::SCN &base_scn,
                    const int64_t size,
                    const int64_t log_id_upper_bound,
                    const LSN &lsn_upper_bound,
                    LSN &lsn,
                    int64_t &log_id,
                    share::SCN &scn,
                    bool &is_new_log,
                    bool &need_gen_padding_entry,
                    int64_t &padding_len);
  // Update last_lsn and log_timestamp
  // called when receive_log/append_disk_log is invoked
  int inc_update_last_log_info(const LSN &lsn, const int64_t log_id, const share::SCN &scn);
  // inc update scn base, called by change access mode and to leader active
  int inc_update_scn_base(const share::SCN &scn);
  int truncate(const LSN &lsn, const int64_t log_id, const share::SCN &scn);
  // Get last_lsn and log_timestamp
  TO_STRING_KV("max_log_id", get_max_log_id(), "max_lsn", lsn_ts_meta_.lsn_val_,
      "max_scn", get_max_scn());
private:
  static const int32_t LOG_ID_DELTA_BIT_CNT = 28;  // number of bits for log_id_delta, can generate 250,000 log_ids
  static const int32_t LOG_TS_DELTA_BIT_CNT = 35;  // number of bits for scn_delta part, ns level, approximately available for 32 seconds
  static const int64_t LOG_ID_DELTA_UPPER_BOUND = (1ul << LOG_ID_DELTA_BIT_CNT) - 1000;
  static const int64_t LOG_TS_DELTA_UPPER_BOUND = (1ul << LOG_TS_DELTA_BIT_CNT) - 1000;
  static const uint64_t LOG_CUT_TRIGGER = 1 << 21;          // Split log when it crosses the 2MB boundary
  static const uint64_t LOG_CUT_TRIGGER_MASK = (1 << 21) - 1;
  static const uint64_t MAX_SUPPORTED_BLOCK_ID = 0xfffffffff - 1000;  // block_id alarm threshold
  static const uint64_t MAX_SUPPORTED_BLOCK_OFFSET = 0xfffffff;        // the maximum supported block_offset is 256MB
private:
  union LSNTsMeta
  {
    struct types::uint128_t v128_;
    struct
    {
      uint8_t is_need_cut_ : 1;  // whether next log need cut
      uint64_t log_id_delta_ : LOG_ID_DELTA_BIT_CNT;
      uint64_t scn_delta_ : LOG_TS_DELTA_BIT_CNT;
      uint64_t lsn_val_ : 64;
    };
  };
private:
  typedef common::RWLock RWLock;
  typedef RWLock::RLockGuard RLockGuard;
  typedef RWLock::WLockGuard WLockGuard;
private:
  LSNTsMeta lsn_ts_meta_;
  int64_t log_id_base_;
  uint64_t scn_base_;
  mutable RWLock lock_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(LSNAllocator);
};
}  // namespace palf
}  // namespace oceanbase
#endif // OCEANBASE_LOGSERVICE_LOG_OFFSET_ALLOCATOR_
