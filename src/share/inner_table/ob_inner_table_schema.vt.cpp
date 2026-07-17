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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_inner_table_schema.h"

namespace oceanbase
{
namespace share
{
VTMapping vt_mappings[5000];
bool vt_mapping_init()
{
   int64_t start_idx = common::OB_MAX_MYSQL_VIRTUAL_TABLE_ID + 1;
   return true;
} // end define vt_mappings

bool inited_vt = vt_mapping_init();

} // end namespace share
} // end namespace oceanbase
