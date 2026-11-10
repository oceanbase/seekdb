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

#include "common/datum/datum.h"
#include "common/datum/datum_value.h"
#include "common/rowkey/ob_store_rowkey.h"
#include "data_plane/access/ob_table_scan_param.h"
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "lib/container/ob_array_helper.h"

namespace oceanbase
{
namespace sql
{

// Execution-owned input for exactly three signed BIGINT primary-key columns.
// The producer has already bound/cast their types. This specialized non-NULL
// layout stores exactly int64_t[3] inline, without per-cell state or pointers.
// Storage and Memtable sidecars borrow those members without copying their
// payloads. Construct in a stable arena and keep it alive until the scan
// iterator is reused/released. Never write through its legacy storage views.
// Only the fixed BIGINT type is shared; all values and views below belong to
// this execution owner and cannot be shared between active scans.
class NativePointGetInput final
{
public:
  static constexpr int64_t COLUMN_COUNT = 3;

  NativePointGetInput();
  NativePointGetInput(const NativePointGetInput &) = delete;
  NativePointGetInput &operator=(const NativePointGetInput &) = delete;
  NativePointGetInput(NativePointGetInput &&) = delete;
  NativePointGetInput &operator=(NativePointGetInput &&) = delete;

  // Reinitialization invalidates all old views and requires their consumers
  // to have stopped first. The caller has checked type and NULL state.
  // Preparation failures leave a published key unchanged.
  int init(const int64_t (&values)[COLUMN_COUNT], int64_t group_id);
  const common::ObIArray<storage::DatumLookupKey> &lookup_keys() const { return lookup_keys_; }
  // A 24-byte fixed BIGINT layout, not an array of generic execution Datums.
  const int64_t *values() const { return values_; }

private:
  alignas(8) int64_t values_[COLUMN_COUNT];
  blocksstable::ObStorageDatum storage_datums_[COLUMN_COUNT];
  common::DatumValue legacy_values_[COLUMN_COUNT];
  common::ObStoreRowkey cached_rowkey_;
  storage::DatumLookupKey key_;
  common::ObArrayHelper<storage::DatumLookupKey> lookup_keys_;
};

} // namespace sql
} // namespace oceanbase
