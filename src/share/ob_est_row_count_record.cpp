/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX SHARE
#include "share/ob_est_row_count_record.h"

namespace oceanbase
{
namespace common
{

OB_SERIALIZE_MEMBER(ObEstRowCountRecord,
                    table_id_,
                    table_type_,
                    version_range_,
                    logical_row_count_,
                    physical_row_count_);

} // namespace common
} // namespace oceanbase
