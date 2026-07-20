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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_ENGINE_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_ENGINE_

#define private public
#include "logservice/palf/log_engine.h"
#undef private

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace share;

namespace palf
{

class MockLogEngine : public LogEngine
{
public:
  MockLogEngine() = default;
  virtual ~MockLogEngine() {}

  void destroy() {}

  int submit_flush_log_task(
      const FlushLogCbCtx &flush_log_cb_ctx,
      const char *buf,
      const int64_t buf_len)
  {
    int ret = OB_SUCCESS;
    UNUSED(flush_log_cb_ctx);
    UNUSED(buf);
    UNUSED(buf_len);
    return ret;
  }

  int submit_flush_log_task(
      const FlushLogCbCtx &flush_log_cb_ctx,
      const LogWriteBuf &write_buf) override
  {
    int ret = OB_SUCCESS;
    UNUSED(flush_log_cb_ctx);
    UNUSED(write_buf);
    return ret;
  }


  int submit_flush_snapshot_meta_task(
      const FlushMetaCbCtx &flush_meta_cb_ctx,
      const LogSnapshotMeta &log_snapshot_meta)
  {
    int ret = OB_SUCCESS;
    UNUSED(flush_meta_cb_ctx);
    UNUSED(log_snapshot_meta);
    return ret;
  }


  int submit_truncate_prefix_blocks_task(
      const TruncatePrefixBlocksCbCtx &truncate_prefix_blocks_ctx)
  {
    int ret = OB_SUCCESS;
    UNUSED(truncate_prefix_blocks_ctx);
    return ret;
  }

  int after_flush_log(
      LogIOFlushLogTask *log_io_task)
  {
    int ret = OB_SUCCESS;
    UNUSED(log_io_task);
    return ret;
  }

  int after_flush_meta(
      LogIOFlushMetaTask *log_io_task)
  {
    int ret = OB_SUCCESS;
    UNUSED(log_io_task);
    return ret;
  }


  int after_truncate_prefix_blocks(
      LogIOTruncatePrefixBlocksTask *log_io_task)
  {
    int ret = OB_SUCCESS;
    UNUSED(log_io_task);
    return ret;
  }
  int append_log(const LSN &lsn,
                 const LogWriteBuf &write_buf,
                 const int64_t log_ts)
  {
    int ret = OB_SUCCESS;
    UNUSED(lsn);
    UNUSED(write_buf);
    UNUSED(log_ts);
    return ret;
  }
  int read_log(const LSN &lsn,
               const int64_t in_read_size,
               ReadBuf &read_buf,
               int64_t &out_read_size)
  {
    int ret = OB_SUCCESS;
    UNUSED(lsn);
    UNUSED(in_read_size);
    UNUSED(read_buf);
    UNUSED(out_read_size);
    return ret;
  }
  int read_group_entry_header(const LSN &lsn,
                              LogGroupEntryHeader &log_group_entry_header)
  {
    int ret = OB_SUCCESS;
    UNUSED(lsn);
    UNUSED(log_group_entry_header);
    return ret;
  }
  int truncate(const LSN &prev_lsn, const LSN &lsn)
  {
    int ret = OB_SUCCESS;
    UNUSED(prev_lsn);
    UNUSED(lsn);
    return ret;
  }
  int truncate_prefix_blocks(const LSN &lsn)
  {
    int ret = OB_SUCCESS;
    UNUSED(lsn);
    return ret;
  }
  int delete_block(const block_id_t &block_id)
  {
    int ret = OB_SUCCESS;
    UNUSED(block_id);
    return ret;
  }

  const LSN get_begin_lsn() const
  {
    LSN lsn(0);
    return lsn;
  }
  int get_block_id_range(block_id_t &min_block_id, block_id_t &max_block_id) const
  {
    int ret = OB_SUCCESS;
    UNUSED(min_block_id);
    UNUSED(max_block_id);
    return ret;
  }
  int get_block_min_ts_ns(const block_id_t &block_id, int64_t &ts_ns)
  {
    int ret = OB_SUCCESS;
    UNUSED(block_id);
    UNUSED(ts_ns);
    return ret;
  }
  int update_base_lsn_used_for_gc(const LSN &lsn)
  {
    int ret = OB_SUCCESS;
    UNUSED(lsn);
    return ret;
  }
  int append_meta(const char *buf,
                  const int64_t buf_len)
  {
    int ret = OB_SUCCESS;
    UNUSED(buf);
    UNUSED(buf_len);
    return ret;
  }


  LogMeta get_log_meta() const
  {
    LogMeta meta;
    return meta;
  }
  const LSN &get_base_lsn_used_for_block_gc() const
  {
    return base_lsn_for_block_gc_;
  }
  int get_min_block_info_for_gc(block_id_t &block_id, int64_t &ts_ns)
  {
    int ret = OB_SUCCESS;
    UNUSED(block_id);
    UNUSED(ts_ns);
    return ret;
  }
  LogStorage *get_log_storage() { return &log_storage_; }
  LogStorage *get_log_meta_storage() { return &log_meta_storage_; }


};

} // end of palf
} // end of oceanbase
#endif
