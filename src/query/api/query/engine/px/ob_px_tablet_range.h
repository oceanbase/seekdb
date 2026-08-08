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

#ifndef OCEANBASE_QUERY_ENGINE_PX_OB_PX_TABLET_RANGE_H_
#define OCEANBASE_QUERY_ENGINE_PX_OB_PX_TABLET_RANGE_H_

#include "query/ob_sql_define.h"
#include "lib/oblog/ob_log.h"

namespace oceanbase
{
namespace sql
{

// Shared PX range vocabulary.  Rootserver consumes this value type while SQL
// owns the execution implementation, so the declaration belongs to Query API.
class ObPxTabletRange final
{
  OB_UNIS_VERSION(1);
public:
  ObPxTabletRange();
  ~ObPxTabletRange() = default;
  void reset();
  bool is_valid() const;
  template <bool use_allocator>
  int deep_copy_from(const ObPxTabletRange &other, common::ObIAllocator &allocator,
                     char *buf, int64_t size, int64_t &pos);
  int assign(const ObPxTabletRange &other);
  int64_t get_range_col_cnt() const
  {
    return range_cut_.empty() ? 0 : range_cut_.at(0).count();
  }
  TO_STRING_KV(K_(tablet_id), K_(range_cut), K_(range_weights));
public:
  static const int64_t DEFAULT_RANGE_COUNT = 8;
  typedef common::ObSEArray<common::ObRowkey, DEFAULT_RANGE_COUNT> EndKeys;
  typedef ObTMSegmentArray<ObDatum> DatumKey;
  typedef ObSEArray<int64_t, 2> RangeWeight;
  typedef ObTMArray<DatumKey> RangeCut;
  typedef ObSEArray<RangeWeight, DEFAULT_RANGE_COUNT> RangeWeights;

  int64_t tablet_id_;
  int64_t range_weights_;
  RangeCut range_cut_;
};

template <bool use_allocator>
int ObPxTabletRange::deep_copy_from(const ObPxTabletRange &other,
                                    common::ObIAllocator &allocator,
                                    char *buf,
                                    int64_t size,
                                    int64_t &pos)
{
  int ret = OB_SUCCESS;
  reset();
  tablet_id_ = other.tablet_id_;
  range_weights_ = other.range_weights_;
  if (OB_FAIL(range_cut_.reserve(other.range_cut_.count()))) {
    SQL_LOG(WARN, "reserve end keys failed", K(ret), K(other.range_cut_.count()));
  }
  DatumKey copied_key;
  RangeWeight range_weight;
  ObDatum tmp_datum;
  for (int64_t i = 0; OB_SUCC(ret) && i < other.range_cut_.count(); ++i) {
    const DatumKey &cur_key = other.range_cut_.at(i);
    copied_key.reuse();
    range_weight.reuse();
    for (int64_t j = 0; OB_SUCC(ret) && j < cur_key.count(); ++j) {
      if (use_allocator && OB_FAIL(tmp_datum.deep_copy(cur_key.at(j), allocator))) {
        SQL_LOG(WARN, "deep copy datum failed", K(ret), K(i), K(j), K(cur_key.at(j)));
      } else if (!use_allocator && OB_FAIL(tmp_datum.deep_copy(cur_key.at(j), buf, size, pos))) {
        SQL_LOG(WARN, "deep copy datum failed", K(ret), K(i), K(j), K(cur_key.at(j)), K(size), K(pos));
      } else if (OB_FAIL(copied_key.push_back(tmp_datum))) {
        SQL_LOG(WARN, "push back datum failed", K(ret), K(i), K(j), K(tmp_datum));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(range_cut_.push_back(copied_key))) {
      SQL_LOG(WARN, "push back rowkey failed", K(ret), K(copied_key), K(i));
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_ENGINE_PX_OB_PX_TABLET_RANGE_H_
