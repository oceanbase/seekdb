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
#include "common/datum/datum.h"
#include "common/datum/datum_copy_plan.h"

namespace oceanbase
{
namespace sql
{

// One value, with a declared physical layout or an explicitly untyped old
// storage-key byte span. Opaque bytes never select a SQL scalar getter. The
// old descriptor exists only at synchronous codec/comparator boundaries.
class ObPxRangeCell final
{
public:
  ObPxRangeCell() : value_(), meta_(), stored_pack_(0), opaque_(true)
  { common::ObDatumDesc desc; desc.set_null(); stored_pack_ = desc.pack_; }
  const common::Datum &get_datum() const { OB_ASSERT(!opaque_); return value_; }
  const common::SqlTypeMeta &get_meta() const { OB_ASSERT(!opaque_); return meta_; }
  bool is_opaque() const { return opaque_; }
  bool is_null() const
  { common::ObDatumDesc desc; desc.pack_ = stored_pack_; return opaque_ ? desc.is_null() : value_.is_null(); }
  int assign_stored_key_borrowed(const common::ObDatum &source);
  int assign_stored_key_owned(const common::ObDatum &source, common::ObIAllocator &owner);
  int assign_expr_owned(const common::Datum &source, const common::DatumCopyPlan &plan,
                        common::ObIAllocator &owner);
  // Only the synchronous routing-key caller may borrow materialized scratch.
  int assign_expr_view(const common::Datum &source, const common::DatumCopyPlan &plan,
                       void *scratch, int64_t capacity);
  int bind_for_execution(const common::DatumCopyPlan &plan, common::ObIAllocator &owner);
  int export_stored_key(common::ObDatum &output, void *scratch, int64_t capacity) const;
  int payload_size(int64_t &bytes) const;
  int deep_copy(const ObPxRangeCell &source, common::ObIAllocator &owner);
  int deep_copy(const ObPxRangeCell &source, char *buffer, int64_t capacity, int64_t &position);
  // Delegate the entire old ObDatum envelope, without adding a cell header.
  int serialize(char *buf, int64_t length, int64_t &pos) const;
  int deserialize(const char *buf, int64_t length, int64_t &pos);
  int64_t get_serialize_size() const;
  TO_STRING_KV(K_(value), K_(meta), K_(opaque), K_(stored_pack));
private:
  common::Datum value_;
  common::SqlTypeMeta meta_;
  uint32_t stored_pack_; // Old format metadata only, not a second value.
  bool opaque_;
};

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
  typedef ObTMSegmentArray<ObPxRangeCell> DatumKey;
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
  }
  DatumKey copied_key;
  RangeWeight range_weight;
  ObPxRangeCell tmp_datum;
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
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(range_cut_.push_back(copied_key))) {
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_ENGINE_PX_OB_PX_TABLET_RANGE_H_
