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

#include "logservice/palf/log_io_task_cb_utils.h"
#include "logservice/ob_tenant_mutil_allocator.h"
#include <gtest/gtest.h>
#define private public
#include "logservice/palf/log_sliding_window.h"
#include "mock_logservice_container/mock_log_mode_mgr.h"
#include "mock_logservice_container/mock_log_engine.h"
#include "mock_logservice_container/mock_log_state_mgr.h"
#include "mock_logservice_container/mock_palf_fs_cb_wrapper.h"
#undef private

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace share;

namespace unittest
{

class TestLogSlidingWindow : public ::testing::Test
{
public:
  TestLogSlidingWindow();
  virtual ~TestLogSlidingWindow();
public:
  virtual void SetUp();
  virtual void TearDown();
public:
  class MockPublicLogSlidingWindow : public LogSlidingWindow
  {
  public:
    MockPublicLogSlidingWindow() {}
    virtual ~MockPublicLogSlidingWindow() {}
    virtual bool is_handle_thread_lease_expired(const int64_t thread_lease_begin_ts) const override final
    {
      UNUSED(thread_lease_begin_ts);
      return false;
    }
  };
public:
  common::ObAddr self_;
  MockLogStateMgr mock_state_mgr_;
  MockLogModeMgr mock_mode_mgr_;
  MockLogEngine mock_log_engine_;
  MockPalfFSCbWrapper palf_fs_cb_;
  ObTenantMutilAllocator *alloc_mgr_;
  MockPublicLogSlidingWindow log_sw_;
  char *data_buf_;
};

TestLogSlidingWindow::TestLogSlidingWindow() {}
TestLogSlidingWindow::~TestLogSlidingWindow() {}

const static uint64_t tenant_id = 1001;
void TestLogSlidingWindow::SetUp()
{
  self_.set_ip_addr("127.0.0.1", 12345);

  int ret = ObMallocAllocator::get_instance()->create_and_add_tenant_allocator();
  OB_ASSERT(OB_SUCCESS == ret);
  ObMemAttr attr(ObModIds::OB_TENANT_MUTIL_ALLOCATOR);
  void *buf = ob_malloc(sizeof(common::ObTenantMutilAllocator), attr);
  if (NULL == buf) {
    CLOG_LOG_RET(WARN, OB_ALLOCATE_MEMORY_FAILED, "alloc memory failed");
    OB_ASSERT(false);
  }
  alloc_mgr_ = new (buf) common::ObTenantMutilAllocator();
  data_buf_ = (char*)ob_malloc(64 * 1024 * 1024, attr);
  // init MTL
  ObTenantBase tbase(tenant_id);
  ObTenantEnv::set_tenant(&tbase);
}

void TestLogSlidingWindow::TearDown()
{
  ob_free(alloc_mgr_);
  ob_free(data_buf_);
  ObMallocAllocator::get_instance()->recycle_tenant_allocator();
}

void gen_default_palf_base_info_(PalfBaseInfo &palf_base_info)
{
  palf_base_info.reset();
  LSN default_prev_lsn(PALF_INITIAL_LSN_VAL);
  LogInfo prev_log_info;
  prev_log_info.log_id_ = 0;
  prev_log_info.scn_.set_min();
  prev_log_info.lsn_ = default_prev_lsn;
  prev_log_info.accum_checksum_ = -1;
  palf_base_info.prev_log_info_ = prev_log_info;
  palf_base_info.curr_lsn_ = default_prev_lsn;
}

TEST_F(TestLogSlidingWindow, test_log_checksum)
{
  int64_t last_log_acc_checksum = 1000310830;
  int64_t data_checksum = 3265973353;
  int64_t cal_checksum = -1;
  int64_t expected_acc_checksum = 389839115;
  LogChecksum checksum_obj;
  EXPECT_EQ(OB_SUCCESS, checksum_obj.init(last_log_acc_checksum));
  EXPECT_EQ(OB_SUCCESS, checksum_obj.acquire_accum_checksum(data_checksum, cal_checksum));
  EXPECT_EQ(OB_SUCCESS, checksum_obj.verify_accum_checksum(data_checksum, expected_acc_checksum));
  PALF_LOG(INFO, "finish test checksum", K(last_log_acc_checksum), K(data_checksum), K(cal_checksum), K(expected_acc_checksum));
}

TEST_F(TestLogSlidingWindow, test_init)
{
  PalfBaseInfo base_info;
  gen_default_palf_base_info_(base_info);

  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.init(self_, NULL, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, NULL, &palf_fs_cb_, NULL, base_info));
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.init(self_, &mock_state_mgr_,
        NULL, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.init(self_, &mock_state_mgr_, NULL, NULL, &palf_fs_cb_, NULL, base_info));
  // init succ
  EXPECT_EQ(OB_SUCCESS, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  // init twice
  EXPECT_EQ(OB_INIT_TWICE, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
}

TEST_F(TestLogSlidingWindow, test_private_func_batch_01)
{
  LSN end_lsn;
  int64_t log_id = OB_INVALID_LOG_ID;
  EXPECT_EQ(OB_NOT_INIT, log_sw_.get_last_slide_end_lsn(end_lsn));
  PalfBaseInfo base_info;
  gen_default_palf_base_info_(base_info);
  // init succ
  EXPECT_EQ(OB_SUCCESS, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  log_id = 10 + PALF_SLIDING_WINDOW_SIZE;
  EXPECT_EQ(false, log_sw_.can_receive_larger_log_(log_id));
  EXPECT_EQ(false, log_sw_.can_submit_larger_log_(log_id));
  EXPECT_EQ(false, log_sw_.can_submit_larger_log_(PALF_SLIDING_WINDOW_SIZE + 1));
  EXPECT_EQ(OB_SUCCESS, log_sw_.get_last_slide_end_lsn(end_lsn));
  share::SCN scn = log_sw_.get_last_slide_scn();
}

TEST_F(TestLogSlidingWindow, test_report_log_task_trace)
{
  EXPECT_EQ(OB_NOT_INIT, log_sw_.report_log_task_trace(1));
  PalfBaseInfo base_info;
  gen_default_palf_base_info_(base_info);
  // init succ
  EXPECT_EQ(OB_SUCCESS, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  EXPECT_EQ(OB_SUCCESS, log_sw_.report_log_task_trace(1));
  char *buf = data_buf_;
  int64_t buf_len = 2 * 1024 * 1024;
  LSN lsn;
  share::SCN scn;
  share::SCN ref_scn;
  ref_scn.convert_for_logservice(99);
  buf_len = 2 * 1024 * 1024;
  EXPECT_EQ(OB_SUCCESS, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  EXPECT_EQ(OB_SUCCESS, log_sw_.report_log_task_trace(1));
}

TEST_F(TestLogSlidingWindow, test_submit_log)
{
  PALF_LOG(INFO, "begin test_submit_log");
  PalfBaseInfo base_info;
  gen_default_palf_base_info_(base_info);
  char *buf = data_buf_;
  int64_t buf_len = 1000;
  share::SCN ref_scn;
  ref_scn.convert_for_logservice(99);
  LSN lsn;
  share::SCN scn;
  EXPECT_EQ(OB_NOT_INIT, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  EXPECT_EQ(OB_SUCCESS, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.submit_log(NULL, buf_len, ref_scn, lsn, scn));
  buf_len = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  buf_len = 64 * 1024 * 1024;
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  buf_len = 1000;
  ref_scn.reset();
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  ref_scn.convert_for_logservice(99);
  buf_len = 1 * 1024 * 1024;
  for (int i = 0; i < 2; ++i) {
    EXPECT_EQ(OB_SUCCESS, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  }
  // append to last group log
  buf_len = 1 * 1024 * 1024;
  EXPECT_EQ(OB_SUCCESS, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  buf_len = 2 * 1024 * 1024;
  PALF_LOG(INFO, "current lsn", K(lsn), K(buf_len));
  // 4M group buffer has been filled with 3M, unable to continue submit 2M log.
  EXPECT_EQ(OB_EAGAIN, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
}

TEST_F(TestLogSlidingWindow, test_after_flush_log)
{
  PALF_LOG(INFO, "begin test_after_flush_log");
  FlushLogCbCtx flush_log_ctx;
  EXPECT_EQ(OB_NOT_INIT, log_sw_.after_flush_log(flush_log_ctx));

  PalfBaseInfo base_info;
  gen_default_palf_base_info_(base_info);
  EXPECT_EQ(OB_SUCCESS, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));


  char *buf = data_buf_;
  int64_t buf_len = 2 * 1024 * 1024;
  share::SCN ref_scn;
  ref_scn.convert_for_logservice(999);
  LSN lsn;
  share::SCN scn;
  EXPECT_EQ(OB_SUCCESS, log_sw_.submit_log(buf, buf_len, ref_scn, lsn, scn));
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.after_flush_log(flush_log_ctx));

  flush_log_ctx.log_id_ = PALF_SLIDING_WINDOW_SIZE + 100;
  flush_log_ctx.scn_ = scn;
  LSN group_log_lsn;
  group_log_lsn.val_ = lsn.val_ - LogGroupEntryHeader::HEADER_SER_SIZE;
  flush_log_ctx.lsn_ = group_log_lsn;
  flush_log_ctx.total_len_ = LogGroupEntryHeader::HEADER_SER_SIZE + LogEntryHeader::HEADER_SER_SIZE + buf_len;;
  flush_log_ctx.begin_ts_ = ObTimeUtility::current_time();
  EXPECT_EQ(OB_ERR_OUT_OF_UPPER_BOUND, log_sw_.after_flush_log(flush_log_ctx));
  flush_log_ctx.log_id_ = 2;
  EXPECT_EQ(OB_SUCCESS, log_sw_.after_flush_log(flush_log_ctx));
  flush_log_ctx.log_id_ = 1;
  EXPECT_EQ(OB_ERR_OUT_OF_LOWER_BOUND, log_sw_.after_flush_log(flush_log_ctx));
}


TEST_F(TestLogSlidingWindow, test_append_disk_log)
{
  PALF_LOG(INFO, "begin test_append_disk_log");
  LSN lsn(0);
  LogGroupEntry group_entry;
  EXPECT_EQ(OB_NOT_INIT, log_sw_.append_disk_log(lsn, group_entry));
  PalfBaseInfo base_info;
  gen_default_palf_base_info_(base_info);
  EXPECT_EQ(OB_SUCCESS, log_sw_.init(self_, &mock_state_mgr_, &mock_mode_mgr_, &mock_log_engine_, &palf_fs_cb_, alloc_mgr_, base_info));
  // generate new group entry
  LogEntry log_entry;
  LogEntryHeader log_entry_header;
  LogGroupEntryHeader group_header;
  share::SCN max_scn;
  max_scn.convert_for_logservice(111111);
  int64_t log_id = 1;
  LSN committed_end_lsn(0);
  char log_data[2048];
  int64_t log_data_len = 2048;
  int64_t group_data_checksum = -1;
  EXPECT_EQ(OB_SUCCESS, log_entry_header.generate_header(log_data, log_data_len, max_scn));
  static const int64_t DATA_BUF_LEN = 64 * 1024 * 1024;
  int64_t group_header_size = LogGroupEntryHeader::HEADER_SER_SIZE;
  int64_t pos = 0;
  log_entry_header.serialize(data_buf_ + group_header_size, DATA_BUF_LEN, pos);
  EXPECT_TRUE(pos > 0);
  memcpy(data_buf_ + group_header_size + pos, log_data, log_data_len);
  int64_t dser_pos = 0;
  // test log_entry serialize/deserialize
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_entry.deserialize(NULL, DATA_BUF_LEN - group_header_size, dser_pos));
  int64_t short_buf_size = log_entry_header.get_serialize_size() - 10;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, log_entry.deserialize(data_buf_ + group_header_size, short_buf_size, dser_pos));
  EXPECT_EQ(OB_SUCCESS, log_entry.deserialize(data_buf_ + group_header_size, DATA_BUF_LEN - group_header_size, dser_pos));
  EXPECT_EQ(true, log_entry.check_integrity());
  int64_t new_ser_pos = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_entry.serialize(NULL, DATA_BUF_LEN - group_header_size, new_ser_pos));
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, log_entry.serialize(data_buf_ + group_header_size, dser_pos - 10, new_ser_pos));
  EXPECT_EQ(OB_SUCCESS, log_entry.serialize(data_buf_ + group_header_size, DATA_BUF_LEN - group_header_size, new_ser_pos));
  EXPECT_EQ(dser_pos, new_ser_pos);

  int64_t log_entry_size = pos + log_data_len;
  // gen 2nd log entry
  max_scn.convert_for_logservice(222222);
  EXPECT_EQ(OB_SUCCESS, log_entry_header.generate_header(log_data, log_data_len, max_scn));
  pos = 0;
  log_entry_header.serialize(data_buf_ + group_header_size + log_entry_size, DATA_BUF_LEN, pos);
  EXPECT_TRUE(pos > 0);
  memcpy(data_buf_ + group_header_size + log_entry_size + pos, log_data, log_data_len);
  log_entry_size += (pos + log_data_len);
  // gen group log
  LogWriteBuf write_buf;
  EXPECT_EQ(OB_INVALID_ARGUMENT, group_header.generate(false, write_buf, log_entry_size, max_scn, log_id,
      committed_end_lsn, group_data_checksum));
  const int64_t total_group_log_size = group_header_size + log_entry_size;
  const int64_t first_part_len = total_group_log_size / 2;
  EXPECT_TRUE(first_part_len > 0);
  const int64_t second_part_len = total_group_log_size - first_part_len;
  // continous buf
  EXPECT_EQ(OB_SUCCESS, write_buf.push_back(data_buf_, first_part_len));
  EXPECT_EQ(OB_SUCCESS, write_buf.push_back(data_buf_ + first_part_len, second_part_len));
  EXPECT_EQ(OB_SUCCESS, group_header.generate(false, write_buf, log_entry_size, max_scn, log_id,
      committed_end_lsn, group_data_checksum));
  // non-continous buf
  group_header.reset();
  write_buf.reset();
  char *second_buf = (char *)ob_malloc(second_part_len, ObNewModIds::TEST);
  EXPECT_TRUE(NULL != second_buf);
  memcpy(second_buf, data_buf_ + first_part_len, second_part_len);
  EXPECT_EQ(OB_SUCCESS, write_buf.push_back(data_buf_, first_part_len));
  EXPECT_EQ(OB_SUCCESS, write_buf.push_back(second_buf, second_part_len));
  EXPECT_EQ(OB_SUCCESS, group_header.generate(false, write_buf, log_entry_size, max_scn, log_id,
      committed_end_lsn, group_data_checksum));
  int64_t accum_checksum = 100;
  (void) group_header.update_accumulated_checksum(accum_checksum);
  // calculate header parity flag
  (void) group_header.update_header_checksum();
  pos = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT, group_header.serialize(NULL, DATA_BUF_LEN, pos));
  EXPECT_EQ(OB_INVALID_ARGUMENT, group_header.serialize(data_buf_, 0, pos));
  EXPECT_EQ(OB_SUCCESS, group_header.serialize(data_buf_, DATA_BUF_LEN, pos));
  EXPECT_TRUE(pos > 0);
  int64_t group_entry_size = pos + log_entry_size;
  EXPECT_TRUE(group_header.check_integrity(data_buf_ + group_header_size, group_entry_size - group_header_size));
  // append disk log
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.append_disk_log(lsn, group_entry));
  EXPECT_EQ(OB_SUCCESS, group_entry.generate(group_header, data_buf_ + group_header_size));
  lsn.reset();
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_sw_.append_disk_log(lsn, group_entry));
  lsn.val_ = 0;
  EXPECT_EQ(OB_SUCCESS, log_sw_.append_disk_log(lsn, group_entry));
  // gen new group entry
  log_id++;
  uint64_t new_val = max_scn.get_val_for_logservice() + 100;
  max_scn.convert_for_logservice(new_val);
  lsn.val_ += group_entry_size;
  // gen group log
  LogWriteBuf write_buf1;
  EXPECT_EQ(OB_SUCCESS, write_buf1.push_back(data_buf_, log_entry_size+group_header_size));
  EXPECT_EQ(OB_SUCCESS, group_header.generate(false, write_buf, log_entry_size, max_scn, log_id,
      committed_end_lsn, group_data_checksum));
  accum_checksum += 100;
  (void) group_header.update_accumulated_checksum(accum_checksum);
  // calculate header parity flag
  (void) group_header.update_header_checksum();
  pos = 0;
  group_header.serialize(data_buf_, DATA_BUF_LEN, pos);
  EXPECT_TRUE(pos > 0);
  group_entry_size = pos + log_entry_size;
  EXPECT_TRUE(group_header.check_integrity(data_buf_ + group_header_size, group_entry_size - group_header_size));
  EXPECT_EQ(OB_SUCCESS, group_entry.generate(group_header, data_buf_ + group_header_size));
  EXPECT_EQ(OB_SUCCESS, log_sw_.append_disk_log(lsn, group_entry));
}

} // END of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  system("rm -f ./test_log_sliding_window.log");
  OB_LOGGER.set_file_name("test_log_sliding_window.log", true);
  OB_LOGGER.set_log_level("TRACE");
  PALF_LOG(INFO, "begin unittest::test_log_sliding_window");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
