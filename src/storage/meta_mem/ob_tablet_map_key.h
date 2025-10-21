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
#include "share/ob_ls_id.h"
#include "common/ob_tablet_id.h"

namespace oceanbase
{
namespace storage
{
class ObTabletMapKey final
{
public:
  ObTabletMapKey();
  ObTabletMapKey(const share::ObLSID &ls_id, const common::ObTabletID &tablet_id);
  ~ObTabletMapKey();

  void reset();
  bool is_valid() const;

  bool operator ==(const ObTabletMapKey &other) const;
  bool operator !=(const ObTabletMapKey &other) const;
  bool operator <(const ObTabletMapKey &other) const;
  int hash(uint64_t &hash_val) const;
  uint64_t hash() const;

  TO_STRING_KV(K_(ls_id), K_(tablet_id));
public:
  share::ObLSID ls_id_;
  common::ObTabletID tablet_id_;
};

inline bool ObTabletMapKey::is_valid() const
{
  return ls_id_.is_valid() && tablet_id_.is_valid();
}

inline bool ObTabletMapKey::operator ==(const ObTabletMapKey &other) const
{
  return ls_id_ == other.ls_id_ && tablet_id_ == other.tablet_id_;
}

inline bool ObTabletMapKey::operator !=(const ObTabletMapKey &other) const
{
  return !(*this == other);
}

inline bool ObTabletMapKey::operator <(const ObTabletMapKey &other) const
{
  return ls_id_ < other.ls_id_ && tablet_id_ < other.tablet_id_;
}

class ObDieingTabletMapKey final
{
public:
  ObDieingTabletMapKey();
  ObDieingTabletMapKey(const uint64_t tablet_id, const int64_t transfer_seq);
  ObDieingTabletMapKey(const ObTabletMapKey &tablet_map_key, const int64_t transfer_seq);
  ~ObDieingTabletMapKey();

  void reset();
  bool is_valid() const;

  bool operator ==(const ObDieingTabletMapKey &other) const;
  bool operator !=(const ObDieingTabletMapKey &other) const;
  bool operator <(const ObDieingTabletMapKey &other) const;
  int hash(uint64_t &hash_val) const;
  uint64_t hash() const;

  TO_STRING_KV(K_(tablet_id), K_(transfer_seq));
private:
  uint64_t tablet_id_;
  int64_t transfer_seq_;
};

inline bool ObDieingTabletMapKey::is_valid() const
{
  return ObTabletID::INVALID_TABLET_ID != tablet_id_ && transfer_seq_ >= 0;
}

inline bool ObDieingTabletMapKey::operator ==(const ObDieingTabletMapKey &other) const
{
  return tablet_id_ == other.tablet_id_ && transfer_seq_ == other.transfer_seq_;
}

inline bool ObDieingTabletMapKey::operator !=(const ObDieingTabletMapKey &other) const
{
  return !(*this == other);
}

inline bool ObDieingTabletMapKey::operator <(const ObDieingTabletMapKey &other) const
{
  return tablet_id_ < other.tablet_id_ && transfer_seq_ < other.transfer_seq_;
}

} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_MAP_KEY
