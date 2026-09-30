// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_RUST_CHANNEL_RECEIVE_FIXTURE_H_
#define SEEKDB_TEST_RUST_CHANNEL_RECEIVE_FIXTURE_H_
#include "sql/dtl/ob_dtl_local_channel.h"
#include "sql/engine/px/exchange/ob_px_receive_op.h"
namespace rust_dtl_wire_test {
// Real local attach, notification, channel loop, row processor and receive DFC.
// SQC discovery and global DTL channel-map installation remain out of scope.
class ChannelReceive final : public oceanbase::sql::ObPxFifoReceiveOp {
public:
  ChannelReceive(oceanbase::sql::ObExecContext &ctx,
      const oceanbase::sql::ObOpSpec &spec, oceanbase::sql::ObOpInput *input,
      uint64_t id)
      : ObPxFifoReceiveOp(ctx, spec, input), data_(id), empty_(id + 2) {
    CHECK(data_.init() == OB_SUCCESS && empty_.init() == OB_SUCCESS);
    dfc_.set_dtl_channel_watcher(&msg_loop_);
    CHECK(dfc_.init(2) == OB_SUCCESS); dfc_.set_receive();
    msg_loop_.register_processor(px_row_msg_proc_);
    for (auto *channel : {&data_, &empty_}) {
      CHECK(task_channels_.push_back(channel) == OB_SUCCESS);
      CHECK(dfc_.register_channel(channel) == OB_SUCCESS);
    }
    CHECK(!all_eof() && eof_count() == 0);
  }
  ~ChannelReceive() override {
    // These channels were not installed in the global DTL map. Clean their
    // actual receive queues/DFC and detach watcher links before destruction.
    CHECK(dfc_.unregister_all_channel() == OB_SUCCESS);
    CHECK(dfc_.get_used() == 0 && dfc_.get_total_buffer_cnt() == 0 && !dfc_.is_block());
    for (auto *channel : {&data_, &empty_}) {
      // Cleanup does not advance processed_buffer_cnt for discarded messages,
      // so force unlink as global DTL teardown does, rather than has_msg().
      msg_loop_.remove_data_list(channel, true);
      channel->set_dfc(nullptr);
      CHECK(channel->get_alloc_buffer_cnt() == channel->get_free_buffer_cnt());
      channel->destroy(); // finish the channel while its watcher is still alive
    }
    CHECK(msg_loop_.unregister_all_channel() == OB_SUCCESS);
    task_channels_.reset();
    ObPxFifoReceiveOp::destroy();
  }
  void feed(oceanbase::sql::dtl::ObDtlLinkedBuffer *&buffer, bool empty_channel = false) {
    CHECK(buffer);
    auto &channel = empty_channel ? empty_ : data_;
    buffer->seq_no() = channel.get_recv_buffer_cnt() + 1;
    CHECK(channel.feedup(buffer) == OB_SUCCESS && !buffer);
  }
  int process_one() { return msg_loop_.process_any(); }
  int64_t eof_count() { return msg_loop_.get_eof_cnt(); }
  bool all_eof() const { return msg_loop_.all_eof(task_channels_.count()); }
  bool reader_empty() const { return !row_reader_.has_more() && row_reader_.left_rows() == 0; }
  int64_t processed_data() { return data_.get_processed_buffer_cnt(); }
  void check_consumed() {
    CHECK(all_eof() && eof_count() == 2 && reader_empty());
    for (auto *channel : {&data_, &empty_}) {
      CHECK(channel->is_eof());
      CHECK(channel->get_processed_buffer_cnt() == channel->get_recv_buffer_cnt());
      CHECK(channel->get_alloc_buffer_cnt() == channel->get_free_buffer_cnt());
    }
    CHECK(dfc_.get_used() == 0 && dfc_.get_total_buffer_cnt() == 0 && !dfc_.is_block());
  }
protected:
  int try_link_channel() override { return OB_SUCCESS; }
private:
  oceanbase::sql::dtl::ObDtlLocalChannel data_, empty_;
};
}
#endif
