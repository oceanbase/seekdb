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

#ifndef OB_STORAGE_COMPACTION_I_COMPACTION_FILTER_H_
#define OB_STORAGE_COMPACTION_I_COMPACTION_FILTER_H_

#include "lib/utility/ob_print_utils.h"
#include "share/schema/ob_table_param.h"
namespace oceanbase
{
namespace blocksstable
{
struct ObDatumRow;
}
namespace compaction
{

class ObICompactionFilter
{
public:
  ObICompactionFilter()
  {
  }

  virtual ~ObICompactionFilter() {}
  // for statistics
  enum ObFilterRet
  {
    FILTER_RET_NOT_CHANGE = 0,
    FILTER_RET_REMOVE = 1,
    FILTER_RET_MAX = 2,
  };
  const static char *ObFilterRetStr[];
  const static char *get_filter_ret_str(const int64_t idx);
  static bool is_valid_filter_ret(const ObFilterRet filter_ret);

  struct ObFilterStatistics
  {
    ObFilterStatistics()
    {
      MEMSET(row_cnt_, 0, sizeof(row_cnt_));
    }
    ~ObFilterStatistics() {}
    void add(const ObFilterStatistics &other);
    void inc(ObFilterRet filter_ret);
    int64_t to_string(char *buf, const int64_t buf_len) const;
    void gene_info(char* buf, const int64_t buf_len, int64_t &pos) const;
    int64_t row_cnt_[FILTER_RET_MAX];
  };

  enum CompactionFilterType : uint8_t
  {
    TX_DATA_MINOR,
    MDS_MINOR_FILTER_DATA,
    MDS_MINOR_CROSS_LS,
    MDS_IN_MEDIUM_INFO,
    FILTER_TYPE_MAX
  };
  const static char *ObFilterTypeStr[];
  const static char *get_filter_type_str(const int64_t idx);

  // need be thread safe
  virtual int filter(
      const blocksstable::ObDatumRow &row,
      ObFilterRet &filter_ret) = 0;
  virtual CompactionFilterType get_filter_type() const = 0;

  VIRTUAL_TO_STRING_KV("filter_type", get_filter_type_str(get_filter_type()));
};

struct ObCompactionFilterFactory final
{
public:
  template <typename T, typename... Args>
  static int alloc_compaction_filter(
    common::ObIAllocator &allocator,
    ObICompactionFilter *&compaction_filter,
    Args&... args)
  {
    compaction_filter = nullptr;
    int ret = OB_SUCCESS;
    void *buf = nullptr;
    T *new_filter = nullptr;
    if (OB_ISNULL(buf = allocator.alloc(sizeof(T)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      STORAGE_LOG(WARN, "failed to alloc memory", K(ret));
    } else {
      new_filter = new (buf) T();
      if (OB_FAIL(new_filter->init(args...))) {
        STORAGE_LOG(WARN, "failed to init filter", K(ret));
        allocator.free(new_filter);
        new_filter = nullptr;
      } else {
        compaction_filter = new_filter;
      }
    }
    return ret;
  }
};

} // namespace compaction
} // namespace oceanbase

#endif // OB_STORAGE_COMPACTION_I_COMPACTION_FILTER_H_
