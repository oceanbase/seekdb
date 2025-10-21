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
#ifndef _OB_EXTERNAL_TABLE_PART_INFO_H
#define _OB_EXTERNAL_TABLE_PART_INFO_H

#include <stdint.h>
#include "src/share/schema/ob_column_schema.h"

namespace oceanbase
{

namespace share
{
struct ObExternalTablePartInfo {
  ObExternalTablePartInfo() 
    : part_id_(common::OB_INVALID_ID),
      list_row_value_() {}

  TO_STRING_KV(K_(part_id), K_(list_row_value), K_(partition_spec));

  int64_t part_id_;
  common::ObNewRow list_row_value_;
  common::ObString partition_spec_;
};

class ObExternalTablePartInfoArray 
{
public:
  ObExternalTablePartInfoArray(common::ObIAllocator &alloc)
    : part_infos_(),
      allocator_(alloc) {}
  
  ~ObExternalTablePartInfoArray() {}

  TO_STRING_KV(K_(part_infos));

  int set_part_pair_by_idx(const int64_t idx, ObExternalTablePartInfo &pair);

  int reserve(const int64_t capacity);
  int serialize(char *buf, const int64_t buf_len, int64_t &pos) const;
  int deserialize(const char *buf, const int64_t data_len, int64_t &pos);
  int64_t get_serialize_size() const;
  int64_t count() const { return part_infos_.count(); }

  const ObExternalTablePartInfo &at(const int64_t idx) const { return part_infos_.at(idx); }

private:
  common::ObArrayWrap<ObExternalTablePartInfo> part_infos_;
  common::ObIAllocator &allocator_;
};
}
}
#endif /* _OB_EXTERNAL_TABLE_PART_INFO_H */
