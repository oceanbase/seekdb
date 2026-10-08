// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_RUST_LINKED_EXCHANGE_FIXTURE_H_
#define SEEKDB_TEST_RUST_LINKED_EXCHANGE_FIXTURE_H_
#include "sql/dtl/ob_dtl_channel_group.h"
#include "sql/dtl/ob_dtl_local_channel.h"
#include "sql/dtl/ob_dtl_utils.h"
#include "sql/engine/px/exchange/ob_px_receive_op.h"
#include "sql/engine/px/exchange/ob_px_transmit_op.h"
namespace rust_dtl_wire_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
// Original generated sender expressions -> actual send/flush -> global peer
// lookup -> local attach -> FIFO operator. No direct buffer injection here.
class LinkedExchange {
  // Only rescan is delegated to this operator. Channels and message delivery
  // remain owned by LinkedExchange; this is not a generated transmit plan or
  // an SQC-driven open/transmit lifecycle.
  class TransmitRescan final : public ObPxTransmitOp {
  public:
    TransmitRescan(ObExecContext &ctx, const ObPxTransmitSpec &spec, ObDtlChannel *channel)
        : ObPxTransmitOp(ctx, spec, nullptr) {
      CHECK(task_channels_.push_back(channel) == OB_SUCCESS);
      CHECK(init() == OB_SUCCESS);
    }
    ~TransmitRescan() override { task_channels_.reset(); }
  protected:
    int do_transmit() override { return OB_NOT_SUPPORTED; }
  };
  class Receive final : public ObPxFifoReceiveOp {
  public:
    Receive(ObExecContext &ctx, const ObOpSpec &spec, ObOpInput *input, uint64_t id)
        : ObPxFifoReceiveOp(ctx, spec, input), id_(id) {
      dfc_.set_dtl_channel_watcher(&msg_loop_);
      CHECK(dfc_.init(1) == OB_SUCCESS); dfc_.set_receive();
      dfc_.set_timeout_ts(ctx.get_physical_plan_ctx()->get_timeout_timestamp());
      msg_loop_.register_processor(px_row_msg_proc_);
      CHECK(DTL.create_local_channel(id_, channel_, &dfc_) == OB_SUCCESS && channel_);
      CHECK(task_channels_.push_back(channel_) == OB_SUCCESS);
      CHECK(!msg_loop_.all_eof(1));
    }
    ~Receive() override {
      CHECK(close() == OB_SUCCESS);
      ObDtlChannel *removed = nullptr;
      CHECK(DTL.remove_channel(id_, removed) == OB_SUCCESS && removed == channel_);
      CHECK(removed->get_pins() == 0);
      CHECK(DTL.get_dfc_server().unregister_dfc_channel(dfc_, removed) == OB_SUCCESS);
      CHECK(dfc_.get_used() == 0 && dfc_.get_total_buffer_cnt() == 0 && !dfc_.is_block());
      CHECK(removed->get_alloc_buffer_cnt() == removed->get_free_buffer_cnt());
      CHECK(msg_loop_.unregister_all_channel() == OB_SUCCESS);
      ob_delete(removed); channel_ = nullptr; task_channels_.reset();
      CHECK(DTL.get_channel(id_, removed) == OB_HASH_NOT_EXIST && !removed);
      ObPxFifoReceiveOp::destroy();
    }
    void check_finished(int64_t blocks) {
      CHECK(msg_loop_.get_eof_cnt() == 1 && msg_loop_.all_eof(1));
      CHECK(channel_->is_eof() && !row_reader_.has_more());
      auto &channel = static_cast<ObDtlLocalChannel &>(*channel_);
      CHECK(channel.get_recv_buffer_cnt() == blocks && channel.get_processed_buffer_cnt() == blocks);
      CHECK(channel.get_pins() == 1); // only the registry pin, no leaked peer lookup
      CHECK(dfc_.get_used() == 0 && dfc_.get_total_buffer_cnt() == 0 && !dfc_.is_block());
    }
    bool blocked() { return dfc_.is_block(); }
    int64_t blocked_count() { return dfc_.get_accumulated_blocked_cnt(); }
    void restart(int64_t batch) {
      CHECK(channel_->is_eof() && msg_loop_.get_eof_cnt() == 1 && !row_reader_.has_more());
      const int64_t old_batch = channel_->get_batch_id(); CHECK(old_batch != batch);
      auto *manager = oceanbase::share::server_service<ObDTLIntermResultManager>(); CHECK(manager);
      ObDTLIntermResultKey old_key, other_key;
      old_key.channel_id_ = other_key.channel_id_ = id_;
      old_key.batch_id_ = old_batch; other_key.batch_id_ = batch + 100;
      const auto insert = [&](ObDTLIntermResultKey &key) {
        ObMemAttr attr("RustPxRescan"); ObDTLIntermResultInfoGuard guard;
        ObDTLIntermResultMonitorInfo monitor;
        CHECK(manager->create_interm_result_info(attr, guard, monitor) == OB_SUCCESS);
        CHECK(manager->insert_interm_result_info(key, guard.result_info_) == OB_SUCCESS);
      };
      insert(old_key); insert(other_key);
      ObDTLIntermResultInfo result;
      CHECK(manager->get_interm_result_info(old_key, result) == OB_SUCCESS);
      CHECK(rescan() == OB_SUCCESS);
      CHECK(manager->get_interm_result_info(old_key, result) == OB_HASH_NOT_EXIST);
      CHECK(manager->get_interm_result_info(other_key, result) == OB_SUCCESS);
      CHECK(manager->erase_interm_result_info(other_key) == OB_SUCCESS);
      CHECK(!channel_->is_eof() && !channel_->channel_is_eof() && channel_->get_batch_id() == batch);
      CHECK(msg_loop_.get_eof_cnt() == 0 && !msg_loop_.all_eof(1) && !row_reader_.has_more());
      CHECK(channel_->get_pins() == 1);
    }
  protected:
    int try_link_channel() override { return OB_SUCCESS; } // SQC discovery is still controlled
  private:
    uint64_t id_; ObDtlChannel *channel_ = nullptr;
  };
public:
  struct Value { int64_t tablet, ddl; bool null; std::string bytes; };
  LinkedExchange(ObExecContext &sender, const ObIArray<ObExpr *> &exprs, bool split)
      : arena_("RustLinkedRecv"), receiver_(arena_), spec_(arena_, PHY_PX_FIFO_RECEIVE),
        input_(receiver_, spec_), tx_spec_(arena_, PHY_PX_REPART_TRANSMIT),
        unblock_(tx_dfc_), split_(split) {
    receiver_.set_my_session(sender.get_my_session()); receiver_.set_sql_ctx(sender.get_sql_ctx());
    CHECK(receiver_.create_physical_plan_ctx() == OB_SUCCESS);
    const auto *plan = sender.get_physical_plan_ctx()->get_phy_plan(); CHECK(plan);
    receiver_.get_physical_plan_ctx()->set_phy_plan(plan);
    timeout_ = ObTimeUtility::current_time() + 30 * 1000000L;
    receiver_.get_physical_plan_ctx()->set_timeout_timestamp(timeout_);
    CHECK(receiver_.init_expr_op(plan->get_expr_operator_size()) == OB_SUCCESS);
    CHECK(plan->get_expr_frame_info().pre_alloc_exec_memory(receiver_) == OB_SUCCESS);
    spec_.plan_ = const_cast<ObPhysicalPlan *>(plan); spec_.max_batch_size_ = 3;
    CHECK(spec_.output_.assign(exprs) == OB_SUCCESS && spec_.child_exprs_.assign(exprs) == OB_SUCCESS);
    CHECK(spec_.calc_exprs_.assign(exprs) == OB_SUCCESS);
    ObDtlChannelInfo tx_info, rx_info;
    CHECK(ObDtlChannelGroup::make_channel(tx_info, rx_info) == OB_SUCCESS);
    tx_id_ = tx_info.chid_;
    op_ = std::make_unique<Receive>(receiver_, spec_, &input_, rx_info.chid_);
    CHECK(op_->init() == OB_SUCCESS && op_->open() == OB_SUCCESS);
    tx_dfc_.set_dtl_channel_watcher(&tx_loop_);
    CHECK(tx_dfc_.init(1) == OB_SUCCESS); tx_dfc_.set_transmit(); tx_dfc_.set_timeout_ts(timeout_);
    tx_loop_.register_processor(unblock_);
    CHECK(DTL.create_local_channel(tx_id_, tx_, &tx_dfc_) == OB_SUCCESS && tx_);
    CHECK(static_cast<ObDtlLocalChannel *>(tx_)->get_peer_id() == rx_info.chid_);
    tx_spec_.plan_ = spec_.plan_; tx_spec_.max_batch_size_ = spec_.max_batch_size_;
    tx_rescan_ = std::make_unique<TransmitRescan>(receiver_, tx_spec_, tx_);
  }
  ~LinkedExchange() {
    CHECK(finished_);
    tx_rescan_.reset(); // drop the borrowed channel before unregistering it
    ObDtlChannel *removed = nullptr;
    CHECK(DTL.remove_channel(tx_id_, removed) == OB_SUCCESS && removed == tx_);
    CHECK(removed->get_pins() == 0);
    CHECK(DTL.get_dfc_server().unregister_dfc_channel(tx_dfc_, removed) == OB_SUCCESS);
    CHECK(tx_loop_.unregister_all_channel() == OB_SUCCESS);
    CHECK(removed->get_alloc_buffer_cnt() == removed->get_free_buffer_cnt());
    ob_delete(removed); tx_ = nullptr;
    CHECK(DTL.get_channel(tx_id_, removed) == OB_HASH_NOT_EXIST && !removed);
    op_.reset();
  }
  void append(const ObDtlMsg &msg, ObEvalCtx &eval) {
    CHECK(!finished_);
    CHECK(tx_->send(msg, timeout_, &eval, false) == OB_SUCCESS); ++sent_rows_;
    if (split_) {
      CHECK(tx_->flush(true, true) == OB_SUCCESS);
      CHECK(read(1) == 1); // bounded synchronous interleaving, not backpressure
    }
  }
  void restart() {
    CHECK(finished_);
    const int64_t batch = receiver_.get_px_batch_id() + 1;
    ObBatchRescanParams params;
    ObSEArray<int64_t, 1> indexes; ObTMArray<ObObjParam> objects;
    for (int64_t i = 0; i <= batch; ++i) CHECK(params.append_batch_rescan_param(indexes, objects) == OB_SUCCESS);
    CHECK(receiver_.fill_px_batch_info(params, batch, spec_.plan_->get_expr_frame_info().rt_exprs_) == OB_SUCCESS);
    op_->restart(batch);
    CHECK(tx_->is_eof() && tx_->get_batch_id() == batch - 1);
    CHECK(tx_rescan_->rescan() == OB_SUCCESS);
    CHECK(!tx_->is_eof() && !tx_->channel_is_eof() && tx_->get_batch_id() == batch);
    CHECK(tx_->get_pins() == 1);
    block_base_ = static_cast<ObDtlLocalChannel *>(tx_)->get_send_buffer_cnt();
    sent_rows_ = 0; values_.clear(); finished_ = false;
  }
  void exercise_pressure(const ObDtlMsg &msg, ObEvalCtx &eval) {
    CHECK(split_ && sent_rows_ == 0 && !finished_);
    // Small valid channel buffers bound physical allocation while production
    // DFC still uses its unchanged queue-byte/count and global-budget policy.
    tx_->set_send_buffer_size(1024);
    while (!op_->blocked() && sent_rows_ < 65536) {
      CHECK(tx_->send(msg, timeout_, &eval, false) == OB_SUCCESS); ++sent_rows_;
      CHECK(tx_->flush(true, true) == OB_SUCCESS);
    }
    CHECK(op_->blocked() && op_->blocked_count() == 1 && sent_rows_ >= 3);
    auto &tx = static_cast<ObDtlLocalChannel &>(*tx_);
    CHECK(tx.get_recv_buffer_cnt() == 0 && tx.get_processed_buffer_cnt() == 0);
    const int64_t sent_buffers = tx.get_send_buffer_cnt();
    CHECK(sent_buffers == sent_rows_);
    // A pending block response becomes actual transmit DFC state; with no
    // receiver progress there is no unblock message and the real wait times out.
    tx_dfc_.set_timeout_ts(ObTimeUtility::current_time() - 1);
    CHECK(tx.wait_unblocking_if_blocked() == OB_TIMEOUT);
    CHECK(tx_dfc_.is_block() && op_->blocked() && tx.get_send_buffer_cnt() == sent_buffers);
    CHECK(tx_dfc_.get_accumulated_blocked_cnt() == 1);
    tx_dfc_.set_timeout_ts(timeout_);
    while (values_.size() < size_t(sent_rows_)) {
      const int64_t request = std::min(int64_t(2), sent_rows_ - int64_t(values_.size()));
      CHECK(read(request) == request);
    }
    // Draining production receive buffers generates a real control message.
    CHECK(!op_->blocked() && tx_dfc_.is_block());
    CHECK(tx.get_recv_buffer_cnt() == 1 && tx.get_processed_buffer_cnt() == 0);
    CHECK(tx_->send(msg, timeout_, &eval, false) == OB_SUCCESS); ++sent_rows_;
    CHECK(tx_->flush(true, true) == OB_SUCCESS);
    CHECK(!tx_dfc_.is_block() && tx.get_recv_buffer_cnt() == 1);
    // Control-message dispatch succeeds before its buffer reaches ITER_END.
    // The next poll observes exhaustion and returns that buffer to the pool.
    CHECK(tx_loop_.process_any() == OB_DTL_WAIT_EAGAIN);
    CHECK(tx.get_processed_buffer_cnt() == 1);
    CHECK(tx_dfc_.get_accumulated_blocked_cnt() == 1 && op_->blocked_count() == 1);
    CHECK(read(1) == 1);
  }
  const std::vector<Value> &finish() {
    CHECK(!finished_);
    ObSEArray<ObDtlChannel *, 1> channels;
    CHECK(channels.push_back(tx_) == OB_SUCCESS);
    ObTransmitEofAsynSender eof(channels, timeout_, nullptr, PX_DATUM_ROW);
    CHECK(eof.asyn_send() == OB_SUCCESS);
    while (values_.size() < size_t(sent_rows_)) {
      const int64_t remaining = sent_rows_ - values_.size();
      CHECK(read(2) == std::min(int64_t(2), remaining));
    }
    CHECK(read(2) == 0 && read(2) == 0);
    const int64_t blocks = block_base_ + (split_ ? sent_rows_ + 1 : 1);
    CHECK(static_cast<ObDtlLocalChannel *>(tx_)->get_send_buffer_cnt() == blocks);
    CHECK(tx_->get_pins() == 1 && tx_->get_alloc_buffer_cnt() == tx_->get_free_buffer_cnt());
    op_->check_finished(blocks); finished_ = true;
    return values_;
  }
private:
  int64_t read(int64_t request) {
    const ObBatchRows *batch = nullptr;
    CHECK(op_->get_next_batch(request, batch) == OB_SUCCESS && batch);
    if (batch->end_) { CHECK(batch->size_ == 0 && int64_t(values_.size()) == sent_rows_); return 0; }
    CHECK(batch->size_ > 0 && batch->size_ <= request);
    auto &eval = op_->get_eval_ctx(); ObEvalCtx::BatchInfoScopeGuard frame(eval);
    frame.set_batch_size(batch->size_);
    for (int64_t i = 0; i < batch->size_; ++i) {
      CHECK(!batch->skip_->at(i)); frame.set_batch_idx(i);
      const auto &tablet = spec_.output_.at(0)->locate_expr_datum(eval);
      const auto &ddl = spec_.output_.at(1)->locate_expr_datum(eval);
      const auto &value = spec_.output_.at(2)->locate_expr_datum(eval);
      CHECK(!tablet.is_null() && !ddl.is_null());
      Value result{tablet.get_int(), ddl.get_int(), value.is_null(), {}};
      if (!result.null) result.bytes.assign(value.ptr_, value.len_);
      values_.push_back(std::move(result));
    }
    return batch->size_;
  }
  ObArenaAllocator arena_; ObExecContext receiver_; ObPxFifoReceiveSpec spec_;
  ObPxFifoReceiveOpInput input_; std::unique_ptr<Receive> op_;
  ObPxTransmitSpec tx_spec_; std::unique_ptr<TransmitRescan> tx_rescan_;
  ObDtlChannelLoop tx_loop_; ObDtlFlowControl tx_dfc_; ObDtlUnblockingMsgP unblock_;
  ObDtlChannel *tx_ = nullptr;
  uint64_t tx_id_ = 0; int64_t timeout_ = 0, sent_rows_ = 0, block_base_ = 0;
  bool split_, finished_ = false; std::vector<Value> values_;
};
}
#endif
