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
#ifndef OB_STORAGE_COLUMN_STORE_OB_COLUMN_STORE_CONST_H_
#define OB_STORAGE_COLUMN_STORE_OB_COLUMN_STORE_CONST_H_

#include <stdint.h>
#include "lib/ob_define.h"

namespace oceanbase
{
namespace storage
{

typedef int64_t ObCSRowId;
const ObCSRowId OB_INVALID_CS_ROW_ID = -1;
const uint32_t OB_CS_INVALID_CG_IDX = INT32_MAX;
const uint32_t OB_CS_VIRTUAL_CG_IDX = INT32_MAX - 1;
const uint32_t OB_CS_COLUMN_REPLICA_ROWKEY_CG_IDX = 0;

OB_INLINE bool is_virtual_cg(const uint32_t cg_idx)
{
  return OB_CS_VIRTUAL_CG_IDX == cg_idx;
}

enum BlockScanState
{
  BLOCKSCAN_RANGE = 0,
  SWITCH_RANGE,
  BLOCKSCAN_FINISH,
  SCAN_FINISH,
  MAX_STATE,
};

}  // namespace storage
}  // namespace oceanbase

#endif /* OB_STORAGE_COLUMN_STORE_OB_COLUMN_STORE_CONST_H_ */
