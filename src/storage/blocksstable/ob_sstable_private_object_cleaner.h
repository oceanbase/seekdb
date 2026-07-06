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

#ifndef OCEANBASE_STORAGE_BLOCKSSTABLE_SSTABLE_PRIVATE_OBJECT_CLEANER_H_
#define OCEANBASE_STORAGE_BLOCKSSTABLE_SSTABLE_PRIVATE_OBJECT_CLEANER_H_

#include "lib/lock/ob_spin_lock.h"
#include "lib/container/ob_se_array.h"
#include "storage/blocksstable/ob_macro_block_id.h"

namespace oceanbase
{
namespace blocksstable
{

class ObDataStoreDesc;

class ObSSTablePrivateObjectCleaner final
{
private:
  static const int64_t CLEANER_MACRO_ID_LOCAL_ARRAY_SIZE = 32;

public:
  ObSSTablePrivateObjectCleaner();
  ~ObSSTablePrivateObjectCleaner();
  void reset();
  int add_new_macro_block_id(const MacroBlockId &macro_id);
  int mark_succeed();
  static int get_cleaner_from_data_store_desc(ObDataStoreDesc &data_store_desc, ObSSTablePrivateObjectCleaner *&cleaner);
  TO_STRING_KV(K_(new_macro_block_ids), K_(is_ss_mode), K_(task_succeed));

private:
  void clean();
  DISALLOW_COPY_AND_ASSIGN(ObSSTablePrivateObjectCleaner);

private:
  common::ObSEArray<MacroBlockId, CLEANER_MACRO_ID_LOCAL_ARRAY_SIZE> new_macro_block_ids_;
  SpinRWLock lock_;
  bool is_ss_mode_;
  bool task_succeed_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif
