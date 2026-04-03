
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

#ifndef OCEANBASE_STORAGE_OB_TABLET_TRANSFER_TX_CTX
#define OCEANBASE_STORAGE_OB_TABLET_TRANSFER_TX_CTX

namespace oceanbase
{
namespace storage
{

#include "share/scn.h"
#include "share/ob_ls_id.h"
#include "storage/multi_data_source/mds_ctx.h"
#include "storage/tablelock/ob_table_lock_common.h"

struct ObTxCtxMoveArg
{
  OB_UNIS_VERSION(1);
public:
  transaction::ObTransID tx_id_;
  int64_t epoch_;
  uint32_t session_id_;
  uint32_t associated_session_id_;
  uint32_t client_sid_;
  transaction::ObTxState tx_state_;
  share::SCN trans_version_;
  share::SCN prepare_version_;
  share::SCN commit_version_;
  uint64_t cluster_id_;
  uint64_t cluster_version_;
  common::ObAddr scheduler_;
  int64_t tx_expired_time_;
  transaction::ObXATransID xid_;
  transaction::ObTxSEQ last_seq_no_;
  transaction::ObTxSEQ max_submitted_seq_no_;
  share::SCN tx_start_scn_;
  share::SCN tx_end_scn_;
  bool is_sub2pc_;
  bool happened_before_;
  transaction::tablelock::ObTableLockInfo table_lock_info_;

  TO_STRING_KV(K_(tx_id), K_(epoch), K_(session_id), K_(tx_state), K_(trans_version), K_(prepare_version), K_(commit_version),
      K_(cluster_id), K_(cluster_version), K_(scheduler), K_(tx_expired_time), K_(xid), K_(last_seq_no), K_(max_submitted_seq_no),
      K_(tx_start_scn), K_(tx_end_scn), K_(is_sub2pc), K_(happened_before), K_(table_lock_info));
};

struct ObTransferMoveTxParam
{
  OB_UNIS_VERSION_V(1);
public:
  ObTransferMoveTxParam(share::ObLSID ls_id, int64_t transfer_epoch, share::SCN transfer_scn,
      share::SCN op_scn, transaction::NotifyType op_type, bool is_replay, bool is_incomplete_replay)
    : src_ls_id_(ls_id),
      transfer_epoch_(transfer_epoch),
      transfer_scn_(transfer_scn),
      op_scn_(op_scn),
      op_type_(op_type),
      is_replay_(is_replay),
      is_incomplete_replay_(is_incomplete_replay) {}
  ~ObTransferMoveTxParam() { reset(); }
  void reset();
  TO_STRING_KV(K_(src_ls_id), K_(transfer_epoch), K_(transfer_scn),
      K_(op_scn), K_(op_type), K_(is_replay), K_(is_incomplete_replay));

  share::ObLSID src_ls_id_;
  int64_t transfer_epoch_;
  share::SCN transfer_scn_;
  share::SCN op_scn_;
  transaction::NotifyType op_type_;
  bool is_replay_;
  bool is_incomplete_replay_;
};

struct ObTransferOutTxParam
{
  ObTransferOutTxParam() { reset(); }
  ~ObTransferOutTxParam() { reset(); }
  void reset();
  TO_STRING_KV(K_(except_tx_id), K_(data_end_scn), K_(op_scn), K_(op_type),
      K_(is_replay), K_(dest_ls_id), K_(transfer_epoch), K_(move_tx_ids));
  int64_t except_tx_id_;
  share::SCN data_end_scn_;
  share::SCN op_scn_;
  transaction::NotifyType op_type_;
  bool is_replay_;
  share::ObLSID dest_ls_id_;
  int64_t transfer_epoch_;
  ObIArray<transaction::ObTransID> *move_tx_ids_;
};

struct CollectTxCtxInfo final
{
  OB_UNIS_VERSION(1);
public:
  CollectTxCtxInfo() { reset(); }
  ~CollectTxCtxInfo() { reset(); }
  bool is_valid() {
    return src_ls_id_.is_valid() &&
           dest_ls_id_.is_valid() &&
           task_id_ > 0 &&
           transfer_epoch_ > 0 &&
           transfer_scn_.is_valid() &&
           args_.count() > 0;
  }
  void reset() {
    src_ls_id_.reset();
    dest_ls_id_.reset();
    task_id_ = 0;
    transfer_epoch_ = 0;
    transfer_scn_.reset();
    args_.reset();
  }
  int assign(const CollectTxCtxInfo& other);
  share::ObLSID src_ls_id_;
  share::ObLSID dest_ls_id_;
  int64_t task_id_;
  int64_t transfer_epoch_;
  share::SCN transfer_scn_;
  ObSArray<ObTxCtxMoveArg> args_;

  TO_STRING_KV(K_(src_ls_id), K_(dest_ls_id), K_(task_id), K_(transfer_epoch), K_(transfer_scn), K_(args));
};


} // end storage
} // end oceanbase


#endif
