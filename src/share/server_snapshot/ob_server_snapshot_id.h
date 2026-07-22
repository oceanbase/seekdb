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


#ifndef OCEANBASE_SHARE_OB_SERVER_SNAPSHOT_ID_H_
#define OCEANBASE_SHARE_OB_SERVER_SNAPSHOT_ID_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"     // TO_STRING_KV

namespace oceanbase
{
namespace share
{
class ObServerSnapshotID final
{
public:
  static const int64_t OB_INVALID_SNAPSHOT_ID = -1;

public:
  ObServerSnapshotID() : id_(OB_INVALID_SNAPSHOT_ID) {}
  ObServerSnapshotID(const ObServerSnapshotID &other) : id_(other.id_) {}
  explicit ObServerSnapshotID(const int64_t id) : id_(id) {}
  ~ObServerSnapshotID() { reset(); }

public:
  int64_t id() const { return id_; }
  void reset() { id_ = OB_INVALID_SNAPSHOT_ID; }
  bool is_valid() const { return id_ != OB_INVALID_SNAPSHOT_ID; }
  // assignment
  ObServerSnapshotID &operator=(const int64_t id) { id_ = id; return *this; }
  ObServerSnapshotID &operator=(const ObServerSnapshotID &other) { id_ = other.id_; return *this; }

  // compare operator
  bool operator == (const ObServerSnapshotID &other) const { return id_ == other.id_; }
  bool operator >  (const ObServerSnapshotID &other) const { return id_ > other.id_; }
  bool operator != (const ObServerSnapshotID &other) const { return id_ != other.id_; }
  bool operator <  (const ObServerSnapshotID &other) const { return id_ < other.id_; }
  int compare(const ObServerSnapshotID &other) const
  {
    if (id_ == other.id_) {
      return 0;
    } else if (id_ < other.id_) {
      return -1;
    } else {
      return 1;
    }
  }

  uint64_t hash() const
  {
    OB_ASSERT(id_ != UINT64_MAX);
    return id_;
  }

  int hash(uint64_t &hash_val) const
  {
    int ret = OB_SUCCESS;
    hash_val = hash();
    return ret;
  }

  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(id));

private:
  int64_t id_;
};

} // end namespace share
} // end namespace oceanbase

#endif // OCEANBASE_SHARE_OB_SERVER_SNAPSHOT_ID_H_
