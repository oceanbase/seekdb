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

#include "lib/checksum/ob_parity_check.h"          // ob_crc64
#define private public
#include "logservice/palf/log_entry.h"
#include "logservice/ob_log_base_header.h"  // ObLogBaseHeader
#include "logservice/palf/log_group_buffer.h"
#include "logservice/palf/log_group_entry.h"
#include "logservice/palf/log_writer_utils.h"
#include "share/rc/ob_server_runtime.h"
#undef private

#include <gtest/gtest.h>
#include <random>

namespace oceanbase
{
using namespace common;
using namespace palf;

namespace unittest
{

TEST(TestLogGroupEntryHeader, test_group_entry_header_wrap_checksum)
{
  const int64_t BUFSIZE = 1 << 21;
  LogGroupEntryHeader header;
  LogEntryHeader log_entry_header;
  int64_t group_entry_header_size = header.get_serialize_size();
  int64_t log_entry_header_size = log_entry_header.get_serialize_size();
  int64_t total_header_size = group_entry_header_size + log_entry_header_size;
  char buf[BUFSIZE];
  char ptr[BUFSIZE] = "helloworld";
  // Data section
  memcpy(buf + total_header_size, ptr, strlen(ptr));

  bool is_padding_log = false;
  const char *data = buf + total_header_size;
  int64_t data_len = strlen(ptr);
  memcpy(buf + total_header_size + data_len + log_entry_header_size, ptr, strlen(ptr));
  int64_t min_timestamp = 0;
  share::SCN max_scn = share::SCN::min_scn();
  int64_t log_id = 1;
  LSN committed_lsn;
  committed_lsn.val_ = 1;
  int64_t log_checksum = 0;

  // test LogEntry and LogEntryHeader
  EXPECT_EQ(OB_SUCCESS, log_entry_header.generate_header(data, data_len, share::SCN::base_scn()));
  int64_t tmp_pos = 0, new_pos = 0;
  EXPECT_EQ(OB_SUCCESS,
            log_entry_header.serialize(buf + group_entry_header_size, BUFSIZE, tmp_pos));
  EXPECT_EQ(tmp_pos, log_entry_header_size);
  EXPECT_EQ(OB_SUCCESS,
            log_entry_header.serialize(buf + total_header_size + data_len, BUFSIZE, new_pos));
  EXPECT_EQ(new_pos, log_entry_header_size);
  // test LogGroupEntryHeader and LogEntry
  LogWriteBuf write_buf;

  int64_t group_log_data_len = 0;
  int64_t group_log_len = group_entry_header_size + (log_entry_header_size + data_len);
  for (int64_t sub_val = 1; sub_val < group_log_len; ++sub_val) {
    write_buf.reset();
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf, sub_val));
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf + sub_val,  group_log_len - sub_val));
    group_log_data_len = group_log_len - group_entry_header_size;
    PALF_LOG(INFO, "before group_header generate", K(group_log_data_len), K(write_buf), K(sub_val));
    EXPECT_EQ(OB_SUCCESS,
              header.generate(is_padding_log, write_buf, group_log_data_len,
                              max_scn, log_id, committed_lsn, log_checksum));
  }

  is_padding_log = true;
  for (int64_t sub_val = 1; sub_val < group_log_len; ++sub_val) {
    write_buf.reset();
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf, sub_val));
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf + sub_val,  group_log_len - sub_val));
    group_log_data_len = group_log_len - group_entry_header_size;
    PALF_LOG(INFO, "before group_header generate", K(group_log_data_len), K(write_buf), K(sub_val));
    EXPECT_EQ(OB_SUCCESS,
              header.generate(is_padding_log, write_buf, group_log_data_len,
                              max_scn, log_id, committed_lsn, log_checksum));
  }

  group_log_len = group_entry_header_size + 2 * (log_entry_header_size + data_len);
  for (int64_t sub_val = 1; sub_val < group_log_len; ++sub_val) {
    write_buf.reset();
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf, sub_val));
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf + sub_val,  group_log_len - sub_val));
    group_log_data_len = group_log_len - group_entry_header_size;
    PALF_LOG(INFO, "before group_header generate", K(group_log_data_len), K(write_buf), K(sub_val));
    EXPECT_EQ(OB_SUCCESS,
              header.generate(is_padding_log, write_buf, group_log_data_len,
                              max_scn, log_id, committed_lsn, log_checksum));
  }

  is_padding_log = true;
  for (int64_t sub_val = 1; sub_val < group_log_len; ++sub_val) {
    write_buf.reset();
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf, sub_val));
    EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf + sub_val,  group_log_len - sub_val));
    group_log_data_len = group_log_len - group_entry_header_size;
    PALF_LOG(INFO, "before group_header generate", K(group_log_data_len), K(write_buf), K(sub_val));
    EXPECT_EQ(OB_SUCCESS,
              header.generate(is_padding_log, write_buf, group_log_data_len,
                              max_scn, log_id, committed_lsn, log_checksum));
  }

  is_padding_log = true;
  EXPECT_EQ(OB_SUCCESS,
            header.generate(is_padding_log, write_buf, group_log_data_len,
                            max_scn, log_id, committed_lsn, log_checksum));
}

TEST(TestLogGroupEntryHeader, test_log_group_entry_header)
{
  const int64_t BUFSIZE = 1 << 21;
  LogGroupEntryHeader header;
  LogEntryHeader log_entry_header;
  int64_t log_group_entry_header_size = header.get_serialize_size();
  int64_t log_entry_header_size = log_entry_header.get_serialize_size();
  int64_t header_size = log_group_entry_header_size + log_entry_header_size;
  char buf[BUFSIZE];
  char ptr[BUFSIZE] = "helloworld";
  // Data section
  memcpy(buf + header_size, ptr, strlen(ptr));

  LogType log_type = palf::LOG_PADDING;
  const bool is_padding_log = (LOG_PADDING == log_type) ? true : false;
  const char *data = buf + header_size;
  int64_t data_len = strlen(ptr);
  int64_t min_timestamp = 0;
  share::SCN max_scn = share::SCN::min_scn();
  int64_t log_id = 1;
  LSN committed_lsn;
  committed_lsn.val_ = 1;
  int64_t log_checksum = 0;

  // test LogEntry and LogEntryHeader
  LogEntry log_entry;
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_entry_header.generate_header(NULL, 0, share::SCN::base_scn()));
  EXPECT_EQ(OB_SUCCESS, log_entry_header.generate_header(data, data_len, share::SCN::base_scn()));
  log_entry.header_ = log_entry_header;
  log_entry.buf_ = data;
  int64_t tmp_pos = 0;
  EXPECT_EQ(OB_SUCCESS,
            log_entry_header.serialize(buf + log_group_entry_header_size, BUFSIZE, tmp_pos));
  EXPECT_EQ(tmp_pos, log_entry_header_size);
  // test LogGroupEntryHeader and LogEntry
  LogWriteBuf write_buf;
  EXPECT_EQ(OB_SUCCESS, write_buf.push_back(buf, data_len + header_size));
  PALF_LOG(INFO, "runlin trace", K(tmp_pos), K(log_entry), K(write_buf),
           K(write_buf.get_total_size()));
  max_scn.reset();
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            header.generate(is_padding_log, write_buf, data_len + log_entry_header_size,
                            max_scn, log_id, committed_lsn, log_checksum));
  max_scn.set_base();
  int64_t defalut_acc = 10;
  min_timestamp = 1;
  EXPECT_EQ(OB_SUCCESS,
            header.generate(is_padding_log, write_buf, data_len + log_entry_header_size,
                            max_scn, log_id, committed_lsn, log_checksum));
  header.update_accumulated_checksum(defalut_acc);
  header.update_header_checksum();
  EXPECT_TRUE(
      header.check_integrity(buf + log_group_entry_header_size, data_len + log_entry_header_size));
  EXPECT_TRUE(header.is_valid());
  EXPECT_EQ(data_len + log_entry_header_size, header.get_data_len());
  EXPECT_EQ(max_scn, header.get_max_scn());
  EXPECT_EQ(log_id, header.get_log_id());
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, header.serialize(buf, BUFSIZE, pos));
  EXPECT_EQ(pos, header.get_serialize_size());
  pos = 0;
  LogGroupEntryHeader header1;
  EXPECT_EQ(OB_SUCCESS, header1.deserialize(buf, BUFSIZE, pos));
  EXPECT_TRUE(
      header1.check_integrity(buf + log_group_entry_header_size, data_len + log_entry_header_size));
  EXPECT_TRUE(header1 == header);
  EXPECT_TRUE(header1.check_header_integrity());
  EXPECT_TRUE(
      header1.check_integrity(buf + log_group_entry_header_size, data_len + log_entry_header_size));

  LogGroupEntry log_group_entry, log_group_entry1, log_group_entry2;
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_group_entry.generate(header, NULL));
  EXPECT_EQ(OB_SUCCESS, log_group_entry.generate(header, buf + log_group_entry_header_size));
  EXPECT_TRUE(log_group_entry.is_valid());
  EXPECT_EQ(OB_SUCCESS, log_group_entry1.shallow_copy(log_group_entry));
  EXPECT_EQ(log_group_entry1.get_header(), log_group_entry.get_header());
  EXPECT_EQ(log_group_entry1.get_header_size(), log_group_entry.get_header_size());
  EXPECT_EQ(data_len + log_entry_header_size, log_group_entry.get_data_len());
  EXPECT_EQ(max_scn, log_group_entry.get_scn());
  EXPECT_EQ(committed_lsn, log_group_entry.get_committed_end_lsn());
  pos = 0;
  EXPECT_EQ(OB_SUCCESS, log_group_entry.serialize(buf, BUFSIZE, pos));
  pos = 0;
  EXPECT_EQ(OB_SUCCESS, log_group_entry2.deserialize(buf, BUFSIZE, pos));
  EXPECT_TRUE(log_group_entry2.check_integrity());
}

TEST(TestPaddingLogEntry, test_invalid_padding_log_entry)
{
  LogEntryHeader header;
  char buf[1024];
  EXPECT_EQ(OB_INVALID_ARGUMENT, header.generate_padding_header_(NULL, 1, 1, share::SCN::min_scn()));
  EXPECT_EQ(OB_INVALID_ARGUMENT, header.generate_padding_header_(buf, 0, 1, share::SCN::min_scn()));
  EXPECT_EQ(OB_INVALID_ARGUMENT, header.generate_padding_header_(buf, 1, 0, share::SCN::min_scn()));
  EXPECT_EQ(OB_INVALID_ARGUMENT, header.generate_padding_header_(buf, 1, 1, share::SCN::invalid_scn()));
  EXPECT_EQ(OB_SUCCESS, header.generate_padding_header_(buf, 1, 1, share::SCN::min_scn()));

  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(0, share::SCN::min_scn(), buf, 1));
  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(1, share::SCN::invalid_scn(), buf, 1));
  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(1, share::SCN::min_scn(), NULL, 1));
  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(1, share::SCN::min_scn(), buf, 0));
  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(1, share::SCN::min_scn(), buf, 2));
  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(2, share::SCN::min_scn(), buf, 1));
  EXPECT_EQ(OB_INVALID_ARGUMENT, LogEntryHeader::generate_padding_log_buf(1, share::SCN::min_scn(), buf, 1));
  logservice::ObLogBaseHeader base_header;
  const int64_t min_padding_valid_data_len = header.get_serialize_size() + base_header.get_serialize_size();
  EXPECT_EQ(OB_SUCCESS, LogEntryHeader::generate_padding_log_buf(1+min_padding_valid_data_len, share::SCN::min_scn(), buf, min_padding_valid_data_len));
}

TEST(TestLogBaseHeader, serialize_and_restore)
{
  char buf[128] = {'\0'};
  logservice::ObLogBaseHeader encoded(logservice::ObLogBaseType::TRANS_SERVICE_LOG_BASE_TYPE,
                                      logservice::ObReplayBarrierType::STRICT_BARRIER,
                                      12345);
  ASSERT_TRUE(encoded.is_valid());
  EXPECT_TRUE(encoded.need_pre_replay_barrier());
  EXPECT_TRUE(encoded.need_post_replay_barrier());
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, encoded.serialize(buf, sizeof(buf), pos));
  EXPECT_EQ(encoded.get_serialize_size(), pos);

  logservice::ObLogBaseHeader decoded;
  int64_t decode_pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(buf, pos, decode_pos));
  EXPECT_EQ(pos, decode_pos);
  EXPECT_TRUE(decoded.is_valid());
  EXPECT_EQ(logservice::ObLogBaseType::TRANS_SERVICE_LOG_BASE_TYPE, decoded.get_log_type());
  EXPECT_TRUE(decoded.need_pre_replay_barrier());
  EXPECT_TRUE(decoded.need_post_replay_barrier());
  EXPECT_EQ(12345, decoded.get_replay_hint());
}

TEST(TestPaddingLogEntry, test_padding_log_entry)
{
  PALF_LOG(INFO, "test_padding_log_entry");
  LogEntry padding_log_entry;
  const int64_t padding_data_len = MAX_LOG_BODY_SIZE;
  share::SCN padding_group_scn;
  padding_group_scn.convert_for_logservice(ObTimeUtility::current_time_ns());
  LogEntryHeader padding_log_entry_header;
  char base_header_data[1024] = {'\0'};
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::PADDING_LOG_BASE_TYPE,
                                          logservice::ObReplayBarrierType::NO_NEED_BARRIER);
  int64_t serialize_pos = 0;
  EXPECT_EQ(OB_SUCCESS, base_header.serialize(base_header_data, 1024, serialize_pos));
  // Generate valid data for padding log entry
  EXPECT_EQ(OB_SUCCESS, padding_log_entry_header.generate_padding_header_(
      base_header_data, base_header.get_serialize_size(),
      padding_data_len-LogEntryHeader::HEADER_SER_SIZE, padding_group_scn));
  EXPECT_EQ(true, padding_log_entry_header.check_integrity(base_header_data, padding_data_len));

  // padding group log format
  // | GroupHeader | EntryHeader | BaseHeader | '\0' |
  LogGroupEntry padding_group_entry;
  LogGroupEntryHeader padding_group_entry_header;

  const int64_t padding_buffer_len = MAX_LOG_BUFFER_SIZE;
  char *padding_buffer = reinterpret_cast<char *>(ob_malloc(padding_buffer_len, "unittest"));
  ASSERT_NE(nullptr, padding_buffer);
  memset(padding_buffer, PADDING_LOG_CONTENT_CHAR, padding_buffer_len);
  {
    // Copy the data from base_data to the corresponding position
    memcpy(padding_buffer+padding_group_entry_header.get_serialize_size() + LogEntryHeader::HEADER_SER_SIZE, base_header_data, 1024);
    padding_log_entry.header_ = padding_log_entry_header;
    padding_log_entry.buf_ = padding_buffer+padding_group_entry_header.get_serialize_size() + LogEntryHeader::HEADER_SER_SIZE;
    // Construct a valid padding_log_entry for subsequent serialization operations
    EXPECT_EQ(true, padding_log_entry.check_integrity());
    EXPECT_EQ(true, padding_log_entry.header_.is_padding_log_());
  }
  {
    LogEntry deserialize_padding_log_entry;
    const int64_t tmp_padding_buffer_len = MAX_LOG_BUFFER_SIZE;
    char *tmp_padding_buffer = reinterpret_cast<char *>(ob_malloc(tmp_padding_buffer_len, "unittest"));
    ASSERT_NE(nullptr, tmp_padding_buffer);
    int64_t pos = 0;
    EXPECT_EQ(OB_SUCCESS, padding_log_entry.serialize(tmp_padding_buffer, tmp_padding_buffer_len, pos));
    pos = 0;
    EXPECT_EQ(OB_SUCCESS, deserialize_padding_log_entry.deserialize(tmp_padding_buffer, tmp_padding_buffer_len, pos));
    EXPECT_EQ(true, deserialize_padding_log_entry.check_integrity());
    EXPECT_EQ(padding_log_entry.header_.data_checksum_, deserialize_padding_log_entry.header_.data_checksum_);
    ob_free(tmp_padding_buffer);
    tmp_padding_buffer = nullptr;
  }

  serialize_pos = padding_group_entry_header.get_serialize_size();
  // Copy LogEntry to specified position in padding_buffer
  EXPECT_EQ(OB_SUCCESS, padding_log_entry.serialize(padding_buffer, padding_buffer_len, serialize_pos));

  LogWriteBuf write_buf;
  EXPECT_EQ(OB_SUCCESS, write_buf.push_back(padding_buffer, padding_buffer_len));
  bool is_padding_log = true;
  int64_t data_checksum = 0;
  EXPECT_EQ(OB_SUCCESS, padding_group_entry_header.generate(is_padding_log, write_buf, padding_data_len,  padding_group_scn, 1, LSN(0), data_checksum));
  padding_group_entry_header.update_accumulated_checksum(0);
  padding_group_entry_header.update_header_checksum();
  padding_group_entry.header_ = padding_group_entry_header;
  padding_group_entry.buf_ = padding_buffer + padding_group_entry_header.get_serialize_size();
  EXPECT_EQ(true, padding_group_entry.check_integrity());
  EXPECT_EQ(true, padding_group_entry.header_.is_padding_log());
  // Validate deserialization of LogEntry
  {
    int64_t pos = 0;
    LogEntry tmp_padding_log_entry;
    EXPECT_EQ(OB_SUCCESS, tmp_padding_log_entry.deserialize(padding_group_entry.buf_, padding_group_entry.get_data_len(), pos));
    EXPECT_EQ(pos, padding_group_entry.get_data_len());
    EXPECT_EQ(true, tmp_padding_log_entry.check_integrity());
    EXPECT_EQ(true, tmp_padding_log_entry.header_.is_padding_log_());
    logservice::ObLogBaseHeader tmp_base_header;
    pos = 0;
    EXPECT_EQ(OB_SUCCESS, tmp_base_header.deserialize(tmp_padding_log_entry.buf_, tmp_padding_log_entry.get_data_len(), pos));
    EXPECT_EQ(base_header.log_type_, logservice::ObLogBaseType::PADDING_LOG_BASE_TYPE);
  }

  char *serialize_buffer = reinterpret_cast<char *>(ob_malloc(padding_buffer_len, "unittest"));
  ASSERT_NE(nullptr, serialize_buffer);
  memset(serialize_buffer, PADDING_LOG_CONTENT_CHAR, padding_buffer_len);
  serialize_pos = 0;
  // Verify that the serialized data meets expectations
  EXPECT_EQ(OB_SUCCESS, padding_group_entry.serialize(serialize_buffer, padding_buffer_len, serialize_pos));
  EXPECT_EQ(serialize_pos, padding_data_len+padding_group_entry_header.get_serialize_size());

  LogGroupEntry deserialize_group_entry;
  serialize_pos = 0;
  EXPECT_EQ(OB_SUCCESS, deserialize_group_entry.deserialize(serialize_buffer, padding_buffer_len, serialize_pos));
  EXPECT_EQ(true, deserialize_group_entry.check_integrity());
  EXPECT_EQ(true, deserialize_group_entry.header_.is_padding_log());
  EXPECT_EQ(padding_group_entry.header_, deserialize_group_entry.header_);
  // Validate deserialization of LogEntry
  {
    int64_t pos = 0;
    LogEntry tmp_padding_log_entry;
    EXPECT_EQ(OB_SUCCESS, tmp_padding_log_entry.deserialize(deserialize_group_entry.buf_, padding_group_entry.get_data_len(), pos));
    EXPECT_EQ(pos, deserialize_group_entry.get_data_len());
    EXPECT_EQ(true, tmp_padding_log_entry.check_integrity());
    EXPECT_EQ(true, tmp_padding_log_entry.header_.is_padding_log_());
    logservice::ObLogBaseHeader tmp_base_header;
    pos = 0;
    EXPECT_EQ(OB_SUCCESS, tmp_base_header.deserialize(tmp_padding_log_entry.buf_, tmp_padding_log_entry.get_data_len(), pos));
    EXPECT_EQ(base_header.log_type_, logservice::ObLogBaseType::PADDING_LOG_BASE_TYPE);
  }

  LogGroupBuffer group_buffer;
  LSN start_lsn(0);

  // init MTL
  static share::ObServerRuntimeState runtime_state;
  share::g_server_runtime = &runtime_state;
  EXPECT_EQ(OB_SUCCESS, group_buffer.init(start_lsn));

  const int64_t padding_valid_data_len = deserialize_group_entry.get_header().get_serialize_size() + padding_log_entry_header.get_serialize_size() + base_header.get_serialize_size();
  // Fill valid padding log to group buffer, verify if data is equal
  // padding_buffer includes LogGruopEntryHeader, LogEntryHeader, ObLogBaseHeader, padding log body(is filled with '\0')
  EXPECT_EQ(OB_SUCCESS, group_buffer.fill_padding_body(start_lsn, serialize_buffer, padding_valid_data_len, padding_buffer_len));
  EXPECT_EQ(0, memcmp(group_buffer.data_buf_, serialize_buffer, deserialize_group_entry.get_serialize_size()));
  PALF_LOG(INFO, "runlin trace", K(group_buffer.data_buf_), K(serialize_buffer), K(padding_buffer_len), KP(group_buffer.data_buf_), KP(padding_buffer));
  ob_free(padding_buffer);
  padding_buffer = NULL;
  ob_free(serialize_buffer);
  serialize_buffer = NULL;

  group_buffer.destroy();
}

TEST(TestPaddingLogEntry, test_generate_padding_log_entry)
{
  PALF_LOG(INFO, "test_generate_padding_log_entry");
  LogEntry padding_log_entry;
  const int64_t padding_data_len = 1024;
  const share::SCN padding_scn = share::SCN::min_scn();
  const int64_t padding_log_entry_len = padding_data_len + LogEntryHeader::HEADER_SER_SIZE;
  char *out_buf = reinterpret_cast<char*>(ob_malloc(padding_log_entry_len, "unittest"));
  ASSERT_NE(nullptr, out_buf);
  LogEntryHeader padding_header;
  logservice::ObLogBaseHeader base_header(logservice::ObLogBaseType::PADDING_LOG_BASE_TYPE, logservice::ObReplayBarrierType::NO_NEED_BARRIER, 0);
  char base_header_buf[1024];
  memset(base_header_buf, 0, 1024);
  int64_t serialize_base_header_pos = 0;
  EXPECT_EQ(OB_SUCCESS, base_header.serialize(base_header_buf, 1024, serialize_base_header_pos));
  EXPECT_EQ(OB_SUCCESS, padding_header.generate_padding_header_(base_header_buf, base_header.get_serialize_size(), padding_data_len, padding_scn));
  EXPECT_EQ(true, padding_header.check_header_integrity());
  EXPECT_EQ(OB_SUCCESS, LogEntryHeader::generate_padding_log_buf(padding_data_len, padding_scn, out_buf, LogEntryHeader::PADDING_LOG_ENTRY_SIZE));
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, padding_log_entry.deserialize(out_buf, padding_log_entry_len, pos));
  EXPECT_EQ(true, padding_log_entry.check_integrity());
  EXPECT_EQ(true, padding_log_entry.header_.is_padding_log_());
  EXPECT_EQ(padding_log_entry.header_.data_checksum_, padding_header.data_checksum_);
  ob_free(out_buf);
  out_buf = nullptr;
}

void bit_flip(uint8_t *ptr, int len, int bit_count)
{
  // Ensure magic and version are not flipped
  const int arr_count = len * 8 - 32;
  std::vector<int> numbers(0, arr_count);
  numbers.resize(arr_count);
  for (int i = 0; i < arr_count; i++) {
    numbers[i] = i + 32;
  }
  std::random_device rd;
  auto rng = std::default_random_engine { rd() };
  std::shuffle(numbers.begin(), numbers.end(), rng);  // shuffle the order
  
  for (int i = 0; i < bit_count; ++i) {
    int pos = numbers[i];
    uint8_t mask = (1 << (pos%8));
    *(ptr+pos/8) ^= mask;
    PALF_LOG(INFO, "runlin trace bit flip", K(pos));
  }
}

TEST(TestBitFlip, test_log_entry_header)
{
  std::srand(ObTimeUtility::current_time());
  PALF_LOG(INFO, "test_bit_flip_log_entry_header");
  LogEntryHeader log_entry_header;
  const int header_len = sizeof(LogEntryHeader);
  constexpr int data_len = 1024;
  char data[data_len]; memset(data, 'c', data_len);
  EXPECT_EQ(OB_SUCCESS, log_entry_header.generate_header(data, data_len, share::SCN::base_scn()));
  PALF_LOG(INFO, "origin header", K(log_entry_header));
  const int count = 1 << 10;
  struct Pair {
    LogEntryHeader header;
    int bit_count;
  };
  int ret = OB_SUCCESS;
  std::vector<Pair> array;
  std::map<int, int> count_array;
  for (int i = 1; i <= 1; i++) {
    count_array.insert(std::pair<int, int>(i, 0));
    for (int j = 0; j < count; j++) {
      LogEntryHeader tmp_header = log_entry_header;
      uint8_t *ptr = reinterpret_cast<uint8_t*>(&tmp_header);
      bit_flip(ptr, header_len, i);
      bool bool_ret = tmp_header.check_header_integrity();
      EXPECT_EQ(false, bool_ret);
      if (bool_ret) {
        count_array[i] ++;
        array.push_back(Pair{tmp_header, i});
        PALF_LOG(ERROR, "print info", K(log_entry_header), K(tmp_header), K(j), K(i));
      }
    }
  }
  OB_LOGGER.set_file_name("print_info.log", true);
  OB_LOGGER.set_log_level("INFO");
  for (auto &p : count_array) {
    PALF_LOG(INFO, "runlin trace print", "bit_flip", p.first, "count", p.second);
  }
}

TEST(TestBitFlip, test_log_group_entry_header)
{
  std::srand(ObTimeUtility::current_time());
  PALF_LOG(INFO, "test_bit_flip_log_group_entry_header");
  LogGroupEntryHeader header;
  LogWriteBuf write_buf;
  constexpr int data_len = 1024;
  char data[data_len]; memset(data, 'c', data_len);
  write_buf.push_back(data, data_len);
  share::SCN max_scn = share::SCN::min_scn();
  int64_t log_id = 1;
  LSN committed_lsn;
  committed_lsn.val_ = 1;
  int64_t log_checksum = 0;
  EXPECT_EQ(OB_SUCCESS,
            header.generate(true, write_buf, data_len,
                            max_scn, log_id, committed_lsn, log_checksum));
  const int header_len = sizeof(LogGroupEntryHeader);
  header.update_header_checksum();
  PALF_LOG(INFO, "origin header", K(header));
  const int count = 1 << 10;
  for (int i = 0; i < count; i++) {
    LogGroupEntryHeader tmp_header = header;
    uint8_t *ptr = reinterpret_cast<uint8_t*>(&tmp_header);
    const int bit_count = 1;
    bit_flip(ptr, header_len, bit_count);
    PALF_LOG(INFO, "current header", K(header), K(tmp_header), K(i), K(bit_count));
    bool bool_ret = tmp_header.check_header_integrity();
    EXPECT_EQ(false, bool_ret);
    if (bool_ret) {
      assert(false);
    }
  }
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_log_entry_and_group_entry.log");
  system("rm -f print_info*");
  OB_LOGGER.set_file_name("test_log_entry_and_group_entry.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_log_entry_and_group_entry");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
