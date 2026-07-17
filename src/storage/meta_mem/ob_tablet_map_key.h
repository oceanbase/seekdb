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

#ifndef OCEANBASE_STORAGE_OB_TABLET_MAP_KEY
#define OCEANBASE_STORAGE_OB_TABLET_MAP_KEY

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace storage
{
class ObTabletMapKey final
{
public:
  ObTabletMapKey();
  explicit ObTabletMapKey(const common::ObTabletID &tablet_id);
  ~ObTabletMapKey();

  void reset();
  bool is_valid() const;

  bool operator ==(const ObTabletMapKey &other) const;
  bool operator !=(const ObTabletMapKey &other) const;
  bool operator <(const ObTabletMapKey &other) const;
  int hash(uint64_t &hash_val) const;
  uint64_t hash() const;

  TO_STRING_KV(K_(tablet_id));
public:
  common::ObTabletID tablet_id_;
};

inline bool ObTabletMapKey::is_valid() const
{
  return tablet_id_.is_valid();
}

inline bool ObTabletMapKey::operator ==(const ObTabletMapKey &other) const
{
  return tablet_id_ == other.tablet_id_;
}

inline bool ObTabletMapKey::operator !=(const ObTabletMapKey &other) const
{
  return !(*this == other);
}

inline bool ObTabletMapKey::operator <(const ObTabletMapKey &other) const
{
  return tablet_id_ < other.tablet_id_;
}

class ObDieingTabletMapKey final
{
public:
  ObDieingTabletMapKey();
  explicit ObDieingTabletMapKey(const uint64_t tablet_id);
  explicit ObDieingTabletMapKey(const ObTabletMapKey &tablet_map_key);
  ~ObDieingTabletMapKey();

  void reset();
  bool is_valid() const;

  bool operator ==(const ObDieingTabletMapKey &other) const;
  bool operator !=(const ObDieingTabletMapKey &other) const;
  bool operator <(const ObDieingTabletMapKey &other) const;
  int hash(uint64_t &hash_val) const;
  uint64_t hash() const;

  TO_STRING_KV(K_(tablet_id));
private:
  uint64_t tablet_id_;
};

inline bool ObDieingTabletMapKey::is_valid() const
{
  return ObTabletID::INVALID_TABLET_ID != tablet_id_;
}

inline bool ObDieingTabletMapKey::operator ==(const ObDieingTabletMapKey &other) const
{
  return tablet_id_ == other.tablet_id_;
}

inline bool ObDieingTabletMapKey::operator !=(const ObDieingTabletMapKey &other) const
{
  return !(*this == other);
}

inline bool ObDieingTabletMapKey::operator <(const ObDieingTabletMapKey &other) const
{
  return tablet_id_ < other.tablet_id_;
}

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_MAP_KEY
