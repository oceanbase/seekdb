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
#include "ob_tablet_transfer_info.h"

using namespace oceanbase;
using namespace share;
using namespace storage;


ObTabletTransferInfo::ObTabletTransferInfo()
  : ls_id_(),
    transfer_start_scn_(),
    transfer_seq_(-1),
    has_transfer_table_(false)
{
}

int ObTabletTransferInfo::init()
{
  int ret = OB_SUCCESS;
  ls_id_ = TRANSFER_INIT_LS_ID;
  transfer_start_scn_.set_min();
  transfer_seq_ = TRANSFER_INIT_SEQ;
  has_transfer_table_ = false;
  return ret;
}

int ObTabletTransferInfo::init(
    const share::ObLSID &ls_id,
    const share::SCN &transfer_start_scn,
    const int64_t transfer_seq)
{
  int ret = OB_SUCCESS;
  if (!ls_id.is_valid() || !transfer_start_scn.is_valid_and_not_min() || transfer_seq < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("init transfer info get invalid argument", K(ret), K(ls_id), K(transfer_start_scn), K(transfer_seq));
  } else {
    ls_id_ = ls_id;
    transfer_start_scn_ = transfer_start_scn;
    transfer_seq_ = transfer_seq;
    has_transfer_table_ = true;
  }
  return ret;
}

void ObTabletTransferInfo::reset()
{
  ls_id_.reset();
  transfer_start_scn_.reset();
  transfer_seq_ = -1;
  has_transfer_table_ = false;
}

bool ObTabletTransferInfo::is_valid() const
{
  return ls_id_.is_valid()
      && transfer_start_scn_.is_valid()
      && transfer_seq_ >= 0;
}

bool ObTabletTransferInfo::has_transfer_table() const
{
  return has_transfer_table_;
}

void ObTabletTransferInfo::reset_transfer_table()
{
  has_transfer_table_ = false;
  //transfer seq, ls id, transfer start scn will not change
}

OB_SERIALIZE_MEMBER(ObTabletTransferInfo, ls_id_, transfer_start_scn_, transfer_seq_, has_transfer_table_);

