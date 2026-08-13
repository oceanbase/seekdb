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
#ifndef OB_STORAGE_TRUNCATE_INFO_ARRAY_H_
#define OB_STORAGE_TRUNCATE_INFO_ARRAY_H_
#include "lib/container/ob_se_array.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{
struct MdsDumpKey;
struct MdsDumpNode;
}
struct ObTruncateInfo;

enum ObTruncateInfoSrc : uint8_t
{
  TRUN_SRC_KV_CACHE = 0,
  TRUN_SRC_MDS = 1,
  TRUN_SRC_MAX
};

struct ObTruncateInfoArray
{
public:
  ObTruncateInfoArray();
  ~ObTruncateInfoArray();
  int init_for_first_creation(common::ObIAllocator &allocator);
  int init_with_kv_cache_array(common::ObIAllocator &allocator, const ObArrayWrap<ObTruncateInfo> &truncate_info_array);
  void reset();
  OB_INLINE int64_t count() const { return truncate_info_array_.count(); }
  OB_INLINE bool empty() const { return truncate_info_array_.empty(); }
  OB_INLINE bool is_valid() const { return is_inited_ && inner_is_valid(); }
  const ObTruncateInfo *at(const int64_t idx) const
  {
    OB_ASSERT(idx >= 0 && idx < count());
    return truncate_info_array_.at(idx);
  }
  ObTruncateInfo *at(const int64_t idx)
  {
    OB_ASSERT(idx >= 0 && idx < count());
    return truncate_info_array_.at(idx);
  }
  OB_INLINE ObIArray<ObTruncateInfo *> &get_array() { return truncate_info_array_; }
  OB_INLINE const ObIArray<ObTruncateInfo *> &get_array() const { return truncate_info_array_; }
  OB_INLINE ObTruncateInfoSrc get_src() const { return src_; }
  int append_with_deep_copy(const ObTruncateInfo &truncate_info); // will deep copy with allocator
  int append_ptr(ObTruncateInfo &truncate_info); // will append ptr only
  // serialize & deserialize
  static bool compare(const ObTruncateInfo *lhs, const ObTruncateInfo *rhs);
  TO_STRING_KV(K_(is_inited), "array_cnt", count(), K_(src), K_(truncate_info_array));
private:
  bool inner_is_valid() const { return 0 == count() || (count() >= 0 && allocator_ != nullptr); }
  void reset_list();
  int inner_append_and_sort(ObTruncateInfo &info);
  ObSEArray<ObTruncateInfo *, 1> truncate_info_array_;
  common::ObIAllocator *allocator_;
  ObTruncateInfoSrc src_;
  bool is_inited_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObTruncateInfoArray);
};

} // namespace strorage
} // namespace oceanbase

#endif // OB_STORAGE_TRUNCATE_INFO_ARRAY_H_
