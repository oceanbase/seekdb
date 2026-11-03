// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Actual DTL datum encoding and receive reader; buffers move in memory, not RPC.
#ifndef SEEKDB_TEST_RUST_DTL_WIRE_FIXTURE_H_
#define SEEKDB_TEST_RUST_DTL_WIRE_FIXTURE_H_
#include <array>
#include "sql/dtl/ob_dtl_basic_channel.h"
#include "sql/dtl/ob_dtl_channel_mem_manager.h"
#include "sql/engine/px/exchange/ob_px_receive_op.h"
#include "share/rc/ob_server_runtime.h"
#include "rust_channel_receive_fixture.h"
#include "rust_linked_exchange_fixture.h"
#include "rust_eof_sender_fixture.h"
namespace rust_dtl_wire_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
class MemoryScope {
public:
  static bool &pressure_checked() { static bool checked = false; return checked; }
  static int64_t &rescans_checked() { static int64_t count = 0; return count; }
  MemoryScope() : saved_(oceanbase::share::server_service<ObDfc>()),
      saved_results_(oceanbase::share::server_service<ObDTLIntermResultManager>()) {
    pressure_checked() = false;
    rescans_checked() = 0;
    CHECK(dfc_.get_mem_manager()->init() == OB_SUCCESS);
    dfc_.calc_max_buffer(10);
    oceanbase::share::bind_server_service<ObDfc>(&dfc_);
    CHECK(DTL.init() == OB_SUCCESS);
    // The standalone bootstrap reports zero CPUs. Supply a temporary positive
    // capacity solely while initializing the real intermediate-result maps.
    auto *runtime = oceanbase::share::server_runtime(); const double saved_cpu = runtime->max_cpu();
    runtime->set_max_cpu(std::max(1.0, saved_cpu));
    const int result_init = results_.init(); runtime->set_max_cpu(saved_cpu);
    CHECK(result_init == OB_SUCCESS);
    oceanbase::share::bind_server_service<ObDTLIntermResultManager>(&results_);
    verify_eof_sender();
  }
  ~MemoryScope() {
    CHECK(pressure_checked());
    CHECK(rescans_checked() > 0);
    class CountResults final : public ObIDTLIntermResultConsumer {
    public:
      int consume(const ObDTLIntermResultKey &, const ObDTLIntermResultInfo &) override { ++count; return OB_SUCCESS; }
      int64_t count = 0;
    } results;
    CHECK(results_.generate_monitor_info_rows(results) == OB_SUCCESS && results.count == 0);
    CHECK(dfc_.get_current_buffer_used() == 0 && dfc_.get_current_buffer_cnt() == 0);
    CHECK(dfc_.get_current_blocked_cnt() == 0);
    CHECK(dfc_.get_channel_cnt() == 0);
    int64_t registered = 0;
    CHECK(DTL.foreach_refactored([&](ObDtlChannel *) { ++registered; return OB_SUCCESS; }) == OB_SUCCESS);
    CHECK(registered == 0);
    auto &manager = *dfc_.get_mem_manager();
    for (int64_t i = 0; i < manager.get_channel_mgr_count(); ++i) {
      ObDtlChannelMemManager *channel = nullptr;
      CHECK(manager.get_channel_mem_manager(i, channel) == OB_SUCCESS && channel);
      CHECK(channel->get_alloc_cnt() == channel->get_free_cnt());
    }
    oceanbase::share::bind_server_service<ObDfc>(saved_);
    oceanbase::share::bind_server_service<ObDTLIntermResultManager>(saved_results_);
  }
private:
  ObDfc dfc_; ObDfc *saved_;
  ObDTLIntermResultManager results_; ObDTLIntermResultManager *saved_results_;
};
// Only the transport handoff is controlled: there are no live channels, hence
// all_eof(0) is true. The production operator drains already received buffers.
// This does not exercise SQC linking, message-loop EOF delivery or networking.
class BufferedReceive final : public ObPxFifoReceiveOp {
public:
  BufferedReceive(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input)
      : ObPxFifoReceiveOp(ctx, spec, input) {}
  ~BufferedReceive() override { ObPxFifoReceiveOp::destroy(); }
  void feed(ObDtlLinkedBuffer *&buffer) {
    CHECK(buffer); bool transferred = false;
    CHECK(row_reader_.add_buffer(*buffer, transferred) == OB_SUCCESS && transferred);
    buffer = nullptr;
  }
  bool empty() const { return !row_reader_.has_more() && row_reader_.left_rows() == 0; }
protected:
  int try_link_channel() override { return OB_SUCCESS; }
};
class WireRows {
  struct Value { int64_t tablet, ddl; bool null; std::string bytes; };
public:
  explicit WireRows(int64_t channel, int rows_per_block = 0)
      : channel_(channel), rows_per_block_(rows_per_block) {}
  ~WireRows() { CHECK(buffer_ == nullptr); }
  void append(const ObDtlMsg &msg, ObEvalCtx &eval) {
    const auto &row = static_cast<const ObPxNewRow &>(msg);
    const auto *exprs = row.get_exprs(); CHECK(exprs && exprs->count() == 3);
    ObDatum *tablet = nullptr, *ddl = nullptr, *value = nullptr;
    CHECK(exprs->at(0)->eval(eval, tablet) == OB_SUCCESS && tablet);
    CHECK(exprs->at(1)->eval(eval, ddl) == OB_SUCCESS && ddl);
    CHECK(exprs->at(2)->eval(eval, value) == OB_SUCCESS && value);
    Value expected{tablet->get_int(), ddl->get_int(), value->is_null(), {}};
    if (!expected.null) expected.bytes.assign(value->ptr_, value->len_);
    if (!MemoryScope::pressure_checked() && !expected.null) {
      LinkedExchange pressure(eval.exec_ctx_, *exprs, true);
      pressure.exercise_pressure(msg, eval);
      const auto &received = pressure.finish(); CHECK(received.size() >= 4);
      for (const auto &row : received) {
        CHECK(row.tablet == expected.tablet && row.ddl == expected.ddl);
        CHECK(!row.null && row.bytes == expected.bytes);
      }
      MemoryScope::pressure_checked() = true;
    }
    if (!replay_) {
      replay_ = std::make_unique<LinkedExchange>(eval.exec_ctx_, *exprs, rows_per_block_ == 1);
    } else {
      replay_->restart(); ++MemoryScope::rescans_checked();
    }
    replay_->append(msg, eval);
    const auto &replayed = replay_->finish(); CHECK(replayed.size() == 1);
    CHECK(replayed[0].tablet == expected.tablet && replayed[0].ddl == expected.ddl);
    CHECK(replayed[0].null == expected.null && replayed[0].bytes == expected.bytes);
    values_.push_back(std::move(expected));
    if (!linked_) linked_ = std::make_unique<LinkedExchange>(eval.exec_ctx_, *exprs, rows_per_block_ == 1);
    linked_->append(msg, eval);
    if (buffer_ && rows_per_block_ > 0 && writer_.rows() == rows_per_block_) seal();
    if (!buffer_) {
      buffer_ = manager().alloc(channel_, 65536); CHECK(buffer_);
      CHECK(writer_.init(buffer_) == OB_SUCCESS);
      writer_.write_msg_type(buffer_); buffer_->set_data_msg(true);
    }
    CHECK(writer_.write(msg, &eval, false) == OB_SUCCESS);
  }
  void verify(ObExecContext &sender, const ObIArray<ObExpr *> &exprs) {
    if (values_.empty()) { CHECK(!buffer_ && buffers_.empty()); return; }
    seal(); CHECK(sealed_rows_ == int64_t(values_.size()));
    CHECK(replay_); replay_.reset();
    CHECK(linked_);
    const auto &received = linked_->finish(); CHECK(received.size() == values_.size());
    for (size_t i = 0; i < values_.size(); ++i) {
      CHECK(received[i].tablet == values_[i].tablet && received[i].ddl == values_[i].ddl);
      CHECK(received[i].null == values_[i].null && received[i].bytes == values_[i].bytes);
    }
    linked_.reset();
    CHECK(buffers_.size() == (rows_per_block_ == 1 ? values_.size() : size_t(1)));
    ObArenaAllocator arena("RustDtlRecv");
    ObExecContext receiver(arena); receiver.set_my_session(sender.get_my_session());
    receiver.set_sql_ctx(sender.get_sql_ctx());
    CHECK(receiver.create_physical_plan_ctx() == OB_SUCCESS);
    const auto *plan = sender.get_physical_plan_ctx()->get_phy_plan(); CHECK(plan);
    receiver.get_physical_plan_ctx()->set_phy_plan(plan);
    CHECK(receiver.init_expr_op(plan->get_expr_operator_size()) == OB_SUCCESS);
    CHECK(plan->get_expr_frame_info().pre_alloc_exec_memory(receiver) == OB_SUCCESS);
    ObEvalCtx eval(receiver); eval.max_batch_size_ = plan->get_batch_size();
    CHECK(eval.max_batch_size_ == 3);
    ObEvalCtx::BatchInfoScopeGuard frame(eval);
    ObSEArray<ObExpr *, 1> dynamic_constants;
    const auto check_row = [&](size_t i, ObEvalCtx &eval) {
      const auto &expected = values_.at(i);
      const auto &tablet = exprs.at(0)->locate_expr_datum(eval);
      const auto &ddl = exprs.at(1)->locate_expr_datum(eval);
      const auto &value = exprs.at(2)->locate_expr_datum(eval);
      CHECK(!tablet.is_null() && tablet.get_int() == expected.tablet);
      CHECK(!ddl.is_null() && ddl.get_int() == expected.ddl);
      CHECK(value.is_null() == expected.null);
      if (!expected.null) CHECK(std::string(value.ptr_, value.len_) == expected.bytes);
      for (int64_t col = 0; col < exprs.count(); ++col) {
        CHECK(exprs.at(col)->get_eval_info(eval).evaluated_ && exprs.at(col)->get_eval_info(eval).projected_);
      }
    };
    for (int mode = 0; mode < 5; ++mode) {
      ObReceiveRowReader reader; bool transferred = false;
      if (mode == 3) {
        // A trailing row has fewer columns, after earlier valid rows/blocks.
        // Preserve its byte span so this tests schema shape, not a damaged block.
        auto *block = reinterpret_cast<ObChunkDatumStore::Block *>(buffers_.back()[mode]->buf());
        int64_t offset = 0; const ObChunkDatumStore::StoredRow *last = nullptr;
        for (uint32_t i = 0; i < block->rows_; ++i) CHECK(block->get_store_row(offset, last) == OB_SUCCESS && last);
        CHECK(last && last->cnt_ == 3); const_cast<ObChunkDatumStore::StoredRow *>(last)->cnt_ = 2;
      }
      for (auto &buffers : buffers_) {
        CHECK(reader.add_buffer(*buffers[mode], transferred) == OB_SUCCESS && transferred);
        buffers[mode] = nullptr; // ownership transferred to the real reader
      }
      CHECK(reader.left_rows() == int64_t(values_.size()));
      size_t position = 0;
      if (mode >= 3) {
        frame.set_batch_size(3);
        const auto untouched = [&] {
          for (int i = 0; i < 3; ++i) {
            frame.set_batch_idx(i);
            CHECK(exprs.at(0)->locate_expr_datum(eval).get_int() == -777);
            CHECK(exprs.at(1)->locate_expr_datum(eval).get_int() == -777);
            CHECK(exprs.at(2)->locate_expr_datum(eval).is_null());
          }
          for (int64_t col = 0; col < exprs.count(); ++col) {
            CHECK(!exprs.at(col)->get_eval_info(eval).evaluated_ && !exprs.at(col)->get_eval_info(eval).projected_);
          }
        };
        for (int i = 0; i < 3; ++i) {
          frame.set_batch_idx(i);
          exprs.at(0)->locate_datum_for_write(eval).set_int(-777);
          exprs.at(1)->locate_datum_for_write(eval).set_int(-777);
          exprs.at(2)->locate_datum_for_write(eval).set_null();
        }
        for (int64_t col = 0; col < exprs.count(); ++col) {
          exprs.at(col)->get_eval_info(eval).evaluated_ = false;
          exprs.at(col)->get_eval_info(eval).projected_ = false;
        }
        const ObChunkDatumStore::StoredRow *stored[3] = {};
        int64_t read = 77;
        ObSEArray<ObExpr *, 3> targets;
        for (int64_t i = 0; i < (mode == 4 ? 2 : exprs.count()); ++i) CHECK(targets.push_back(exprs.at(i)) == OB_SUCCESS);
        // Mode 4 supplies fewer destination columns than the unmodified wire
        // row, proving the batch reader cannot silently truncate extra columns.
        CHECK(reader.get_next_batch(targets, dynamic_constants, eval, 3, read, stored) == OB_ERR_UNEXPECTED);
        CHECK(read == 0); untouched();
        stored[values_.size() - 1] = nullptr;
        CHECK(ObReceiveRowReader::attach_rows(exprs, dynamic_constants, eval, stored, values_.size()) == OB_INVALID_ARGUMENT);
        untouched();
        for (int size : {-1, 0, 4}) {
          CHECK(ObReceiveRowReader::attach_rows(exprs, dynamic_constants, eval, stored, size) == OB_INVALID_ARGUMENT);
          untouched();
        }
      } else if (mode == 0 || mode == 2) {
        frame.set_batch_size(1); frame.set_batch_idx(0);
        while (position < (mode == 2 ? size_t(1) : values_.size())) {
          CHECK(reader.get_next_row(exprs, dynamic_constants, eval) == OB_SUCCESS);
          check_row(position++, eval);
        }
        if (mode == 0) CHECK(reader.get_next_row(exprs, dynamic_constants, eval) == OB_ITER_END);
      } else {
        frame.set_batch_size(3); frame.set_batch_idx(0);
        const ObChunkDatumStore::StoredRow *stored[3] = {};
        for (int size : {-1, 0, 4}) {
          int64_t read = 77;
          CHECK(reader.get_next_batch(exprs, dynamic_constants, eval, size, read, stored) == OB_INVALID_ARGUMENT);
          CHECK(read == 0 && reader.left_rows() == int64_t(values_.size()));
          CHECK(!stored[0] && !stored[1] && !stored[2]);
        }
        int64_t read = 77;
        CHECK(reader.get_next_batch(exprs, dynamic_constants, eval, 3, read, nullptr) == OB_INVALID_ARGUMENT && read == 0);
        while (position < values_.size()) {
          CHECK(reader.get_next_batch(exprs, dynamic_constants, eval, 2, read, stored) == OB_SUCCESS);
          CHECK(read == int64_t(std::min(size_t(2), values_.size() - position)));
          for (int64_t i = 0; i < read; ++i) { frame.set_batch_idx(i); check_row(position++, eval); }
        }
        CHECK(reader.get_next_batch(exprs, dynamic_constants, eval, 3, read, stored) == OB_ITER_END && read == 0);
      }
      if (mode < 2) CHECK(!reader.has_more() && reader.left_rows() == 0);
      reader.reset(); reader.reset();
      CHECK(!reader.has_more() && reader.left_rows() == 0);
    }
    ObPxFifoReceiveSpec spec(arena, PHY_PX_FIFO_RECEIVE);
    spec.plan_ = const_cast<ObPhysicalPlan *>(plan);
    spec.max_batch_size_ = 3;
    CHECK(spec.output_.assign(exprs) == OB_SUCCESS);
    CHECK(spec.child_exprs_.assign(exprs) == OB_SUCCESS);
    CHECK(spec.calc_exprs_.assign(exprs) == OB_SUCCESS);
    ObPxFifoReceiveOpInput input(receiver, spec);
    for (int mode : {5, 7, 9}) {
      BufferedReceive op(receiver, spec, &input);
      CHECK(op.init() == OB_SUCCESS);
      CHECK(op.open() == OB_SUCCESS && op.empty());
      const auto feed = [&](int copy) {
        for (auto &buffers : buffers_) op.feed(buffers[copy]);
        CHECK(!op.empty());
      };
      const auto read_all = [&] {
        size_t position = 0;
        auto &op_eval = op.get_eval_ctx();
        if (mode == 5) {
          // Public row API exercises the production vector-to-row adapter.
          while (position < values_.size()) {
            CHECK(op.get_next_row() == OB_SUCCESS);
            check_row(position++, op_eval);
          }
          CHECK(op.get_next_row() == OB_ITER_END);
          CHECK(op.get_next_row() == OB_ITER_END);
        } else {
          const ObBatchRows *batch = nullptr;
          while (position < values_.size()) {
            CHECK(op.get_next_batch(2, batch) == OB_SUCCESS && batch);
            // The public operator wrapper clears the all-active optimization
            // hint for non-table-scan operators. The skip bitmap is decisive.
            CHECK(!batch->end_ && !batch->all_rows_active_);
            CHECK(batch->size_ == int64_t(std::min(size_t(2), values_.size() - position)));
            ObEvalCtx::BatchInfoScopeGuard batch_frame(op_eval);
            batch_frame.set_batch_size(batch->size_);
            for (int64_t i = 0; i < batch->size_; ++i) {
              CHECK(!batch->skip_->at(i)); batch_frame.set_batch_idx(i);
              check_row(position++, op_eval);
            }
          }
          for (int repeat = 0; repeat < 2; ++repeat) {
            CHECK(op.get_next_batch(2, batch) == OB_SUCCESS && batch);
            CHECK(batch->end_ && batch->size_ == 0);
          }
        }
        CHECK(op.empty());
      };
      feed(mode);
      if (mode == 9) {
        const ObBatchRows *batch = nullptr;
        CHECK(op.get_next_batch(1, batch) == OB_SUCCESS && batch && batch->size_ == 1);
        ObEvalCtx::BatchInfoScopeGuard first_frame(op.get_eval_ctx());
        first_frame.set_batch_size(1); first_frame.set_batch_idx(0);
        check_row(0, op.get_eval_ctx());
        // Close with unread rows/blocks when the input has more than one row.
      } else {
        read_all();
        CHECK(op.rescan() == OB_SUCCESS && op.empty());
        feed(mode + 1);
        {
          ObEvalCtx::BatchInfoScopeGuard partial_frame(op.get_eval_ctx());
          if (mode == 5) {
            CHECK(op.get_next_row() == OB_SUCCESS);
          } else {
            const ObBatchRows *batch = nullptr;
            CHECK(op.get_next_batch(1, batch) == OB_SUCCESS && batch && batch->size_ == 1);
          }
          partial_frame.set_batch_size(1); partial_frame.set_batch_idx(0);
          check_row(0, op.get_eval_ctx());
        }
        // In row mode unread rows may live in the vector-to-row adapter;
        // in batch mode they remain in the reader. Rescan must discard both.
        CHECK(op.rescan() == OB_SUCCESS && op.empty());
        feed(mode == 5 ? 10 : 11); read_all();
      }
      CHECK(op.close() == OB_SUCCESS && op.empty());
    }
    for (int mode = 12; mode < 16; ++mode) {
      ChannelReceive op(receiver, spec, &input, 200000 + 4 * channel_);
      CHECK(op.init() == OB_SUCCESS && op.open() == OB_SUCCESS);
      auto *empty_eof = make_eof(); op.feed(empty_eof, true);
      CHECK(op.reader_empty() && op.eof_count() == 0);
      CHECK(op.process_one() == OB_SUCCESS);
      CHECK(op.eof_count() == 1 && !op.all_eof() && op.reader_empty());
      CHECK(op.process_one() == OB_DTL_WAIT_EAGAIN); // other channel has not ended
      CHECK(op.eof_count() == 1 && !op.all_eof());
      for (auto &buffers : buffers_) {
        if (mode == 15) buffers[mode]->msg_type() = PX_NEW_ROW; // unsupported datum payload tag
        // Exercise both data-bearing EOF and a separate empty EOF message.
        if (mode == 12 && &buffers == &buffers_.back()) buffers[mode]->is_eof() = true;
        op.feed(buffers[mode]);
      }
      if (mode != 12) { auto *eof = make_eof(); op.feed(eof); }
      CHECK(op.processed_data() == 0 && op.reader_empty() && !op.all_eof());
      auto &op_eval = op.get_eval_ctx();
      if (mode == 15) {
        const ObBatchRows *batch = nullptr;
        CHECK(op.get_next_batch(2, batch) == OB_ERR_UNEXPECTED && batch);
        CHECK(batch->size_ == 0 && !batch->end_ && op.reader_empty());
        CHECK(op.processed_data() == 0 && op.eof_count() == 1);
      } else if (mode == 13) {
        for (size_t i = 0; i < values_.size(); ++i) {
          CHECK(op.get_next_row() == OB_SUCCESS); check_row(i, op_eval);
        }
        CHECK(op.get_next_row() == OB_ITER_END);
        CHECK(op.get_next_row() == OB_ITER_END); op.check_consumed();
      } else {
        size_t position = 0;
        const ObBatchRows *batch = nullptr;
        while (position < (mode == 14 ? size_t(1) : values_.size())) {
          const int request = mode == 14 ? 1 : 2;
          CHECK(op.get_next_batch(request, batch) == OB_SUCCESS && batch);
          CHECK(!batch->end_ && batch->size_ == int64_t(std::min(size_t(request), values_.size() - position)));
          ObEvalCtx::BatchInfoScopeGuard batch_frame(op_eval);
          batch_frame.set_batch_size(batch->size_);
          for (int64_t i = 0; i < batch->size_; ++i) {
            CHECK(!batch->skip_->at(i)); batch_frame.set_batch_idx(i); check_row(position++, op_eval);
          }
        }
        if (mode == 12) {
          for (int repeat = 0; repeat < 2; ++repeat) {
            CHECK(op.get_next_batch(2, batch) == OB_SUCCESS && batch && batch->end_ && batch->size_ == 0);
          }
          op.check_consumed();
        }
      }
      CHECK(op.close() == OB_SUCCESS && op.reader_empty());
      // Destructor releases pending EOF/data and failed process_buffer_ through
      // the actual receive DFC cleanup path, including early-close/error modes.
    }
    for (const auto &buffers : buffers_) for (auto *buffer : buffers) CHECK(!buffer);
  }
private:
  ObDtlLinkedBuffer *make_eof() {
    auto *buffer = manager().alloc(channel_, 65536); CHECK(buffer);
    ObDtlDatumMsgWriter writer; CHECK(writer.init(buffer) == OB_SUCCESS);
    writer.write_msg_type(buffer); buffer->set_data_msg(true);
    ObPxNewRow eof; eof.set_eof_row();
    CHECK(writer.write(eof, nullptr, true) == OB_SUCCESS);
    CHECK(writer.rows() == 0 && buffer->is_eof()); writer.reset();
    return buffer;
  }
  void seal() {
    CHECK(buffer_ && writer_.rows() > 0);
    // Datum write already unswizzles; match production switch_buffer by not
    // calling serialize a second time. Each relocated allocation is distinct.
    CHECK(writer_.handle_eof() == OB_SUCCESS);
    std::array<ObDtlLinkedBuffer *, 16> copies{};
    for (auto &copy : copies) {
      copy = manager().alloc(channel_, buffer_->pos()); CHECK(copy && copy->buf() != buffer_->buf());
      MEMCPY(copy->buf(), buffer_->buf(), buffer_->pos()); copy->pos() = buffer_->pos();
      copy->set_data_msg(true); copy->msg_type() = PX_DATUM_ROW;
    }
    buffers_.push_back(copies); sealed_rows_ += writer_.rows(); writer_.reset();
    MEMSET(buffer_->buf(), 0xa5, buffer_->pos());
    CHECK(manager().free(buffer_) == OB_SUCCESS); buffer_ = nullptr;
  }
  static ObDtlMemManager &manager() {
    auto *dfc = oceanbase::share::server_service<ObDfc>(); CHECK(dfc);
    return *dfc->get_mem_manager();
  }
  int64_t channel_; int rows_per_block_; int64_t sealed_rows_ = 0;
  ObDtlLinkedBuffer *buffer_ = nullptr;
  std::vector<std::array<ObDtlLinkedBuffer *, 16>> buffers_;
  ObDtlDatumMsgWriter writer_; std::vector<Value> values_;
  std::unique_ptr<LinkedExchange> linked_;
  std::unique_ptr<LinkedExchange> replay_;
};
}
#endif
