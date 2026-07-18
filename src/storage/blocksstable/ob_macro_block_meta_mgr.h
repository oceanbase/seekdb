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

#ifndef OB_MACRO_BLOCK_META_MGR_H_
#define OB_MACRO_BLOCK_META_MGR_H_
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace blocksstable
{
// Only support major sstable key whose data_version_ is not 0.
struct ObMajorMacroBlockKey
{
  ObMajorMacroBlockKey() { reset(); }
  bool is_valid() const { return table_id_ > 0 && partition_id_ >= 0 && data_version_ > 0 && data_seq_ >= 0; }
  void reset();
  TO_STRING_KV(K_(table_id), K_(partition_id), K_(data_version), K_(data_seq));

  uint64_t table_id_;
  int64_t partition_id_;
  int64_t data_version_;
  int64_t data_seq_;
};
}
}

#endif /* OB_MACRO_BLOCK_META_MGR_H_ */
