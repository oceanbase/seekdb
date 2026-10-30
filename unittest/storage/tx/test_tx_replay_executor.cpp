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
#include "lib/allocator/page_arena.h"
#include "storage/memtable/ob_memtable_mutator.h"
#include "storage/tx/ob_tx_replay_executor.h"
#include "storage/tx/ob_tx_ctx.h"
#include "storage/ls/ob_ls.h"
#include "storage/memtable/ob_memtable_context.h"

namespace oceanbase
{
namespace transaction
{
using namespace common;
using namespace memtable;
using namespace share;

class TestTxReplayExecutor : public ::testing::Test
{
protected:
  // Only the final tablet write is replaced. Parsing and replay control flow
  // execute the production implementation from liboceanbase.
  class RecordingExecutor final : public ObTxReplayExecutor
  {
  public:
    RecordingExecutor(storage::ObLS *ls, const logservice::ObLogBaseHeader &header)
      : ObTxReplayExecutor(ls, nullptr, palf::LSN(4096), SCN::plus(SCN::base_scn(), 100), header),
        row_count_(0), row_ret_(OB_SUCCESS), empty_payload_(false)
    {}
    ~RecordingExecutor() = default;

    int row_count_;
    int row_ret_;
    bool empty_payload_;

  private:
    int replay_one_row_in_memtable_(ObMutatorRowHeader &header,
                                    ObMemtableMutatorIterator *iterator) override
    {
      const ObMemtableMutatorRow &row = iterator->get_mutator_row();
      const int64_t index = row_count_++ % 2;
      EXPECT_EQ(MutatorType::MUTATOR_ROW, header.mutator_type_);
      EXPECT_EQ(ObTabletID(ObTabletID::MIN_USER_TABLET_ID + 1), header.tablet_id_);
      EXPECT_EQ(1, row.rowkey_.get_value_count());
      if (row.rowkey_.get_value_count() == 1) {
        EXPECT_EQ(index == 0 ? 42 : 84, row.rowkey_.get_values()[0].get_int());
      }
      const char *payload = empty_payload_ ? "" : (index == 0 ? "first" : "second");
      EXPECT_EQ(blocksstable::ObDmlFlag::DF_UPDATE, row.dml_flag_);
      EXPECT_EQ(1, row.table_version_);
      EXPECT_EQ(0, row.old_row_.size_);
      EXPECT_EQ(STRLEN(payload), row.new_row_.size_);
      if (row.new_row_.size_ == STRLEN(payload) && row.new_row_.size_ > 0) {
        EXPECT_EQ(0, MEMCMP(payload, row.new_row_.data_, row.new_row_.size_));
      }
      EXPECT_EQ(index == 0 ? 9 : 3, iterator->get_row_seq_no().get_seq());
      return row_ret_;
    }
  };

  class RecordingMemtableCtx final : public ObMemtableCtx
  {
  public:
    RecordingMemtableCtx() : end_count_(0), succeeded_(false), queue_(-1) {}
    int replay_end(const bool succeeded, const int16_t queue, const SCN scn) override
    {
      ++end_count_;
      succeeded_ = succeeded;
      queue_ = queue;
      scn_ = scn;
      return OB_SUCCESS;
    }
    int end_count_;
    bool succeeded_;
    int16_t queue_;
    SCN scn_;
  };

  TestTxReplayExecutor() : header_(), executor_(&ls_, header_), mutator_size_(0) {}

  void SetUp() override
  {
    // Initialize all context state through its public initialization path.
    // This deliberately unattached context returns OB_NOT_INIT if upper-level
    // replay reaches it, providing a safe probe without booting a database.
    ASSERT_EQ(OB_INVALID_ARGUMENT,
        ctx_.init(0, ObTransID(), 1, nullptr, nullptr, true, TxCtxSource::REPLAY));
    executor_.ctx_ = &ctx_;
  }

  int build_redo(ObTxRedoLog &replayed, const int64_t trailing = 0,
                 const bool truncate = false, const bool empty_payload = false)
  {
    int ret = OB_SUCCESS;
    filled_.reset();
    ObTxRedoLog redo;
    ObMutatorWriter writer;
    ObArenaAllocator key_allocator;
    ObITransCallback callback;
    if (OB_FAIL(filled_.init_for_fill())
        || OB_FAIL(filled_.prepare_mutator_buf(redo))
        || OB_FAIL(writer.set_buffer(redo.get_mutator_buf(), redo.get_mutator_size()))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < 2; ++i) {
        DatumValue cell;
        ObStoreRowkey key;
        ObMemtableKey memtable_key;
        ObRowData new_row;
        ObRowData old_row;
        const char *payload = empty_payload ? "" : (i == 0 ? "first" : "second");
        new_row.set(payload, STRLEN(payload));
        RedoDataNode node;
        if (OB_FAIL(cell.set_int(i == 0 ? 42 : 84, key_allocator))
            || OB_FAIL(key.assign(&cell, 1))
            || OB_FAIL(memtable_key.encode(&key))) {
        } else {
          node.set(&memtable_key, old_row, new_row, blocksstable::ObDmlFlag::DF_UPDATE,
              1, 0, 1, 0, ObTxSEQ(i == 0 ? 9 : 3, 0),
              ObTabletID(ObTabletID::MIN_USER_TABLET_ID + 1), 1);
          node.set_callback(&callback);
          ret = writer.append_row_kv(1, node);
        }
      }
      if (OB_SUCC(ret)) {
        ret = writer.serialize(ObTransRowFlag::NORMAL_ROW, mutator_size_);
      }
    }
    if (OB_SUCC(ret)) {
      // Keep the inner frame intact and all bytes within the allocated buffer.
      // Only the outer Redo length includes this extra content.
      if (trailing > 0) {
        MEMSET(redo.get_mutator_buf() + mutator_size_, 0, trailing);
      }
      ret = filled_.finish_mutator_buf(redo, mutator_size_ + trailing - (truncate ? 1 : 0));
    }
    if (OB_SUCC(ret)) {
      ret = filled_.seal(1);
    }
    int64_t skip = 0;
    logservice::ObLogBaseHeader base;
    ObTxLogBlock block;
    ObTxLogHeader log_header;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(base.deserialize(filled_.get_buf(), filled_.get_size(), skip))
          || OB_FAIL(block.init_for_replay(filled_.get_buf(), filled_.get_size(), skip))
          || OB_FAIL(block.get_next_log(log_header))
          || OB_FAIL(block.deserialize_log_body(replayed))) {
      }
    }
    return ret;
  }

  int replay(ObTxRedoLog &redo, const bool serial_final = false)
  { return executor_.replay_redo_in_memtable_(redo, serial_final, max_seq_); }

  void rewrite_retry_code(int &ret) { executor_.rewrite_replay_retry_code_(ret); }

  int replay_upper()
  {
    executor_.log_block_.reset();
    int ret = OB_SUCCESS;
    int64_t skip = 0;
    logservice::ObLogBaseHeader base;
    ObTxLogHeader log_header;
    if (OB_FAIL(base.deserialize(filled_.get_buf(), filled_.get_size(), skip))
        || OB_FAIL(executor_.log_block_.init_for_replay(filled_.get_buf(), filled_.get_size(), skip))
        || OB_FAIL(executor_.log_block_.get_next_log(log_header))) {
    } else {
      ret = executor_.replay_redo_();
    }
    return ret;
  }

  void expect_finish(const int ret, const bool succeeded, const int16_t queue)
  {
    RecordingMemtableCtx memtable;
    ASSERT_EQ(OB_SUCCESS, memtable.replay_begin(queue != 0, executor_.log_ts_ns_));
    executor_.ctx_ = nullptr; // No context was acquired from an LS service.
    executor_.mt_ctx_ = &memtable;
    executor_.has_redo_ = true;
    executor_.replay_queue_ = queue;
    executor_.finish_replay_(ret);
    EXPECT_EQ(1, memtable.end_count_);
    EXPECT_EQ(succeeded, memtable.succeeded_);
    EXPECT_EQ(queue, memtable.queue_);
    EXPECT_EQ(executor_.log_ts_ns_, memtable.scn_);
    executor_.mt_ctx_ = nullptr;
    executor_.has_redo_ = false;
    executor_.ctx_ = &ctx_;
  }

  storage::ObLS ls_;
  logservice::ObLogBaseHeader header_;
  ObTxCtx ctx_;
  RecordingExecutor executor_;
  ObTxLogBlock filled_;
  ObTxSEQ max_seq_;
  int64_t mutator_size_;
};

TEST_F(TestTxReplayExecutor, normal_writer_redo)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  EXPECT_EQ(mutator_size_, redo.get_mutator_size());
  EXPECT_EQ(OB_SUCCESS, replay(redo));
  EXPECT_EQ(2, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, serial_final_selects_max_sequence)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  EXPECT_EQ(OB_SUCCESS, replay(redo, true));
  EXPECT_EQ(2, executor_.row_count_);
  EXPECT_EQ(9, max_seq_.get_seq());
}

TEST_F(TestTxReplayExecutor, size_mismatch_is_invalid_log)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 8));
  ObMemtableMutatorIterator iterator;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, iterator.deserialize(redo.get_replay_mutator_buf(), redo.get_mutator_size(), pos));
  ASSERT_EQ(mutator_size_, pos);
  ASSERT_LT(pos, redo.get_mutator_size());
  EXPECT_EQ(OB_INVALID_LOG, replay(redo));
  EXPECT_EQ(0, executor_.row_count_);
  EXPECT_EQ(OB_INVALID_LOG, replay(redo, true));
  EXPECT_EQ(0, executor_.row_count_);
  EXPECT_FALSE(max_seq_.is_valid());
}

TEST_F(TestTxReplayExecutor, preserves_deserialization_error)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 0, true));
  ObMemtableMutatorIterator iterator;
  int64_t pos = 0;
  ASSERT_EQ(OB_BUF_NOT_ENOUGH,
      iterator.deserialize(redo.get_replay_mutator_buf(), redo.get_mutator_size(), pos));
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, replay(redo));
  EXPECT_EQ(0, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, empty_row_payload_is_supported)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 0, false, true));
  executor_.empty_payload_ = true;
  EXPECT_EQ(OB_SUCCESS, replay(redo));
  EXPECT_EQ(2, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, preserves_row_replay_error)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  executor_.row_ret_ = OB_EAGAIN;
  EXPECT_EQ(OB_EAGAIN, replay(redo));
  EXPECT_EQ(1, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, iterator_can_be_reused_after_failure)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  ASSERT_EQ(OB_SUCCESS, replay(redo));
  ASSERT_EQ(2, executor_.row_count_);
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 8));
  EXPECT_EQ(OB_INVALID_LOG, replay(redo));
  EXPECT_EQ(2, executor_.row_count_);
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  EXPECT_EQ(OB_SUCCESS, replay(redo));
  EXPECT_EQ(4, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, upper_replay_stops_before_context)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 8));
  EXPECT_EQ(OB_INVALID_LOG, replay_upper());
  EXPECT_EQ(0, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, upper_normal_replay_reaches_context)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  // The real context method rejects the deliberately unattached context.
  // This control proves that upper_replay_stops_before_context is meaningful.
  EXPECT_EQ(OB_NOT_INIT, replay_upper());
  EXPECT_EQ(2, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, failure_finishes_both_replay_queues)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 8));
  const int ret = replay(redo);
  expect_finish(ret, false, 0);
  expect_finish(ret, false, 1);
}

TEST_F(TestTxReplayExecutor, normal_replay_finishes_successfully)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo));
  const int ret = replay(redo);
  ASSERT_EQ(OB_SUCCESS, ret);
  expect_finish(ret, true, 0);
  expect_finish(ret, true, 1);
}

TEST_F(TestTxReplayExecutor, upper_preserves_deserialization_error)
{
  ObTxRedoLog redo;
  ASSERT_EQ(OB_SUCCESS, build_redo(redo, 0, true));
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, replay_upper());
  EXPECT_EQ(0, executor_.row_count_);
}

TEST_F(TestTxReplayExecutor, mismatch_is_not_rewritten_as_retry)
{
  int ret = OB_INVALID_LOG;
  rewrite_retry_code(ret);
  EXPECT_EQ(OB_INVALID_LOG, ret);
  ret = OB_ALLOCATE_MEMORY_FAILED;
  rewrite_retry_code(ret);
  EXPECT_EQ(OB_EAGAIN, ret);
}

} // namespace transaction
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
