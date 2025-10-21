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

#ifndef OCEANBASE_STORAGE_OB_ROW_COMPACTOR_
#define OCEANBASE_STORAGE_OB_ROW_COMPACTOR_

#include "share/ob_define.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/allocator/page_arena.h"
#include "lib/allocator/ob_small_allocator.h"
#include "lib/lock/ob_spin_lock.h"
#include "common/object/ob_object.h"
#include "share/scn.h"

namespace oceanbase
{

namespace common
{
class ObIAllocator;
}

namespace storage
{
class ObTxTableGuard;
}

namespace memtable
{

class ObMemtable;
struct ObMvccRow;
struct ObMvccTransNode;

// Memtable Row Compactor.
class ObMemtableRowCompactor
{
public:
  ObMemtableRowCompactor();
  virtual ~ObMemtableRowCompactor();
private:
  DISALLOW_COPY_AND_ASSIGN(ObMemtableRowCompactor);
public:
  int init(ObMvccRow *row,
           ObMemtable *mt,
           common::ObIAllocator *node_alloc);
  // compact and refresh the update counter by snapshot version
  int compact(const share::SCN snapshot_version,
              const int64_t flag);
private:
  void find_start_pos_(const share::SCN snapshot_version,
                       ObMvccTransNode *&save);
  ObMvccTransNode *construct_compact_node_(const share::SCN snapshot_version,
                                           const int64_t flag,
                                           ObMvccTransNode *save);
  int try_cleanout_tx_node_during_compact_(storage::ObTxTableGuard &tx_table_guard,
                                            ObMvccTransNode *tnode);
  void insert_compact_node_(ObMvccTransNode *trans_node,
                            ObMvccTransNode *save);
private:
  bool is_inited_;
  ObMvccRow *row_;
  ObMemtable *memtable_;
  common::ObIAllocator *node_alloc_;
};


}
}



#endif
