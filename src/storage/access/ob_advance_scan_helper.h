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

#ifndef OB_STORAGE_ACCESS_OB_ADVANCE_SCAN_HELPER_H_
#define OB_STORAGE_ACCESS_OB_ADVANCE_SCAN_HELPER_H_

#include "storage/blocksstable/ob_datum_range.h"
#include "storage/blocksstable/ob_datum_row.h"

namespace oceanbase
{
namespace blocksstable
{
class ObIMicroBlockRowScanner;
class ObIndexBlockRowScanner;
struct ObMicroIndexInfo;
}

namespace storage
{
class ObITableReadInfo;
class ObAdvanceScanHelperFactory;
enum class ObAdvanceScanNodeState : int8_t
{
  INVALID_STATE = 0,
  MAY_OVERLAP_RANGE = 1,
  BEFORE_RANGE = 2,
};

struct ObAdvanceScanState
{
  ObAdvanceScanState() : state_(static_cast<int8_t>(ObAdvanceScanNodeState::INVALID_STATE)) {}
  OB_INLINE void reset()
  {
    state_ = static_cast<int8_t>(ObAdvanceScanNodeState::INVALID_STATE);
  }
  OB_INLINE bool is_invalid() const
  {
    return ObAdvanceScanNodeState::INVALID_STATE == static_cast<ObAdvanceScanNodeState>(state_);
  }
  OB_INLINE bool is_before_range() const
  {
    return ObAdvanceScanNodeState::BEFORE_RANGE == static_cast<ObAdvanceScanNodeState>(state_);
  }
  OB_INLINE void set_state(const ObAdvanceScanNodeState state)
  {
    state_ = static_cast<int8_t>(state);
  }
  TO_STRING_KV(K_(state));
  int8_t state_;
};

class ObAdvanceScanHelper
{
public:
  ObAdvanceScanHelper(const blocksstable::ObStorageDatumUtils &datum_utils);
  ~ObAdvanceScanHelper();
  void reset();
  int init(
      const bool is_reverse_scan,
      const blocksstable::ObDatumRange &scan_range,
      const ObITableReadInfo &read_info,
      common::ObIAllocator &stmt_allocator);
  int switch_info(
      const bool is_reverse_scan,
      const blocksstable::ObDatumRange &scan_range,
      const ObITableReadInfo &read_info,
      common::ObIAllocator &stmt_allocator);
  int advance_scan(const blocksstable::ObDatumRange &scan_range);
  int filter_index_node(
      blocksstable::ObMicroIndexInfo &index_info,
      ObAdvanceScanState &prev_state,
      ObAdvanceScanState &state);
  int seek_to_range(
      blocksstable::ObIMicroBlockRowScanner &micro_scanner,
      blocksstable::ObMicroIndexInfo &index_info,
      const bool first = false);
  common::ObIAllocator *get_stmt_alloc()
  {
    return stmt_alloc_;
  }
  bool needs_range_seek() const
  {
    return !left_border_reached_;
  }
  TO_STRING_KV(K_(is_inited),
               K_(left_border_reached),
               K_(micro_start),
               K_(micro_last),
               K_(micro_current),
               KP_(range_datums),
               KP_(read_info),
               KP_(stmt_alloc),
               K_(left_border_reached));
private:
  common::ObArenaAllocator range_alloc_;
  bool is_inited_;
  bool left_border_reached_;
  int64_t micro_start_;
  int64_t micro_last_;
  int64_t micro_current_;
  const blocksstable::ObStorageDatumUtils &datum_utils_;
  blocksstable::ObDatumRange complete_range_;
  blocksstable::ObStorageDatum *range_datums_;
  const ObITableReadInfo *read_info_;
  common::ObIAllocator *stmt_alloc_;
};

class ObAdvanceScanHelperFactory
{
public:
  static int build_advance_scan_helper(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      const blocksstable::ObDatumRange *range,
      ObAdvanceScanHelper *&advance_scan_helper);
  static void destroy_advance_scan_helper(ObAdvanceScanHelper *&advance_scan_helper);
};

} // namespace storage
} // namespace oceanbase

#endif // OB_STORAGE_ACCESS_OB_ADVANCE_SCAN_HELPER_H_
