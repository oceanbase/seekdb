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

#include "lib/oblog/ob_log_module.h"
#include "sql/engine/join/hash_join/ob_hash_join_struct.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{

class ObJoinFilterStoreRow : public ObHJStoredRow
{
public:
  inline uint64_t get_join_filter_hash_value(const RowMeta &row_meta, uint16_t hash_id) const;
  inline void set_join_filter_hash_value(const RowMeta &row_meta, uint16_t hash_id,
                                         uint64_t hash_value);
};

inline uint64_t ObJoinFilterStoreRow::get_join_filter_hash_value(const RowMeta &row_meta,
                                                                 uint16_t hash_id) const
{
  return (reinterpret_cast<uint64_t *>(get_extra_payload(row_meta)))[hash_id];
}

inline void ObJoinFilterStoreRow::set_join_filter_hash_value(const RowMeta &row_meta,
                                                             uint16_t hash_id, uint64_t hash_value)
{
  (reinterpret_cast<uint64_t *>(get_extra_payload(row_meta)))[hash_id] = hash_value;
}

} // namespace sql

} // namespace oceanbase
