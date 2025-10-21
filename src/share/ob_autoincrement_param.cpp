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

#include "share/ob_autoincrement_param.h"

namespace oceanbase
{
namespace share
{

OB_SERIALIZE_MEMBER(AutoincKey, tenant_id_, table_id_, column_id_);

OB_SERIALIZE_MEMBER(AutoincParam,
                    tenant_id_,
                    autoinc_table_id_,
                    autoinc_table_part_num_,
                    autoinc_col_id_,
                    autoinc_col_type_,
                    total_value_count_,
                    autoinc_desired_count_,
                    autoinc_old_value_index_,
                    autoinc_increment_,
                    autoinc_offset_,
                    autoinc_first_part_num_,
                    part_level_,
                    auto_increment_cache_size_,
                    part_value_no_order_,
                    autoinc_mode_is_order_,
                    autoinc_version_,
                    autoinc_auto_increment_);

}//end namespace share
}//end namespace oceanbase
