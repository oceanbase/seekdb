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

#ifndef OCEANBASE_STORAGE_OB_BLOOM_FILTER_TASK_H_
#define OCEANBASE_STORAGE_OB_BLOOM_FILTER_TASK_H_

#include "lib/queue/ob_dedup_queue.h"
#include "storage/ob_i_store.h"
#include "storage/ob_i_table.h"
#include "storage/blocksstable/ob_macro_block_id.h"

namespace oceanbase
{
namespace storage
{

class ObBloomFilterBuildTask: public common::IObDedupTask
{
public:
  ObBloomFilterBuildTask(
      const uint64_t tenant_id,
      const uint64_t table_id,
      const blocksstable::MacroBlockId &macro_id,
      const int64_t prefix_len);
  virtual ~ObBloomFilterBuildTask();
  virtual int64_t hash() const;
  virtual bool operator ==(const IObDedupTask &other) const;
  virtual int64_t get_deep_copy_size() const;
  virtual IObDedupTask *deep_copy(char *buffer, const int64_t buf_size) const;
  virtual int64_t get_abs_expired_time() const {  return 0;  }
  virtual int process();
private:
  int build_bloom_filter();
  uint64_t tenant_id_;
  uint64_t table_id_;
  blocksstable::MacroBlockId macro_id_;
  blocksstable::ObStorageObjectHandle macro_handle_;
  int64_t prefix_len_;
  common::ObArenaAllocator allocator_;
  char *io_buf_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObBloomFilterBuildTask);
};

} /* namespace storage */
} /* namespace oceanbase */

#endif /* OCEANBASE_STORAGE_OB_BLOOM_FILTER_TASK_H_ */
