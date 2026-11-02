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

#include "observer/table_load/plan/ob_table_load_plan_common.h"

namespace oceanbase
{
namespace observer
{
DEFINE_ENUM_FUNC(ObTableLoadTableType::Type, type, OB_TABLE_LOAD_TABLE_TYPE_DEF,
                 ObTableLoadTableType::);

DEFINE_ENUM_FUNC(ObTableLoadDependencyType::Type, type, OB_TABLE_LOAD_DEPENDENCY_TYPE_DEF,
                 ObTableLoadDependencyType::);

DEFINE_ENUM_FUNC(ObTableLoadInputType::Type, type, OB_TABLE_LOAD_INPUT_TYPE_DEF,
                 ObTableLoadInputType::);

DEFINE_ENUM_FUNC(ObTableLoadInputDataType::Type, type, OB_TABLE_LOAD_INPUT_DATA_TYPE_DEF,
                 ObTableLoadInputDataType::);

DEFINE_ENUM_FUNC(ObTableLoadWriteType::Type, type, OB_TABLE_LOAD_WRITE_TYPE_DEF,
                 ObTableLoadWriteType::);

DEFINE_ENUM_FUNC(ObTableLoadOpType::Type, type, OB_TABLE_LOAD_OP_TYPE_DEF, ObTableLoadOpType::);

} // namespace observer
} // namespace oceanbase
