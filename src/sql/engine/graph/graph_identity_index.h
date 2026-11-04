/*
 * Copyright (c) 2026 OceanBase.
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

#include "lib/container/ob_array.h"
#include "sql/engine/graph/graph_path_spec.h"

namespace oceanbase
{
namespace sql
{
// Query-local hash index over typed graph identities. Compatible NUMBER and
// DECIMAL_INT keys are normalized, while tolerance-compared DOUBLE keys use a
// type bucket, so bucket selection follows graph endpoint equality. Identities
// are shallow views: the caller must keep their rowkey backing alive until
// reset(). An optional binding ID separates correlation domains without
// changing element identity. Both arrays use the owner's graph work area.
class GraphIdentityIndex final
{
public:
  explicit GraphIdentityIndex(common::ObIAllocator &allocator);
  ~GraphIdentityIndex() = default;

  int init(int64_t expected_count);
  int get_or_insert(const GraphElementIdentity &identity,
                    int64_t &entry_index,
                    bool &inserted,
                    int64_t binding_id = 0);
  int find(const GraphElementIdentity &identity,
           int64_t &entry_index,
           bool &found,
           int64_t binding_id = 0) const;
  void reset();
  int64_t count() const { return entries_.count(); }
  static int64_t entry_size() { return sizeof(Entry); }
  int64_t used_memory() const
  {
    return bucket_heads_.get_data_size()
               > INT64_MAX - entries_.get_data_size()
        ? INT64_MAX
        : bucket_heads_.get_data_size() + entries_.get_data_size();
  }

private:
  struct Entry
  {
    GraphElementIdentity identity_{};
    // Optional correlation domain; adjacency-only users keep the default zero.
    int64_t binding_id_{0};
    int64_t next_entry_{-1};
    uint64_t hash_{0};
    TO_STRING_KV(K_(identity), K_(next_entry));
  };

  int calc_compatible_hash(const GraphElementIdentity &identity,
                           uint64_t &hash) const;
  int get_hash(const GraphElementIdentity &identity,
               uint64_t &hash, int64_t binding_id) const;
  int find_in_bucket(const GraphElementIdentity &identity, uint64_t hash,
                     int64_t binding_id, int64_t &entry_index, bool &found) const;
  int grow_buckets();

private:
  common::ObArray<int64_t> bucket_heads_;
  common::ObArray<Entry> entries_;

  DISALLOW_COPY_AND_ASSIGN(GraphIdentityIndex);
};


} // namespace sql
} // namespace oceanbase
