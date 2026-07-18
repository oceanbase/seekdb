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

#define USING_LOG_PREFIX STORAGE

#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tablet/ob_tablet_transfer_tx_ctx.h"

namespace oceanbase
{
namespace storage
{
using namespace transaction;

OB_SERIALIZE_MEMBER(CollectTxCtxInfo,
                    src_ls_id_,
                    dest_ls_id_,
                    task_id_,
                    transfer_epoch_,
                    transfer_scn_,
                    args_);
OB_SERIALIZE_MEMBER(ObTxCtxMoveArg,
                    tx_id_,
                    epoch_,
                    session_id_,
                    tx_state_,
                    trans_version_,
                    prepare_version_,
                    commit_version_,
                    cluster_id_,
                    cluster_version_,
                    scheduler_,
                    tx_expired_time_,
                    xid_,
                    last_seq_no_,
                    max_submitted_seq_no_,
                    tx_start_scn_,
                    tx_end_scn_,
                    is_sub2pc_,
                    happened_before_,
                    table_lock_info_,
                    associated_session_id_,
                    client_sid_);
OB_SERIALIZE_MEMBER(ObTransferMoveTxParam,
                    src_ls_id_,
                    transfer_epoch_,
                    transfer_scn_,
                    op_scn_,
                    op_type_,
                    is_replay_,
                    is_incomplete_replay_);

int CollectTxCtxInfo::assign(const CollectTxCtxInfo &other)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(args_.assign(other.args_))) {
    LOG_WARN("collect tx ctx info assign failed", KR(ret), K(other));
  } else {
    src_ls_id_ = other.src_ls_id_;
    dest_ls_id_ = other.dest_ls_id_;
    task_id_ = other.task_id_;
    transfer_epoch_ = other.transfer_epoch_;
    transfer_scn_ = other.transfer_scn_;
  }
  return ret;
}

void ObTransferMoveTxParam::reset()
{
  src_ls_id_.reset();
  transfer_epoch_ = 0;
  transfer_scn_.reset();
  op_scn_.reset();
  op_type_ = NotifyType::UNKNOWN;
  is_replay_ = false;
  is_incomplete_replay_ = false;
}

void ObTransferOutTxParam::reset()
{
  except_tx_id_ = 0;
  data_end_scn_.reset();
  op_scn_.reset();
  op_type_ = NotifyType::UNKNOWN;
  is_replay_ = false;
  dest_ls_id_.reset();
  transfer_epoch_ = 0;
  move_tx_ids_ = nullptr;
}



} // end storage
} // end oceanbase
