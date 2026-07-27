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

#ifndef OCEANBASE_SQL_OB_PHY_TABLE_LOCATION_
#define OCEANBASE_SQL_OB_PHY_TABLE_LOCATION_

#include "share/ob_define.h"

namespace oceanbase
{
namespace sql
{
class ObCandiTableLoc;
class ObCandiTabletLoc;


class ObPhyTableLocation final
{
  OB_UNIS_VERSION(1);
public:
public:
  ObPhyTableLocation();
  void reset();
  int assign(const ObPhyTableLocation &other);
  int assign_from_phy_table_loc_info(const ObCandiTableLoc &other);
  inline bool operator== (const ObPhyTableLocation &other) const
  {
    return table_location_key_ == other.table_location_key_ &&  ref_table_id_ == other.ref_table_id_;
  }

  inline void set_table_location_key(uint64_t table_location_key, uint64_t ref_table_id)
  {
    table_location_key_ = table_location_key;
    ref_table_id_ = ref_table_id;
  }
  inline uint64_t get_table_location_key() const { return table_location_key_; }
  inline uint64_t get_ref_table_id() const { return ref_table_id_; }

  TO_STRING_KV(K_(table_location_key), K_(ref_table_id));
private:
  /* Used for addressing location by table ID (possibly generated alias id) */
  uint64_t table_location_key_;
  /* Used to get the actual physical table ID */
  uint64_t ref_table_id_;
};

class ObPhyTableLocationGuard
{
public:
  ObPhyTableLocationGuard() : loc_(nullptr) {};
  ~ObPhyTableLocationGuard()
  {
    if (loc_) {
      loc_->~ObPhyTableLocation();
      loc_ = nullptr;
    }
  }
  int new_location(common::ObIAllocator &allocator)
  {
    int ret = common::OB_SUCCESS;
    void *buf = nullptr;
    if (OB_NOT_NULL(loc_)) {
      // init twice
      ret = common::OB_ERR_UNEXPECTED;
    } else if (nullptr == (buf = allocator.alloc(sizeof(ObPhyTableLocation)))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
    } else if (NULL == (loc_ = new(buf)ObPhyTableLocation())) {
      ret = common::OB_ERR_UNEXPECTED;
    }
    return ret;
  }
  // caller must ensure that the loc_ is not NULL before call get_loc()
  ObPhyTableLocation *get_loc() { return loc_; }
private:
  ObPhyTableLocation *loc_;
};

}
}
#endif /* OCEANBASE_SQL_OB_PHY_TABLE_LOCATION_ */
