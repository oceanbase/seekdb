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
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "logservice/palf/log_entry_header.h"
#include "logservice/palf/log_reader_utils.h"
#include "gtest/gtest.h"

// Keep the production surface unchanged while checking the exact owned
// iterator and buffer in this module-local regression test.
#define private public
#define protected public
#include "logservice/replayservice/ob_log_replay_service.h"
#undef protected
#undef private

namespace oceanbase {
namespace logservice {

using namespace common;
using namespace palf;
using namespace share;

class RecordingLogStorage final : public ILogStorage {
public:
  RecordingLogStorage()
      : ILogStorage(ILogStorageType::DISK_STORAGE), bytes_(), io_contexts_(),
        read_sizes_() {}

  int set_payload(const char *payload, const SCN &scn) {
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

  int pread(const LSN &lsn, const int64_t in_read_size, ReadBuf &read_buf,
            int64_t &out_read_size, LogIOContext &io_ctx) override {
    int ret = OB_SUCCESS;
    char io_context_buf[1024] = {0};
    (void)io_ctx.to_string(io_context_buf, sizeof(io_context_buf));
    io_contexts_.push_back(io_context_buf);
    read_sizes_.push_back(in_read_size);
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

  LSN iterator_end_lsn() const { return LSN(MAX_LOG_BUFFER_SIZE); }

  bool observed_replay_io() const {
    bool observed = false;
    for (const std::string &context : io_contexts_) {
      if (std::string::npos != context.find("user:\"REPLAY\"")) {
        observed = true;
        break;
      }
    }
    return observed;
  }

  int64_t first_read_size() const {
    return read_sizes_.empty() ? 0 : read_sizes_.front();
  }

private:
  std::vector<char> bytes_;
  std::vector<std::string> io_contexts_;
  std::vector<int64_t> read_sizes_;
};

struct BufferSnapshot {
  BufferSnapshot() : address_(nullptr), capacity_(0) {}
  BufferSnapshot(char *address, const int64_t capacity)
      : address_(address), capacity_(capacity) {}

  char *address_;
  int64_t capacity_;
};

class ReplayIteratorFixture {
public:
  ReplayIteratorFixture() : storage_(), destroy_count_(0) {}

  int set_payload(const char *payload) {
    return storage_.set_payload(payload, SCN::base_scn());
  }

  int open(const LSN &lsn, PalfBufferIterator &iterator) {
    int ret = OB_SUCCESS;
    if (iterator.is_inited()) {
      ret = iterator.reuse(lsn);
    } else {
      GetFileEndLSN get_end_lsn(
          [this]() { return storage_.iterator_end_lsn(); });
      if (OB_FAIL(iterator.init(lsn, get_end_lsn, &storage_))) {
      } else {
        DestroyStorageFunctor destroy_functor([this]() { ++destroy_count_; });
        if (OB_FAIL(iterator.set_destroy_iterator_storage_functor(
                destroy_functor))) {
          iterator.destroy();
        }
      }
    }
    return ret;
  }

  void prepare(ObReplayServiceSubmitTask &task, const char *payload) {
    ASSERT_EQ(OB_SUCCESS, set_payload(payload));
    ASSERT_EQ(OB_SUCCESS, open(LSN(0), task.iterator_));
    ASSERT_EQ(OB_SUCCESS,
              task.iterator_.set_io_context(LogIOContext(LogIOUser::REPLAY)));
    ASSERT_EQ(OB_SUCCESS, task.iterator_.next());
  }

  BufferSnapshot buffer(const ObReplayServiceSubmitTask &task) const {
    const ReadBuf &read_buf = task.iterator_.iterator_storage_.read_buf_;
    return BufferSnapshot(read_buf.buf_, read_buf.buf_len_);
  }

  bool observed_replay_io() const { return storage_.observed_replay_io(); }
  int64_t first_read_size() const { return storage_.first_read_size(); }
  int64_t destroy_count() const { return destroy_count_; }

private:
  RecordingLogStorage storage_;
  int64_t destroy_count_;
};

class BlockingRunWrapper final : public lib::IRunWrapper {
public:
  BlockingRunWrapper() : entered_(false), released_(false) {}

  int pre_run() override {
    std::unique_lock<std::mutex> guard(lock_);
    entered_ = true;
    condition_.notify_all();
    condition_.wait(guard, [this]() { return released_; });
    return OB_SUCCESS;
  }

  uint64_t id() const override { return 1; }

  bool wait_until_entered() {
    std::unique_lock<std::mutex> guard(lock_);
    return condition_.wait_for(guard, std::chrono::seconds(10),
                               [this]() { return entered_; });
  }

  void release() {
    std::lock_guard<std::mutex> guard(lock_);
    released_ = true;
    condition_.notify_all();
  }

private:
  std::mutex lock_;
  std::condition_variable condition_;
  bool entered_;
  bool released_;
};

void init_replay_queue(ObLogReplayService &service,
                       BlockingRunWrapper &wrapper) {
  ASSERT_EQ(OB_SUCCESS,
            service.common::ObLinkQueueThreadPool::init(
                1, common::REPLAY_TASK_QUEUE_SIZE + 1, "ReplayTest"));
  ASSERT_EQ(OB_SUCCESS, service.set_adaptive_thread(0, 1));
  service.set_run_wrapper(&wrapper);
  service.is_inited_ = true;
  service.is_running_ = true;
}

bool wait_until_idle(ObReplayStatus &status, ObLogReplayService &service,
                     const int64_t expected_ref) {
  bool idle = false;
  for (int64_t retry = 0; retry < 10000 && !idle; ++retry) {
    idle = status.submit_log_task_.is_idle() &&
           expected_ref == status.ref_cnt_ && 0 == service.get_queue_num();
    if (!idle) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  return idle;
}

ObReplayServiceSubmitTask::IteratorOpener
make_opener(ReplayIteratorFixture &fixture) {
  return ObReplayServiceSubmitTask::IteratorOpener(
      [&fixture](const LSN &lsn, PalfBufferIterator &iterator) {
        return fixture.open(lsn, iterator);
      });
}

TEST(ReplaySubmitIteratorMemory, ReleasesEachDiskReadBufferExactlyOnce) {
  for (int64_t lifecycle = 0; lifecycle < 2; ++lifecycle) {
    ObReplayServiceSubmitTask task;
    ReplayIteratorFixture fixture;
    fixture.prepare(task,
                    0 == lifecycle ? "first-lifecycle" : "second-lifecycle");

    const BufferSnapshot allocated = fixture.buffer(task);
    ASSERT_NE(nullptr, allocated.address_);
    EXPECT_GT(allocated.capacity_, MAX_LOG_BUFFER_SIZE);
    EXPECT_EQ(MAX_LOG_BUFFER_SIZE, fixture.first_read_size());
    EXPECT_TRUE(fixture.observed_replay_io());
    EXPECT_EQ(0, fixture.destroy_count());

    task.release_iterator();
    const BufferSnapshot released = fixture.buffer(task);
    EXPECT_EQ(nullptr, released.address_);
    EXPECT_EQ(0, released.capacity_);
    EXPECT_FALSE(task.iterator_.is_inited());
    EXPECT_EQ(1, fixture.destroy_count());

    task.release_iterator();
    EXPECT_EQ(1, fixture.destroy_count());
  }
}

TEST(ReplaySubmitIteratorRelease, PreservesBlockersAndBecomesTerminal) {
  ObReplayStatus status;
  status.is_inited_ = true;
  ObReplayServiceSubmitTask &task = status.submit_log_task_;
  ReplayIteratorFixture fixture;
  fixture.prepare(task, "release-state-matrix");
  const BufferSnapshot allocated = fixture.buffer(task);

  SubmitIteratorReleaseState state = SubmitIteratorReleaseState::RELEASED;
  ASSERT_EQ(OB_STATE_NOT_MATCH, status.try_release_submit_iterator(state));
  EXPECT_EQ(SubmitIteratorReleaseState::LOCAL_REPLAY_ENABLED, state);
  EXPECT_EQ(allocated.address_, fixture.buffer(task).address_);
  EXPECT_EQ(0, fixture.destroy_count());

  status.disable_local_replay();
  ASSERT_TRUE(status.try_rdlock());
  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state));
  EXPECT_EQ(SubmitIteratorReleaseState::RWLOCK_BUSY, state);
  status.unlock();
  EXPECT_EQ(allocated.address_, fixture.buffer(task).address_);
  EXPECT_EQ(0, fixture.destroy_count());

  ASSERT_TRUE(task.acquire_lease());
  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state));
  EXPECT_EQ(SubmitIteratorReleaseState::TASK_BUSY, state);
  EXPECT_EQ(allocated.address_, fixture.buffer(task).address_);
  EXPECT_EQ(0, fixture.destroy_count());
  ASSERT_TRUE(task.revoke_lease());

  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state));
  EXPECT_EQ(SubmitIteratorReleaseState::RELEASED, state);
  EXPECT_TRUE(status.submit_iterator_released_);
  EXPECT_EQ(nullptr, fixture.buffer(task).address_);
  EXPECT_EQ(1, fixture.destroy_count());

  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(state));
  EXPECT_EQ(SubmitIteratorReleaseState::RELEASED, state);
  EXPECT_EQ(1, fixture.destroy_count());

  EXPECT_EQ(OB_STATE_NOT_MATCH, status.enable(LSN(0), SCN::base_scn()));
  EXPECT_FALSE(task.iterator_.is_inited());
  EXPECT_EQ(1, fixture.destroy_count());
}

TEST(ReplaySubmitIteratorRelease, ReportsNotInitializedWithoutMutatingState) {
  ObReplayStatus status;
  SubmitIteratorReleaseState state = SubmitIteratorReleaseState::RELEASED;
  EXPECT_EQ(OB_NOT_INIT, status.try_release_submit_iterator(state));
  EXPECT_EQ(SubmitIteratorReleaseState::RWLOCK_BUSY, state);
  EXPECT_FALSE(status.submit_iterator_released_);
}

TEST(ReplaySubmitIteratorEnable, RejectsBeforeReplayServiceStarts) {
  ObLogReplayService service;
  ObReplayStatus status;
  ReplayIteratorFixture fixture;
  ObReplayServiceSubmitTask &task = status.submit_log_task_;
  fixture.prepare(task, "pre-start");
  const BufferSnapshot allocated = fixture.buffer(task);

  status.is_inited_ = true;
  status.ref_cnt_ = 1;
  service.is_inited_ = true;
  service.is_running_ = false;
  service.replay_status_ = &status;

  EXPECT_EQ(OB_NOT_RUNNING, service.enable(LSN(0), SCN::base_scn()));
  EXPECT_TRUE(task.is_idle());
  EXPECT_EQ(1, status.ref_cnt_);
  EXPECT_EQ(0, service.get_queue_num());
  EXPECT_EQ(allocated.address_, fixture.buffer(task).address_);
  EXPECT_TRUE(status.local_replay_enabled_);
  EXPECT_TRUE(status.is_submit_blocked_);
  EXPECT_FALSE(status.is_enabled_);

  service.replay_status_ = nullptr;
  service.is_inited_ = false;
  status.is_inited_ = false;
  task.release_iterator();
  EXPECT_EQ(1, fixture.destroy_count());
}

TEST(ReplaySubmitIteratorPrepare, RollsBackOpenerAndContextFailures) {
  ObReplayServiceSubmitTask task;
  ReplayIteratorFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.set_payload("prepare-rollback"));

  ObReplayServiceSubmitTask::IteratorOpener failing_after_allocation(
      [&fixture](const LSN &lsn, PalfBufferIterator &iterator) {
        int ret = fixture.open(lsn, iterator);
        return OB_SUCCESS == ret ? OB_IO_ERROR : ret;
      });
  EXPECT_EQ(OB_IO_ERROR,
            task.prepare_iterator_(LSN(0), failing_after_allocation));
  EXPECT_FALSE(task.iterator_.is_inited());
  EXPECT_EQ(nullptr, fixture.buffer(task).address_);
  EXPECT_EQ(1, fixture.destroy_count());

  ObReplayServiceSubmitTask::IteratorOpener uninitialized_success(
      [](const LSN &, PalfBufferIterator &) { return OB_SUCCESS; });
  EXPECT_EQ(OB_NOT_INIT, task.prepare_iterator_(LSN(0), uninitialized_success));
  EXPECT_FALSE(task.iterator_.is_inited());
  EXPECT_EQ(nullptr, fixture.buffer(task).address_);
  EXPECT_EQ(1, fixture.destroy_count());

  const ObReplayServiceSubmitTask::IteratorOpener valid = make_opener(fixture);
  EXPECT_EQ(OB_SUCCESS, task.prepare_iterator_(LSN(0), valid));
  EXPECT_TRUE(task.iterator_.is_inited());
  EXPECT_NE(nullptr, fixture.buffer(task).address_);
  EXPECT_TRUE(fixture.observed_replay_io());
  task.release_iterator();
  EXPECT_EQ(2, fixture.destroy_count());
}

TEST(ReplaySubmitIteratorEnable, RollsBackFailedQueueSubmissionAndRetries) {
  BlockingRunWrapper wrapper;
  ObLogReplayService service;
  ObReplayStatus status;
  ReplayIteratorFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.set_payload("queue-submit-rollback"));
  const ObReplayServiceSubmitTask::IteratorOpener opener = make_opener(fixture);

  status.is_inited_ = true;
  status.ref_cnt_ = 1;
  status.rp_sv_ = &service;
  EXPECT_EQ(OB_NOT_INIT, status.enable_(LSN(0), SCN::base_scn(), opener));
  EXPECT_FALSE(status.is_enabled_);
  EXPECT_TRUE(status.is_submit_blocked_);
  EXPECT_TRUE(status.submit_log_task_.is_idle());
  EXPECT_EQ(1, status.ref_cnt_);
  EXPECT_EQ(0, service.get_queue_num());
  EXPECT_FALSE(status.submit_log_task_.iterator_.is_inited());
  EXPECT_EQ(nullptr, fixture.buffer(status.submit_log_task_).address_);
  EXPECT_EQ(1, fixture.destroy_count());

  init_replay_queue(service, wrapper);
  ASSERT_EQ(OB_SUCCESS, status.enable_(LSN(0), SCN::base_scn(), opener));
  ASSERT_TRUE(wrapper.wait_until_entered());
  EXPECT_FALSE(status.submit_log_task_.is_idle());
  EXPECT_EQ(ObThreadLease::HANDLING, status.submit_log_task_.lease_.value());
  EXPECT_EQ(2, status.ref_cnt_);
  EXPECT_EQ(1, service.get_queue_num());
  EXPECT_TRUE(status.submit_log_task_.iterator_.is_inited());
  EXPECT_NE(nullptr, fixture.buffer(status.submit_log_task_).address_);

  ASSERT_EQ(OB_SUCCESS, status.disable());
  wrapper.release();
  ASSERT_TRUE(wait_until_idle(status, service, 1));
  service.stop();
  service.wait();
  service.is_inited_ = false;
  status.submit_log_task_.release_iterator();
  EXPECT_EQ(2, fixture.destroy_count());
  status.is_inited_ = false;
}

TEST(ReplaySubmitIteratorEnable, KeepsSingleQueueOwnerAcrossDisableEnable) {
  BlockingRunWrapper wrapper;
  ObLogReplayService service;
  ObReplayStatus status;
  ReplayIteratorFixture fixture;
  ASSERT_EQ(OB_SUCCESS, fixture.set_payload("already-owned"));
  const ObReplayServiceSubmitTask::IteratorOpener opener = make_opener(fixture);

  init_replay_queue(service, wrapper);
  status.is_inited_ = true;
  status.ref_cnt_ = 1;
  status.rp_sv_ = &service;

  ASSERT_EQ(OB_SUCCESS, status.enable_(LSN(0), SCN::base_scn(), opener));
  ASSERT_TRUE(wrapper.wait_until_entered());
  EXPECT_EQ(ObThreadLease::HANDLING, status.submit_log_task_.lease_.value());
  EXPECT_EQ(2, status.ref_cnt_);
  EXPECT_EQ(1, service.get_queue_num());

  ASSERT_EQ(OB_SUCCESS, status.disable());
  ASSERT_EQ(OB_SUCCESS, status.enable_(LSN(0), SCN::base_scn(), opener));
  EXPECT_EQ(ObThreadLease::READY, status.submit_log_task_.lease_.value());
  EXPECT_EQ(2, status.ref_cnt_);
  EXPECT_EQ(1, service.get_queue_num());
  EXPECT_EQ(0, fixture.destroy_count());

  ASSERT_EQ(OB_SUCCESS, status.disable());
  wrapper.release();
  ASSERT_TRUE(wait_until_idle(status, service, 1));
  EXPECT_EQ(ObThreadLease::IDLE, status.submit_log_task_.lease_.value());
  EXPECT_EQ(1, status.ref_cnt_);
  EXPECT_EQ(0, service.get_queue_num());
  service.stop();
  service.wait();
  service.is_inited_ = false;

  status.disable_local_replay();
  SubmitIteratorReleaseState release_state =
      SubmitIteratorReleaseState::RWLOCK_BUSY;
  ASSERT_EQ(OB_SUCCESS, status.try_release_submit_iterator(release_state));
  EXPECT_EQ(SubmitIteratorReleaseState::RELEASED, release_state);
  EXPECT_EQ(1, fixture.destroy_count());
  status.is_inited_ = false;
}

} // namespace logservice
} // namespace oceanbase
