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
#pragma once

#include "storage/blocksstable/ob_datum_range.h"
#include "storage/direct_load/ob_direct_load_multiple_datum_rowkey.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadMultipleDatumRange
{
public:
  ObDirectLoadMultipleDatumRange();
  ObDirectLoadMultipleDatumRange(const ObDirectLoadMultipleDatumRange &other) = delete;
  ~ObDirectLoadMultipleDatumRange();
  void reset();
  int64_t get_deep_copy_size() const;
  int assign(const ObDirectLoadMultipleDatumRange &other);
  int assign(const common::ObTabletID &tablet_id, const blocksstable::ObDatumRange &range);
  OB_INLINE bool is_valid() const { return start_key_.is_valid() && end_key_.is_valid(); }
  void set_whole_range();
  OB_INLINE bool is_left_open() const { return !border_flag_.inclusive_start(); }
  OB_INLINE bool is_left_closed() const { return border_flag_.inclusive_start(); }
  OB_INLINE bool is_right_open() const { return !border_flag_.inclusive_end(); }
  OB_INLINE bool is_right_closed() const { return border_flag_.inclusive_end(); }
  OB_INLINE void set_left_open() { border_flag_.unset_inclusive_start(); }
  OB_INLINE void set_left_closed() { border_flag_.set_inclusive_start(); }
  OB_INLINE void set_right_open() { border_flag_.unset_inclusive_end(); }
  OB_INLINE void set_right_closed() { border_flag_.set_inclusive_end(); }
  TO_STRING_KV(K_(start_key), K_(end_key), K_(border_flag));
public:
  ObDirectLoadMultipleDatumRowkey start_key_;
  ObDirectLoadMultipleDatumRowkey end_key_;
  common::ObBorderFlag border_flag_;
};

} // namespace storage
} // namespace oceanbase
