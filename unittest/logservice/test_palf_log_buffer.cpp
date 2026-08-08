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

#include <gtest/gtest.h>
#include <vector>

#include "lib/checksum/ob_crc64.h"
#include "logservice/palf/log_block_handler.h"
#include "logservice/palf/lsn_allocator.h"
#include "logservice/palf/log_task.h"
#include "logservice/palf/palf_log_buffer.h"

namespace oceanbase
{
using namespace common;
using namespace palf;
using namespace share;

namespace unittest
{

TEST(TestPalfLogBuffer, prepare_bind_and_restore_owner)
{
  const char payload[] = "transferable-log";
  const int64_t payload_len = sizeof(payload) - 1;
  PalfLogBuffer owner;
  LogPendingBufferLimiter limiter(1024);
  LogBufferSegment *segment = NULL;

  ASSERT_EQ(OB_SUCCESS, owner.init(128, LogEntryHeader::HEADER_SER_SIZE));
  MEMCPY(owner.get_buf(), payload, payload_len);
  ASSERT_EQ(OB_SUCCESS, owner.seal(payload_len));
  ASSERT_EQ(OB_SUCCESS, LogBufferSegment::prepare_normal(owner, &limiter, segment));
  ASSERT_FALSE(owner.is_valid());
  ASSERT_TRUE(NULL != segment);
  EXPECT_EQ(128, limiter.get_pending_bytes());

  const LSN entry_lsn(100);
  const SCN scn = SCN::base_scn();
  ASSERT_EQ(OB_SUCCESS, segment->bind_normal(entry_lsn, scn));
  EXPECT_EQ(entry_lsn, segment->get_begin_lsn());
  EXPECT_EQ(LogEntryHeader::HEADER_SER_SIZE + payload_len, segment->get_entry_size());

  LogEntryHeader entry_header;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, entry_header.deserialize(segment->get_entry_buf(),
      segment->get_entry_size(), pos));
  EXPECT_EQ(LogEntryHeader::HEADER_SER_SIZE, pos);
  EXPECT_EQ(payload_len, entry_header.get_data_len());
  EXPECT_EQ(scn, entry_header.get_scn());
  EXPECT_TRUE(entry_header.check_integrity(segment->get_entry_buf() + pos, payload_len));
  EXPECT_EQ(0, MEMCMP(segment->get_entry_buf() + pos, payload, payload_len));

  // Simulate SlidingWindow destruction while an IO task still owns the
  // segment.  The reservation state must outlive the limiter wrapper reset.
  limiter.reset(1024);
  EXPECT_EQ(0, limiter.get_pending_bytes());
  ASSERT_EQ(OB_SUCCESS, segment->move_owner_to(owner));
  EXPECT_TRUE(owner.is_valid());
  EXPECT_TRUE(owner.is_sealed());
  EXPECT_EQ(0, MEMCMP(owner.get_buf(), payload, payload_len));
  EXPECT_EQ(0, limiter.get_pending_bytes());
  LogBufferSegment::destroy_list(segment);
}

TEST(TestPalfLogBuffer, ordered_segments_freeze_to_group_write_buf)
{
  const char first_payload[] = "first";
  const char second_payload[] = "second";
  const int64_t first_len = sizeof(first_payload) - 1;
  const int64_t second_len = sizeof(second_payload) - 1;
  const SCN scn = SCN::base_scn();
  const LSN group_lsn(4096);
  const LSN first_lsn = group_lsn + LogGroupEntryHeader::HEADER_SER_SIZE;
  const LSN second_lsn = first_lsn + LogEntryHeader::HEADER_SER_SIZE + first_len;
  PalfLogBuffer first_owner;
  PalfLogBuffer second_owner;
  LogPendingBufferLimiter limiter(1024);
  LogBufferSegment *first_segment = NULL;
  LogBufferSegment *second_segment = NULL;

  ASSERT_EQ(OB_SUCCESS, first_owner.init(64, LogEntryHeader::HEADER_SER_SIZE));
  MEMCPY(first_owner.get_buf(), first_payload, first_len);
  ASSERT_EQ(OB_SUCCESS, first_owner.seal(first_len));
  ASSERT_EQ(OB_SUCCESS, LogBufferSegment::create_normal(first_lsn, scn, first_owner,
      &limiter, 64, first_segment));

  ASSERT_EQ(OB_SUCCESS, second_owner.init(64, LogEntryHeader::HEADER_SER_SIZE));
  MEMCPY(second_owner.get_buf(), second_payload, second_len);
  ASSERT_EQ(OB_SUCCESS, second_owner.seal(second_len));
  ASSERT_EQ(OB_SUCCESS, LogBufferSegment::create_normal(second_lsn, scn, second_owner,
      &limiter, 64, second_segment));

  const int64_t first_entry_size = first_segment->get_entry_size();
  const int64_t second_entry_size = second_segment->get_entry_size();
  const int64_t first_checksum = first_segment->get_data_checksum();
  const int64_t second_checksum = second_segment->get_data_checksum();
  const int64_t group_data_len = first_entry_size + second_entry_size;

  LogTask task;
  // A later append may reach the task before its first entry initializes the
  // group.  Insert in that order to exercise the ordered intrusive list.
  ASSERT_EQ(OB_SUCCESS, task.insert_segment(second_segment));
  second_segment = NULL;
  task.update_data_len(second_entry_size);
  task.inc_update_max_scn(scn);
  task.ref(second_entry_size);

  LogTaskHeaderInfo header_info;
  header_info.begin_lsn_ = group_lsn;
  header_info.log_id_ = 1;
  header_info.min_scn_ = scn;
  header_info.max_scn_ = scn;
  header_info.data_len_ = first_entry_size;
  ASSERT_EQ(OB_SUCCESS, task.set_initial_header_info(header_info));
  ASSERT_EQ(OB_SUCCESS, task.insert_segment(first_segment));
  first_segment = NULL;
  task.ref(first_entry_size);
  ASSERT_EQ(OB_SUCCESS, task.try_freeze(group_lsn
      + LogGroupEntryHeader::HEADER_SER_SIZE + group_data_len));
  ASSERT_EQ(0, task.get_ref_cnt());

  int64_t expected_checksum = 0;
  expected_checksum = ob_crc64(expected_checksum, &first_checksum, sizeof(first_checksum));
  expected_checksum = ob_crc64(expected_checksum, &second_checksum, sizeof(second_checksum));
  int64_t actual_checksum = 0;
  ASSERT_EQ(OB_SUCCESS, task.calculate_group_checksum(actual_checksum));
  EXPECT_EQ(expected_checksum, actual_checksum);

  LogGroupEntryHeader group_header;
  ASSERT_EQ(OB_SUCCESS, group_header.generate(false, group_data_len, scn, 1,
      LSN(0), actual_checksum));
  LogGroupWriteBuf group_write_buf;
  ASSERT_EQ(OB_SUCCESS, task.detach_group_write_buf(group_header, group_write_buf));
  LogWriteBuf write_buf;
  ASSERT_EQ(OB_SUCCESS, group_write_buf.build_write_buf(write_buf));
  ASSERT_EQ(LogGroupEntryHeader::HEADER_SER_SIZE + group_data_len,
      write_buf.get_total_size());

  std::vector<char> serialized(write_buf.get_total_size());
  write_buf.memcpy_to_continous_memory(serialized.data());
  int64_t pos = LogGroupEntryHeader::HEADER_SER_SIZE;
  LogEntryHeader first_header;
  ASSERT_EQ(OB_SUCCESS, first_header.deserialize(serialized.data(), serialized.size(), pos));
  ASSERT_EQ(first_len, first_header.get_data_len());
  EXPECT_EQ(0, MEMCMP(serialized.data() + pos, first_payload, first_len));
  pos += first_len;
  LogEntryHeader second_header;
  ASSERT_EQ(OB_SUCCESS, second_header.deserialize(serialized.data(), serialized.size(), pos));
  ASSERT_EQ(second_len, second_header.get_data_len());
  EXPECT_EQ(0, MEMCMP(serialized.data() + pos, second_payload, second_len));
  pos += second_len;
  EXPECT_EQ(serialized.size(), pos);

  group_write_buf.reset();
  EXPECT_EQ(0, limiter.get_pending_bytes());
}

TEST(TestPalfLogBuffer, padding_uses_virtual_fill_fragment)
{
  const int64_t padding_body_size = 8192;
  const SCN scn = SCN::base_scn();
  LogPendingBufferLimiter limiter(1024);
  LogBufferSegment *padding_segment = NULL;
  ASSERT_EQ(OB_SUCCESS, LogBufferSegment::prepare_padding(&limiter, padding_segment));
  ASSERT_EQ(OB_SUCCESS, padding_segment->bind_padding(LSN(100), scn, padding_body_size));
  EXPECT_EQ(LogEntryHeader::PADDING_LOG_ENTRY_SIZE, limiter.get_pending_bytes());

  LogGroupEntryHeader group_header;
  ASSERT_EQ(OB_SUCCESS, group_header.generate(true, padding_body_size, scn, 2, LSN(0), 0));
  LogGroupWriteBuf group_write_buf;
  ASSERT_EQ(OB_SUCCESS, group_write_buf.init(group_header, padding_segment,
      padding_segment, padding_body_size));
  padding_segment = NULL;

  LogWriteBuf write_buf;
  ASSERT_EQ(OB_SUCCESS, group_write_buf.build_write_buf(write_buf));
  ASSERT_EQ(3, write_buf.get_buf_count());
  EXPECT_EQ(LogGroupEntryHeader::HEADER_SER_SIZE + padding_body_size,
      write_buf.get_total_size());
  const char *fill_buf = NULL;
  int64_t fill_len = 0;
  bool is_fill = false;
  char fill_char = 1;
  ASSERT_EQ(OB_SUCCESS, write_buf.get_write_buf(2, fill_buf, fill_len, is_fill, fill_char));
  EXPECT_TRUE(is_fill);
  EXPECT_TRUE(NULL == fill_buf);
  EXPECT_EQ(PADDING_LOG_CONTENT_CHAR, fill_char);
  EXPECT_EQ(padding_body_size - LogEntryHeader::PADDING_LOG_ENTRY_SIZE, fill_len);

  std::vector<char> serialized(write_buf.get_total_size(), 1);
  write_buf.memcpy_to_continous_memory(serialized.data());
  for (int64_t i = LogGroupEntryHeader::HEADER_SER_SIZE
      + LogEntryHeader::PADDING_LOG_ENTRY_SIZE; i < serialized.size(); ++i) {
    ASSERT_EQ(PADDING_LOG_CONTENT_CHAR, serialized[i]);
  }
  group_write_buf.reset();
  EXPECT_EQ(0, limiter.get_pending_bytes());
}

TEST(TestPalfLogBuffer, allocator_prepares_padding_before_assigning_lsn)
{
  const LSN start_lsn(PALF_BLOCK_SIZE - 1024);
  LSNAllocator allocator;
  ASSERT_EQ(OB_SUCCESS, allocator.init(0, SCN::base_scn(), start_lsn));
  LogPendingBufferLimiter limiter(1024);
  LogBufferSegment *padding_segment = NULL;
  LSN lsn;
  int64_t log_id = OB_INVALID_LOG_ID;
  SCN scn;
  bool is_new_log = false;
  bool need_padding = false;
  int64_t padding_len = 0;

  ASSERT_EQ(OB_SUCCESS, allocator.alloc_lsn_scn(SCN::base_scn(), 100, 100,
      start_lsn + 4 * 1024 * 1024, lsn, log_id, scn, is_new_log, need_padding,
      padding_len, &limiter, &padding_segment));
  EXPECT_EQ(start_lsn, lsn);
  EXPECT_TRUE(is_new_log);
  EXPECT_TRUE(need_padding);
  EXPECT_EQ(1024, padding_len);
  ASSERT_TRUE(NULL != padding_segment);
  EXPECT_EQ(LogEntryHeader::PADDING_LOG_ENTRY_SIZE, limiter.get_pending_bytes());
  LogBufferSegment::destroy_list(padding_segment);
  EXPECT_EQ(0, limiter.get_pending_bytes());

  LSNAllocator limited_allocator;
  ASSERT_EQ(OB_SUCCESS, limited_allocator.init(0, SCN::base_scn(), start_lsn));
  LogPendingBufferLimiter limited_limiter(1);
  padding_segment = NULL;
  ASSERT_EQ(OB_EAGAIN, limited_allocator.alloc_lsn_scn(SCN::base_scn(), 100, 100,
      start_lsn + 4 * 1024 * 1024, lsn, log_id, scn, is_new_log, need_padding,
      padding_len, &limited_limiter, &padding_segment));
  EXPECT_TRUE(NULL == padding_segment);
  LSN current_end_lsn;
  ASSERT_EQ(OB_SUCCESS, limited_allocator.get_curr_end_lsn(current_end_lsn));
  EXPECT_EQ(start_lsn, current_end_lsn);
}

TEST(TestPalfLogBuffer, gather_fragmented_write_into_dio_buffer)
{
  LogDIOAlignedBuf aligned_buf;
  ASSERT_EQ(OB_SUCCESS, aligned_buf.init(LOG_DIO_ALIGN_SIZE, 2 * LOG_DIO_ALIGN_SIZE));

  const char first[] = "abc";
  LogWriteBuf first_write;
  ASSERT_EQ(OB_SUCCESS, first_write.push_back(first, sizeof(first) - 1));
  char *output = NULL;
  int64_t output_len = 0;
  offset_t offset = 0;
  ASSERT_EQ(OB_SUCCESS, aligned_buf.align_buf(first_write, output, output_len, offset));
  ASSERT_EQ(LOG_DIO_ALIGN_SIZE, output_len);
  EXPECT_EQ(0, offset);
  EXPECT_EQ(0, MEMCMP(output, first, sizeof(first) - 1));
  aligned_buf.truncate_buf();

  const char second[] = "de";
  const char third[] = "fg";
  LogWriteBuf fragmented_write;
  ASSERT_EQ(OB_SUCCESS, fragmented_write.push_back(second, sizeof(second) - 1));
  ASSERT_EQ(OB_SUCCESS, fragmented_write.push_fill('x', 3));
  ASSERT_EQ(OB_SUCCESS, fragmented_write.push_back(third, sizeof(third) - 1));
  offset = sizeof(first) - 1;
  output = NULL;
  output_len = 0;
  ASSERT_EQ(OB_SUCCESS,
      aligned_buf.align_buf(fragmented_write, output, output_len, offset));
  ASSERT_EQ(LOG_DIO_ALIGN_SIZE, output_len);
  EXPECT_EQ(0, offset);
  const char expected[] = "abcdexxxfg";
  EXPECT_EQ(0, MEMCMP(output, expected, sizeof(expected) - 1));
  aligned_buf.truncate_buf();
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_palf_log_buffer.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
