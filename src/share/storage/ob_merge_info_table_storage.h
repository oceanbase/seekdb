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

#ifndef OCEANBASE_SHARE_STORAGE_OB_MERGE_INFO_TABLE_STORAGE_H_
#define OCEANBASE_SHARE_STORAGE_OB_MERGE_INFO_TABLE_STORAGE_H_

#include "share/storage/local_store.h"
#include "share/ob_merge_info.h"
#include "share/ob_global_merge_table_operator.h"

namespace oceanbase
{
namespace share
{

class ObMergeInfoTableStorage
{
public:
  ObMergeInfoTableStorage();
  virtual ~ObMergeInfoTableStorage();

  int init(LocalStore *store);
  bool is_inited() const { return nullptr != store_; }

  // Insert or update global merge info
  int insert_or_update(const ObGlobalMergeInfo &global_merge_info);

  // Get global merge info
  int get(ObGlobalMergeInfo &global_merge_info);

private:
  LocalStore *store_;
  DISALLOW_COPY_AND_ASSIGN(ObMergeInfoTableStorage);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_STORAGE_OB_MERGE_INFO_TABLE_STORAGE_H_

