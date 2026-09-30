// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_RUST_EOF_SENDER_FIXTURE_H_
#define SEEKDB_TEST_RUST_EOF_SENDER_FIXTURE_H_
#include "sql/dtl/ob_dtl_utils.h"
#include "sql/dtl/ob_dtl_channel_group.h"
#include "sql/dtl/ob_dtl_local_channel.h"
#include "sql/engine/px/ob_px_dtl_proc.h"
namespace rust_dtl_wire_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::sql::dtl;
inline void verify_eof_sender()
{
  // The production local "async" sender batches initiation/completion; this
  // fixture does not substitute a scheduler or claim concurrent execution.
  for (int missing : {-1, 0, 1, 2}) {
    ObDtlChannelLoop tx_loop, rx_loop;
    ObDtlFlowControl tx_dfc, rx_dfc;
    tx_dfc.set_dtl_channel_watcher(&tx_loop); rx_dfc.set_dtl_channel_watcher(&rx_loop);
    CHECK(tx_dfc.init(3) == OB_SUCCESS && rx_dfc.init(3) == OB_SUCCESS);
    tx_dfc.set_transmit(); rx_dfc.set_receive();
    const int64_t timeout = ObTimeUtility::current_time() + 30 * 1000000L;
    tx_dfc.set_timeout_ts(timeout); rx_dfc.set_timeout_ts(timeout);
    ObReceiveRowReader reader; ObPxReceiveRowP processor(&reader);
    rx_loop.register_processor(processor);
    ObSEArray<ObDtlChannel *, 3> transmit, receive;
    for (int i = 0; i < 3; ++i) {
      ObDtlChannelInfo tx_info, rx_info;
      CHECK(ObDtlChannelGroup::make_channel(tx_info, rx_info) == OB_SUCCESS);
      ObDtlChannel *tx = nullptr, *rx = nullptr;
      CHECK(DTL.create_local_channel(tx_info.chid_, tx, &tx_dfc) == OB_SUCCESS && tx);
      CHECK(transmit.push_back(tx) == OB_SUCCESS);
      if (i != missing) {
        CHECK(DTL.create_local_channel(rx_info.chid_, rx, &rx_dfc) == OB_SUCCESS && rx);
        CHECK(receive.push_back(rx) == OB_SUCCESS);
      }
    }
    class Sender final : public ObTransmitEofAsynSender {
    public:
      Sender(ObIArray<ObDtlChannel *> &channels, int64_t timeout)
          : ObTransmitEofAsynSender(channels, timeout, nullptr, PX_DATUM_ROW) {}
      int action(ObDtlChannel *channel) override {
        attempted.push_back(channel->get_id());
        return ObTransmitEofAsynSender::action(channel);
      }
      std::vector<uint64_t> attempted;
    } sender(transmit, timeout);
    const int expected = missing < 0 ? OB_SUCCESS : OB_ERR_SIGNALED_IN_PARALLEL_QUERY_SERVER;
    CHECK(sender.asyn_send() == expected);
    CHECK(sender.attempted.size() == 3);
    for (int i = 0; i < 3; ++i) {
      auto &tx = static_cast<ObDtlLocalChannel &>(*transmit.at(i));
      CHECK(sender.attempted[i] == tx.get_id());
      CHECK(tx.get_send_buffer_cnt() == (i == missing ? 0 : 1));
      CHECK(tx.get_alloc_buffer_cnt() == tx.get_free_buffer_cnt() && tx.get_pins() == 1);
      // asyn_send already collected the response (and returned its error).
      // A second wait is a no-op, not a second delivery of that failure.
      CHECK(tx.wait_response() == OB_SUCCESS);
    }
    CHECK(rx_loop.get_eof_cnt() == 0);
    for (int64_t i = 0; i < receive.count(); ++i) CHECK(rx_loop.process_any() == OB_SUCCESS);
    CHECK(rx_loop.get_eof_cnt() == receive.count() && rx_loop.all_eof(receive.count()));
    CHECK(!reader.has_more() && reader.left_rows() == 0);
    for (auto *channel : receive) {
      auto &rx = static_cast<ObDtlLocalChannel &>(*channel);
      CHECK(rx.is_eof() && rx.get_recv_buffer_cnt() == 1 && rx.get_processed_buffer_cnt() == 1);
      CHECK(rx.get_alloc_buffer_cnt() == rx.get_free_buffer_cnt() && rx.get_pins() == 1);
    }
    CHECK(rx_dfc.get_used() == 0 && rx_dfc.get_total_buffer_cnt() == 0 && !rx_dfc.is_block());
    const auto remove = [](ObIArray<ObDtlChannel *> &channels, ObDtlFlowControl &dfc,
        ObDtlChannelLoop &loop) {
      CHECK(loop.unregister_all_channel() == OB_SUCCESS);
      for (int64_t i = 0; i < channels.count(); ++i) {
        const uint64_t id = channels.at(i)->get_id(); ObDtlChannel *removed = nullptr;
        CHECK(DTL.remove_channel(id, removed) == OB_SUCCESS && removed == channels.at(i));
        CHECK(removed->get_pins() == 0);
        CHECK(DTL.get_dfc_server().unregister_dfc_channel(dfc, removed) == OB_SUCCESS);
        ob_delete(removed); channels.at(i) = nullptr;
        CHECK(DTL.get_channel(id, removed) == OB_HASH_NOT_EXIST && !removed);
      }
    };
    remove(transmit, tx_dfc, tx_loop); remove(receive, rx_dfc, rx_loop);
  }
}
}
#endif
