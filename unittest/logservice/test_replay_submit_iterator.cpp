/*
 * Copyright (c) 2026 OceanBase.
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

#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "logservice/palf/log_entry_header.h"
#include "logservice/palf/log_reader_utils.h"
#include "logservice/replayservice/ob_replay_status.h"

namespace oceanbase
{
namespace logservice
{

using namespace common;
using namespace palf;
using namespace share;

class RecordingLogStorage : public ILogStorage
{
public:
  RecordingLogStorage()
      : ILogStorage(ILogStorageType::DISK_STORAGE), bytes_(), io_contexts_()
  {}

  int set_payload(const char *payload, const SCN &scn)
  {
    int ret = OB_SUCCESS;
    LogEntryHeader header;
    const int64_t payload_len = static_cast<int64_t>(std::strlen(payload));
    if (OB_FAIL(header.generate_header(payload, payload_len, scn))) {
    } else {
      bytes_.resize(header.get_serialize_size() + payload_len);
      int64_t pos = 0;
      if (OB_FAIL(header.serialize(bytes_.data(), bytes_.size(), pos))) {
      } else {
        std::memcpy(bytes_.data() + pos, payload, payload_len);
      }
    }
    return ret;
  }

  int pread(const LSN &lsn,
            const int64_t in_read_size,
            ReadBuf &read_buf,
            int64_t &out_read_size,
            LogIOContext &io_ctx) override
  {
    int ret = OB_SUCCESS;
    char io_context_buf[1024] = {0};
    (void)io_ctx.to_string(io_context_buf, sizeof(io_context_buf));
    io_contexts_.push_back(io_context_buf);
    if (!lsn.is_valid() || in_read_size <= 0 || !read_buf.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
    } else if (lsn.val_ >= bytes_.size()) {
      ret = OB_ERR_OUT_OF_UPPER_BOUND;
    } else {
      out_read_size = std::min<int64_t>(in_read_size, bytes_.size() - lsn.val_);
      std::memcpy(read_buf.buf_, bytes_.data() + lsn.val_, out_read_size);
    }
    return ret;
  }

  LSN end_lsn() const { return LSN(bytes_.size()); }

  bool observed_replay_io() const
  {
    bool observed = false;
    for (const std::string &context : io_contexts_) {
      if (std::string::npos != context.find("user:\"REPLAY\"")) {
        observed = true;
        break;
      }
    }
    return observed;
  }

private:
  std::vector<char> bytes_;
  std::vector<std::string> io_contexts_;
};

class IteratorFixture
{
public:
  IteratorFixture() : current_storage_(nullptr), open_count_(0), destroy_count_({0, 0, 0, 0}) {}

  int open(const LSN &start_lsn, PalfBufferIterator &iterator)
  {
    int ret = OB_SUCCESS;
    if (nullptr == current_storage_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (iterator.is_inited()) {
      ret = iterator.reuse(start_lsn);
    } else {
      RecordingLogStorage *storage = current_storage_;
      GetFileEndLSN get_end_lsn([storage]() { return storage->end_lsn(); });
      if (OB_FAIL(iterator.init(start_lsn, get_end_lsn, storage))) {
      } else {
        const int64_t generation = open_count_++;
        DestroyStorageFunctor destroy_functor([this, generation]() {
          ++destroy_count_.at(generation);
        });
        if (OB_FAIL(iterator.set_destroy_iterator_storage_functor(destroy_functor))) {
          iterator.destroy();
        }
      }
    }
    return ret;
  }

  void use(RecordingLogStorage &storage) { current_storage_ = &storage; }
  int64_t destroy_count(const int64_t generation) const { return destroy_count_.at(generation); }

private:
  RecordingLogStorage *current_storage_;
  int64_t open_count_;
  std::array<int64_t, 4> destroy_count_;
};

class ReplaySubmitIteratorTestPeer
{
public:
  static ObReplayServiceSubmitTask &submit_task(ObReplayStatus &status)
  {
    return status.submit_log_task_;
  }

  static void set_iterator_opener(ObReplayServiceSubmitTask &task, IteratorFixture &fixture)
  {
    task.iterator_opener_for_test_ = [&fixture](const LSN &lsn, PalfBufferIterator &iterator) {
      return fixture.open(lsn, iterator);
    };
  }

  static bool is_iterator_inited(const ObReplayServiceSubmitTask &task)
  {
    return task.iterator_.is_inited();
  }

  static void mark_status_inited(ObReplayStatus &status)
  {
    status.is_inited_ = true;
  }

  static void set_local_replay_enabled(ObReplayStatus &status, const bool enabled)
  {
    status.local_replay_enabled_ = enabled;
  }
};

TEST(ReplaySubmitIteratorLifecycle, SameTaskRebuildUsesReplayIo)
{
  RecordingLogStorage storage_a;
  RecordingLogStorage storage_b;
  ASSERT_EQ(OB_SUCCESS, storage_a.set_payload("payload-a", SCN::base_scn()));
  ASSERT_EQ(OB_SUCCESS, storage_b.set_payload("payload-b", SCN::scn_inc(SCN::base_scn())));

  ObReplayStatus status;
  ObReplayServiceSubmitTask &task = ReplaySubmitIteratorTestPeer::submit_task(status);
  IteratorFixture fixture;
  fixture.use(storage_a);
  ReplaySubmitIteratorTestPeer::set_iterator_opener(task, fixture);
  ASSERT_EQ(OB_SUCCESS, task.init(LSN(0), SCN::base_scn(), &status));

  const ObReplayServiceSubmitTask *const original_task = &task;
  const char *payload = nullptr;
  int64_t payload_size = 0;
  SCN payload_scn;
  LSN payload_lsn;
  ASSERT_EQ(OB_SUCCESS, task.get_log(payload, payload_size, payload_scn, payload_lsn));
  EXPECT_EQ("payload-a", std::string(payload, payload_size));
  EXPECT_TRUE(storage_a.observed_replay_io());
  EXPECT_EQ(1, task.get_iterator_generation());

  task.release_iterator();
  task.release_iterator();
  EXPECT_FALSE(ReplaySubmitIteratorTestPeer::is_iterator_inited(task));
  EXPECT_EQ(1, fixture.destroy_count(0));

  fixture.use(storage_b);
  ASSERT_EQ(OB_SUCCESS, task.reset_iterator(LSN(0), SCN::scn_inc(SCN::base_scn())));
  EXPECT_EQ(original_task, &task);
  EXPECT_EQ(2, task.get_iterator_generation());
  ASSERT_EQ(OB_SUCCESS, task.get_log(payload, payload_size, payload_scn, payload_lsn));
  EXPECT_EQ("payload-b", std::string(payload, payload_size));
  EXPECT_TRUE(storage_b.observed_replay_io());

  task.release_iterator();
  EXPECT_EQ(1, fixture.destroy_count(1));
}

TEST(ReplaySubmitIteratorRelease, ReleaseStateMatrix)
{
  RecordingLogStorage storage;
  ASSERT_EQ(OB_SUCCESS, storage.set_payload("payload", SCN::base_scn()));

  ObReplayStatus status;
  ReplaySubmitIteratorTestPeer::mark_status_inited(status);
  ObReplayServiceSubmitTask &task = ReplaySubmitIteratorTestPeer::submit_task(status);
  IteratorFixture fixture;
  fixture.use(storage);
  ReplaySubmitIteratorTestPeer::set_iterator_opener(task, fixture);
  ASSERT_EQ(OB_SUCCESS, task.init(LSN(0), SCN::base_scn(), &status));

  SubmitIteratorReleaseState state = SubmitIteratorReleaseState::RELEASED;
  int64_t generation = 0;
  ASSERT_EQ(OB_STATE_NOT_MATCH, status.try_release_submit_iterator(state, generation));
  EXPECT_EQ(SubmitIteratorReleaseState::LOCAL_REPLAY_ENABLED, state);
  EXPECT_TRUE(ReplaySubmitIteratorTestPeer::is_iterator_inited(task));
  EXPECT_EQ(0, fixture.destroy_count(0));

  ReplaySubmitIteratorTestPeer::set_local_replay_enabled(status, false);
  ASSERT_TRUE(task.acquire_lease());
  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state, generation));
  EXPECT_EQ(SubmitIteratorReleaseState::TASK_BUSY, state);
  EXPECT_TRUE(ReplaySubmitIteratorTestPeer::is_iterator_inited(task));
  EXPECT_EQ(0, fixture.destroy_count(0));
  ASSERT_TRUE(task.revoke_lease());

  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state, generation));
  EXPECT_EQ(SubmitIteratorReleaseState::RELEASED, state);
  EXPECT_FALSE(ReplaySubmitIteratorTestPeer::is_iterator_inited(task));
  EXPECT_EQ(1, fixture.destroy_count(0));

  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state, generation));
  EXPECT_EQ(SubmitIteratorReleaseState::RELEASED, state);
  EXPECT_EQ(1, fixture.destroy_count(0));
}

} // namespace logservice
} // namespace oceanbase
