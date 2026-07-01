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

#ifndef OCEANBASE_BASIC_OB_BLOCK_IREADER_H_
#define OCEANBASE_BASIC_OB_BLOCK_IREADER_H_

#include "share/ob_define.h"
#include "lib/container/ob_se_array.h"
#include "lib/allocator/page_arena.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/list/ob_dlist.h"
#include "src/share/datum/ob_datum.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/basic/chunk_store/ob_chunk_block.h"
#include "sql/engine/basic/ob_temp_block_store.h"
#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/basic/ob_temp_block_store.h"
#include "sql/engine/basic/chunk_store/ob_chunk_block.h"

namespace oceanbase
{
namespace sql
{

class ObBlockIReader
{
public:
  explicit ObBlockIReader(ObTempBlockStore *store) : store_(store), cur_blk_(nullptr) {};
  virtual ~ObBlockIReader() { reset(); };

  void reset()
  {
    cur_blk_ = nullptr;
    store_ = nullptr;
  }
  virtual void reuse() = 0;
  virtual int get_row(const ObChunkDatumStore::StoredRow *&sr) = 0;
  virtual void set_meta(const ChunkRowMeta* row_meta) = 0;
  void set_block(const ObTempBlockStore::Block *blk) { cur_blk_ = blk; }
  const ObTempBlockStore::Block *get_block() {return cur_blk_; }
  virtual int prepare_blk_for_read(ObTempBlockStore::Block *blk)  = 0;

protected:
  ObTempBlockStore *store_;
  const ObTempBlockStore::Block* cur_blk_;
};

} // end namespace sql
} // end namespace oceanbase

#endif // OCEANBASE_BASIC_OB_BLOCK_IREADER_H_
