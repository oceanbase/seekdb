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

#ifndef OCEANBASE_DATA_PLANE_TRANSACTION_OB_VIRTUAL_TX_DATA_H_
#define OCEANBASE_DATA_PLANE_TRANSACTION_OB_VIRTUAL_TX_DATA_H_

#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"
#include "share/scn.h"

namespace oceanbase
{
namespace data_plane
{

struct ObVirtualTxDataRow
{
  int32_t state_;
  share::SCN start_scn_;
  share::SCN end_scn_;
  share::SCN commit_version_;
  char undo_status_list_str_[common::MAX_UNDO_LIST_CHAR_LENGTH];
  char tx_op_str_[common::MAX_TX_OP_CHAR_LENGTH];

  ObVirtualTxDataRow()
    : state_(0), start_scn_(), end_scn_(), commit_version_()
  {
    undo_status_list_str_[0] = '\0';
    tx_op_str_[0] = '\0';
  }

  TO_STRING_KV(K(state_), K(start_scn_), K(end_scn_), K(commit_version_),
               K(undo_status_list_str_), K(tx_op_str_));
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_TRANSACTION_OB_VIRTUAL_TX_DATA_H_
