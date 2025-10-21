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
