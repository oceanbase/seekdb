// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Production transmit loop with captured channels, not an inter-process test.
#ifndef SEEKDB_TEST_RUST_TABLET_TRANSMIT_FIXTURE_H_
#define SEEKDB_TEST_RUST_TABLET_TRANSMIT_FIXTURE_H_
#include "sql/engine/px/exchange/ob_px_transmit_op.h"
#include "sql/dtl/ob_dtl_local_channel.h"
#include "rust_dtl_wire_fixture.h"
namespace rust_tablet_transmit_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
struct Sent { int channel; int64_t tablet; int64_t ddl; };
class Capture final : public oceanbase::sql::dtl::ObDtlLocalChannel {
public:
  Capture(int channel, std::vector<Sent> &sent)
      : ObDtlLocalChannel(2 * (channel + 1)), channel_(channel), sent_(sent),
        wire_(2 * (channel + 1)), split_wire_(2 * (channel + 1), 1) {}
  int send(const oceanbase::sql::dtl::ObDtlMsg &msg, int64_t, ObEvalCtx *eval, bool = false) override {
    const auto *row = dynamic_cast<const ObPxNewRow *>(&msg); CHECK(row && eval);
    if (const auto *exprs = row->get_exprs()) {
      CHECK(exprs->count() == 2 || exprs->count() == 3);
      ObDatum *tablet = nullptr, *ddl = nullptr;
      CHECK(exprs->at(0)->eval(*eval, tablet) == OB_SUCCESS && tablet && !tablet->is_null());
      CHECK(exprs->at(1)->eval(*eval, ddl) == OB_SUCCESS && ddl && !ddl->is_null());
      sent_.push_back({channel_, tablet->get_int(), ddl->get_int()});
      if (exprs->count() == 3) { wire_.append(msg, *eval); split_wire_.append(msg, *eval); }
    }
    return OB_SUCCESS;
  }
  int push_buffer_batch_info() override { return OB_SUCCESS; }
  void verify_wire(ObExecContext &exec, const ObIArray<ObExpr *> &exprs) {
    wire_.verify(exec, exprs); split_wire_.verify(exec, exprs);
  }
private:
  int channel_; std::vector<Sent> &sent_;
  rust_dtl_wire_test::WireRows wire_, split_wire_;
};
class Transmit final : public ObPxTransmitOp {
public:
  Transmit(ObExecContext &exec, const ObPxTransmitSpec &spec, std::vector<Sent> &sent)
      : ObPxTransmitOp(exec, spec, nullptr) {
    CHECK(is_vectorized());
    receive_channel_ready_ = true;
    batch_param_remain_ = true; // Exercise the per-batch EOF path, not async task EOF.
    dfc_.set_dtl_channel_watcher(&loop_);
    CHECK(dfc_.init(18) == OB_SUCCESS); dfc_.set_transmit();
    for (int i = 0; i < 18; ++i) {
      owners_.push_back(std::make_unique<Capture>(i, sent));
      CHECK(task_channels_.push_back(owners_.back().get()) == OB_SUCCESS);
      CHECK(dfc_.register_channel(owners_.back().get()) == OB_SUCCESS);
      CHECK(ch_blocks_.push_back(nullptr) == OB_SUCCESS);
      CHECK(blk_bufs_.push_back(ObChunkDatumStore::BlockBufferWrap()) == OB_SUCCESS);
    }
  }
  ~Transmit() override {
    CHECK(dfc_.unregister_all_channel() == OB_SUCCESS);
    loop_.reset();
    task_channels_.reset();
  }
  template <ObSliceIdxCalc::SliceCalcType Calc>
  int run(ObSliceIdxCalc &routing, uint64_t skip) {
    // The real next_row() consumes this prefetched batch, as after sampling.
    consume_first_row_ = false; sample_done_ = false;
    bits_ = skip;
    brs_.size_ = 3; brs_.end_ = true; brs_.skip_ = to_bit_vector(&bits_);
    brs_.all_rows_active_ = skip == 0;
    return send_rows_in_batch<Calc>(routing);
  }
  void verify_wire(ObExecContext &exec, const ObIArray<ObExpr *> &exprs) {
    for (auto &channel : owners_) channel->verify_wire(exec, exprs);
  }
protected:
  int do_transmit() override { return OB_NOT_SUPPORTED; }
private:
  uint64_t bits_ = 0;
  std::vector<std::unique_ptr<Capture>> owners_;
};
template <ObSliceIdxCalc::SliceCalcType Calc = ObSliceIdxCalc::SM_REPART_RANGE>
inline void run(ObExecContext &exec, ObSliceIdxCalc &routing, ObExpr &tablet, ObExpr &ddl,
    const std::vector<int> &channels, const std::vector<int> &tablets,
    const std::vector<int64_t> &ddl_ids, uint64_t skip = 0, int expected_error = OB_SUCCESS,
    ObExpr *value = nullptr)
{
  CHECK(tablet.type_ == T_PDML_PARTITION_ID && channels.size() == 3 && tablets.size() == 3);
  ObPxTransmitSpec spec(exec.get_allocator(), PHY_PX_REPART_TRANSMIT);
  // The SQL fixture owns a mutable physical plan; this spec only borrows it.
  spec.plan_ = const_cast<ObPhysicalPlan *>(exec.get_physical_plan_ctx()->get_phy_plan());
  spec.max_batch_size_ = 3;
  spec.tablet_id_expr_ = &tablet;
  CHECK(spec.output_.init(value ? 3 : 2) == OB_SUCCESS);
  CHECK(spec.output_.push_back(&tablet) == OB_SUCCESS && spec.output_.push_back(&ddl) == OB_SUCCESS);
  if (value) CHECK(spec.output_.push_back(value) == OB_SUCCESS);
  std::vector<Sent> sent;
  Transmit transmit(exec, spec, sent);
  CHECK(transmit.run<Calc>(routing, skip) == expected_error);
  transmit.verify_wire(exec, spec.output_);
  if (expected_error != OB_SUCCESS) { CHECK(sent.empty()); return; }
  CHECK(ddl_ids.size() == 3);
  size_t position = 0;
  for (int i = 0; i < 3; ++i) if (!(skip & (uint64_t(1) << i))) {
    CHECK(position < sent.size());
    CHECK(sent[position].channel == channels[i] && sent[position].tablet == tablets[i]);
    if (sent[position].ddl != ddl_ids[i]) {
      std::cerr << "transmit DDL row=" << i << " actual=" << sent[position].ddl
                << " expected=" << ddl_ids[i] << " tablet=" << sent[position].tablet << std::endl;
    }
    CHECK(sent[position].ddl == ddl_ids[i]);
    ++position;
  }
  CHECK(sent.size() == position);
}
} // namespace rust_tablet_transmit_test
#endif
