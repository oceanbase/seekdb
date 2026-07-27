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

#ifndef OCEANBASE_SHARE_OB_LS_ID
#define OCEANBASE_SHARE_OB_LS_ID

#include <stdint.h>
#include "lib/ob_define.h"
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace share
{

// Identifier for the sole local log stream.
class ObLSID
{
public:
  static const int64_t INVALID_LS_ID = -1;
  static const int64_t SYS_LS_ID = 1;

  ObLSID() : id_(INVALID_LS_ID) {}
  ObLSID(const ObLSID &other) : id_(other.id_) {}
  explicit ObLSID(const int64_t id) : id_(id) {}
  ~ObLSID() { reset(); }

  int64_t id() const { return id_; }
  void reset() { id_ = INVALID_LS_ID; }

  ObLSID &operator=(const int64_t id) { id_ = id; return *this; }
  ObLSID &operator=(const ObLSID &other) { id_ = other.id_; return *this; }

  bool is_valid() const { return SYS_LS_ID == id_; }

  bool operator==(const ObLSID &other) const { return id_ == other.id_; }
  bool operator>(const ObLSID &other) const { return id_ > other.id_; }
  bool operator>=(const ObLSID &other) const { return id_ >= other.id_; }
  bool operator!=(const ObLSID &other) const { return id_ != other.id_; }
  bool operator<(const ObLSID &other) const { return id_ < other.id_; }
  bool operator<=(const ObLSID &other) const { return id_ <= other.id_; }
  int compare(const ObLSID &other) const
  {
    return id_ == other.id_ ? 0 : (id_ < other.id_ ? -1 : 1);
  }

  uint64_t hash() const;
  int hash(uint64_t &hash_val) const;
  NEED_SERIALIZE_AND_DESERIALIZE;
  TO_STRING_KV(K_(id));

private:
  int64_t id_;
};

static const ObLSID SYS_LS(ObLSID::SYS_LS_ID);

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_LS_ID
