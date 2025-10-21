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

#include "ob_sql_define.h"

namespace oceanbase
{
namespace sql
{
DEFINE_ENUM_FUNC(ObPQDistributeMethod::Type, type, PQ_DIST_METHOD_DEF, ObPQDistributeMethod::);

ObOrderDirection default_asc_direction()
{
  return NULLS_FIRST_ASC;
}

ObOrderDirection default_desc_direction()
{
  return  NULLS_LAST_DESC;
}

} // namespace sql
} // namespace oceanbase
