/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX STORAGE

#include "ob_lob_piece.h"
#include "src/storage/ls/ob_ls_tablet_service.h"


namespace oceanbase
{
namespace storage
{


int ObLobPieceUtil::transform_piece_id(blocksstable::ObDatumRow* row, ObLobPieceInfo &info)
{
  info.piece_id_ = row->storage_datums_[0].get_uint64(); 
  return OB_SUCCESS; 
}

int ObLobPieceUtil::transform_len(blocksstable::ObDatumRow* row, ObLobPieceInfo &info)
{
  info.len_ = row->storage_datums_[1].get_uint32(); 
  return OB_SUCCESS;
}

int ObLobPieceUtil::transform_macro_id(blocksstable::ObDatumRow* row, ObLobPieceInfo &info)
{
  int ret = OB_SUCCESS;
  ObString ser_macro_id = row->storage_datums_[2].get_string();;
  int64_t pos = 0;
  if (OB_FAIL(info.macro_id_.deserialize(ser_macro_id.ptr(), ser_macro_id.length(), pos))) {
    LOG_WARN("deserialize macro id from buffer failed.", K(ret), K(ser_macro_id));
  }
  return ret;
}







} // storage
} // oceanbase
