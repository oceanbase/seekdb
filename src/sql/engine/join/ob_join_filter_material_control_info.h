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

#include "lib/container/ob_fixed_array.h"
#include <utility>

namespace oceanbase
{
namespace sql
{

struct ObJoinFilterMaterialControlInfo
{
  OB_UNIS_VERSION_V(1);
public:
  TO_STRING_KV(K(enable_material_), K(hash_id_), K(is_controller_), K(join_filter_count_),
               K(extra_hash_count_), K(each_sqc_has_full_data_));

public:
  // these variables for all join filter
  bool enable_material_{false};
  int16_t hash_id_{-1}; // mark the hash value position in compact row

  // these variables for the controller and hash join
  bool is_controller_{false}; // only the top join filter become the controller
  uint16_t join_filter_count_{0}; // total join filter count in the left side of a hash join
  uint16_t extra_hash_count_{0}; // hash value count(one for hash join, several for join filter)
  bool each_sqc_has_full_data_{false}; // mark whether each sqc has complete data
  bool need_sync_row_count_{false}; // if at least one join filter is shared join filter, we need to
                                    // send datahub msg to synchronize row count
};


}
}
