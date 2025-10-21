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

#ifndef OB_STORAGE_TABLE_SCAN_RANGE_H
#define OB_STORAGE_TABLE_SCAN_RANGE_H

#include "common/ob_common_types.h"
#include "share/ob_simple_batch.h"
#include "storage/blocksstable/ob_datum_range.h"
#include "src/storage/access/ob_store_row_iterator.h"

namespace oceanbase
{
namespace storage
{
class ObTableScanParam;

struct ObTableScanRange
{
public:
  ObTableScanRange();
  ~ObTableScanRange() { reset(); }
  int init(ObTableScanParam &scan_param, const ObTablet &tablet, const bool is_tablet_spliting);
  int init(ObTableScanParam &scan_param,
           const common::ObSimpleBatch &simple_batch,
           common::ObIAllocator &allocator);
  void reset();
  OB_INLINE bool is_valid() const { return is_inited_; }
  OB_INLINE bool is_get() const { return GET == status_; }
  OB_INLINE bool is_scan() const { return SCAN == status_; }
  OB_INLINE bool is_empty() const { return EMPTY == status_; }
  OB_INLINE void set_empty() { status_ = EMPTY; }
  OB_INLINE const ObIArray<blocksstable::ObDatumRange> &get_ranges() const { return ranges_; }
  OB_INLINE const ObIArray<blocksstable::ObDatumRange> &get_suffix_ranges() const { return skip_scan_ranges_; }
  OB_INLINE const ObIArray<blocksstable::ObDatumRowkey> &get_rowkeys() const { return rowkeys_; }
  int get_query_iter_type(ObQRIterType &iter_type) const;
  TO_STRING_KV(K_(rowkeys), K_(ranges), K_(status), K_(is_inited));
private:
  int init_rowkeys(
      const ObTablet &tablet,
      const bool is_tablet_spliting,
      const common::ObIArray<common::ObNewRange> &ranges,
      const common::ObQueryFlag &scan_flag,
      const blocksstable::ObStorageDatumUtils *datum_utils);
  int init_ranges(
      const ObTablet &tablet,
      const bool is_tablet_spliting,
      const common::ObIArray<common::ObNewRange> &ranges,
      const common::ObQueryFlag &scan_flag,
      const blocksstable::ObStorageDatumUtils *datum_utils);
  int init_ranges_in_skip_scan(
      const ObTablet &tablet,
      const bool is_tablet_spliting,
      const common::ObIArray<common::ObNewRange> &ranges,
      const common::ObIArray<common::ObNewRange> &skip_scan_ranges,
      const common::ObQueryFlag &scan_flag,
      const blocksstable::ObStorageDatumUtils *datum_utils);
  int always_false(
      const common::ObNewRange &range, 
      bool &is_false);
  int get_split_partition_rowkeys(
      const ObTablet &tablet_handle,
      const common::ObIArray<common::ObNewRange> &ranges,
      const blocksstable::ObStorageDatumUtils *datum_utils);
private:
  struct ObSkipScanWrappedRange
  {
    ObSkipScanWrappedRange() = default;
    ObSkipScanWrappedRange(const blocksstable::ObDatumRange &datum_range, blocksstable::ObDatumRange &datum_skip_range)
      : datum_range_(datum_range), datum_skip_range_(datum_skip_range)
    {}
    OB_INLINE int compare(const ObSkipScanWrappedRange &rhs, const blocksstable::ObStorageDatumUtils &datum_utils, int &cmp_ret) const
    {
      return datum_range_.compare(rhs.datum_range_, datum_utils, cmp_ret);
    }
    TO_STRING_KV(K_(datum_range), K_(datum_skip_range));
    blocksstable::ObDatumRange datum_range_;
    blocksstable::ObDatumRange datum_skip_range_;
  };
  enum RangeStatus
  {
    EMPTY,
    GET,
    SCAN,
  };
  static const int64_t DEFAULT_RANGE_CNT = 8;
  common::ObSEArray<blocksstable::ObDatumRowkey, DEFAULT_RANGE_CNT> rowkeys_;
  common::ObSEArray<blocksstable::ObDatumRange, DEFAULT_RANGE_CNT> ranges_;
  common::ObSEArray<blocksstable::ObDatumRange, DEFAULT_RANGE_CNT> skip_scan_ranges_;
  ObIAllocator *allocator_;
  RangeStatus status_;
  bool is_inited_;
  DISALLOW_COPY_AND_ASSIGN(ObTableScanRange);
};


} // namespace storage
} // namespace oceanbase
#endif
