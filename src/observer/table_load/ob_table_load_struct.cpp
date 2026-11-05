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

#define USING_LOG_PREFIX SERVER

#include "observer/table_load/ob_table_load_struct.h"

namespace oceanbase
{
namespace observer
{
using namespace common;
using namespace table;

OB_SERIALIZE_MEMBER(ObTableLoadUniqueKey, table_id_, task_id_);

OB_SERIALIZE_MEMBER(ObTableLoadDDLParam,
                    dest_table_id_,
                    task_id_,
                    schema_version_,
                    snapshot_version_,
                    data_version_,
                    cluster_version_,
                    is_no_logging_);

} // namespace observer
} // namespace oceanbase
