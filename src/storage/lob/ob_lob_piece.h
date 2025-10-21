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

#ifndef OCEABASE_STORAGE_OB_LOB_PIECE_
#define OCEABASE_STORAGE_OB_LOB_PIECE_
#include "lib/lock/ob_spin_lock.h"
#include "lib/task/ob_timer.h"
#include "storage/blocksstable/ob_macro_block_id.h"
#include "ob_lob_util.h"
#include "ob_lob_persistent_adaptor.h"

namespace oceanbase
{
namespace storage
{

class ObLobPieceUtil {
public:
  static const uint64_t LOB_PIECE_COLUMN_CNT = 3;
  static const uint64_t PIECE_ID_COL_ID = 0;
  static const uint64_t LEN_COL_ID = 1;
  static const uint64_t MACRO_ID_COL_ID = 2;
public:
private:
  static int transform_piece_id(blocksstable::ObDatumRow* row, ObLobPieceInfo &info);
  static int transform_len(blocksstable::ObDatumRow* row, ObLobPieceInfo &info);
  static int transform_macro_id(blocksstable::ObDatumRow* row, ObLobPieceInfo &info);
};

class ObLobPieceManager
{
public:
  explicit ObLobPieceManager(const uint64_t tenant_id):
    persistent_lob_adapter_(tenant_id)
  {}
  ~ObLobPieceManager() {}
  TO_STRING_KV("[LOB]", "piece mngr");
private:
  ObPersistentLobApator persistent_lob_adapter_;
};


} // storage
} // oceanbase

#endif


