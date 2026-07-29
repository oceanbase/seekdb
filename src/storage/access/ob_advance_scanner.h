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

#ifndef OCEANBASE_STORAGE_OB_ADVANCE_SCANNER_H_
#define OCEANBASE_STORAGE_OB_ADVANCE_SCANNER_H_

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
class ObAdvanceScanFactory;
enum class ObAdvanceScanNodeState : int8_t
{
  INVALID_STATE = 0,
  PREFIX_UNCERTAIN = 1,
  // the following two state means endkey can be skipped
  // but as there is no startkey, the entire node cannot be determined,
  // will be determined later
  PREFIX_PENDDING_LEFT = 2,
  PREFIX_PENDDING_RIGHT = 3,
  // the following two state means the entire node can be skipped
  PREFIX_SKIPPED_LEFT = 4,
  PREFIX_SKIPPED_RIGHT = 5,
};

struct ObAdvanceScanState
{
  ObAdvanceScanState() : state_(0) {}
  OB_INLINE void reset()
  {
    state_ = 0;
  }
  OB_INLINE bool is_invalid() const
  {
    return ObAdvanceScanNodeState::INVALID_STATE == static_cast<ObAdvanceScanNodeState>(node_state_);
  }
  OB_INLINE bool is_skipped() const
  {
    return ObAdvanceScanNodeState::PREFIX_SKIPPED_LEFT == static_cast<ObAdvanceScanNodeState>(node_state_) ||
           ObAdvanceScanNodeState::PREFIX_SKIPPED_RIGHT == static_cast<ObAdvanceScanNodeState>(node_state_);
  }
  OB_INLINE int64_t range_idx() const
  {
    return range_idx_;
  }
  OB_INLINE void set_state(const int64_t range_idx, const ObAdvanceScanNodeState state)
  {
    range_idx_ = range_idx;
    node_state_ = static_cast<int8_t>(state);
  }
  OB_INLINE void inc_range_idx()
  {
    range_idx_++;
  }
  OB_INLINE void set_range_finished(const bool finish)
  {
    range_finished_ = finish;
  }
  OB_INLINE bool is_range_finished() const
  {
    return range_finished_;
  }
  OB_INLINE bool is_skipped_right() const
  {
    return ObAdvanceScanNodeState::PREFIX_SKIPPED_RIGHT == static_cast<ObAdvanceScanNodeState>(node_state_);
  }
  OB_INLINE bool is_skipped_left() const
  {
    return ObAdvanceScanNodeState::PREFIX_SKIPPED_LEFT == static_cast<ObAdvanceScanNodeState>(node_state_);
  }
  OB_INLINE bool is_pendding_right() const
  {
    return ObAdvanceScanNodeState::PREFIX_PENDDING_RIGHT == static_cast<ObAdvanceScanNodeState>(node_state_);
  }
  OB_INLINE bool is_pendding_left() const
  {
    return ObAdvanceScanNodeState::PREFIX_PENDDING_LEFT == static_cast<ObAdvanceScanNodeState>(node_state_);
  }
  TO_STRING_KV(K_(range_idx), K_(node_state), K_(range_finished), K_(state));
  union {
    struct {
      int64_t range_idx_: 32;
      int64_t node_state_ : 8;
      int64_t range_finished_: 1;
      int64_t reserved_: 23;
    };
    int64_t state_;
  };
};

class ObAdvanceScanner
{
public:
  ObAdvanceScanner(const blocksstable::ObStorageDatumUtils &datum_utils);
  ~ObAdvanceScanner();
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
  int skip(
      blocksstable::ObMicroIndexInfo &index_info,
      ObAdvanceScanState &prev_state,
      ObAdvanceScanState &state);
  int skip(
      blocksstable::ObIMicroBlockRowScanner &micro_scanner,
      blocksstable::ObMicroIndexInfo &index_info,
      const bool first = false);
  common::ObIAllocator *get_stmt_alloc()
  {
    return stmt_alloc_;
  }
  bool force_skip() const
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

class ObAdvanceScanFactory
{
public:
  static int build_advance_scanner(
      const ObTableIterParam &iter_param,
      ObTableAccessContext &access_ctx,
      const blocksstable::ObDatumRange *range,
      ObAdvanceScanner *&advance_scanner);
  static void destroy_advance_scanner(ObAdvanceScanner *&advance_scanner);
};

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_ADVANCE_SCANNER_H_
