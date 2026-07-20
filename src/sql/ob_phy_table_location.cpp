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

#define USING_LOG_PREFIX SQL

#include "ob_phy_table_location.h"
#include "sql/optimizer/ob_phy_table_location_info.h"
using namespace oceanbase::common;
using namespace oceanbase::share;
namespace oceanbase
{
namespace sql
{



OB_DEF_SERIALIZE(ObPhyTableLocation)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              table_location_key_,
              ref_table_id_,
              duplicate_type_);
  return ret;
}

OB_DEF_DESERIALIZE(ObPhyTableLocation)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              table_location_key_,
              ref_table_id_,
              duplicate_type_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObPhyTableLocation)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              table_location_key_,
              ref_table_id_,
              duplicate_type_);
  return len;
}

ObPhyTableLocation::ObPhyTableLocation()
  : table_location_key_(OB_INVALID_ID),
    ref_table_id_(OB_INVALID_ID),
    duplicate_type_(ObDuplicateType::NOT_DUPLICATE)
{
}

void ObPhyTableLocation::reset()
{
  table_location_key_ = OB_INVALID_ID;
  ref_table_id_ = OB_INVALID_ID;
  duplicate_type_ = ObDuplicateType::NOT_DUPLICATE;
}

int ObPhyTableLocation::assign(const ObPhyTableLocation &other)
{
  int ret = OB_SUCCESS;
  table_location_key_ = other.table_location_key_;
  ref_table_id_ = other.ref_table_id_;
  duplicate_type_ = other.duplicate_type_;
  return ret;
}



int ObPhyTableLocation::assign_from_phy_table_loc_info(const ObCandiTableLoc &other)
{
  int ret = OB_SUCCESS;
  table_location_key_ = other.get_table_location_key();
  ref_table_id_ = other.get_ref_table_id();
  set_duplicate_type(other.get_duplicate_type());
  return ret;
}
}/* ns sql*/
}/* ns oceanbase */
