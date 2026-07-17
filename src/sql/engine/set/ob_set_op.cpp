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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/set/ob_set_op.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObSetSpec::ObSetSpec(ObIAllocator &alloc, const ObPhyOperatorType type)
    : ObOpSpec(alloc, type),
    is_distinct_(false),
    set_exprs_(alloc),
    sort_collations_(alloc),
    sort_cmp_funs_(alloc)
{
}

OB_SERIALIZE_MEMBER((ObSetSpec, ObOpSpec),
    is_distinct_,
    set_exprs_,
    sort_collations_,
    sort_cmp_funs_);

} // end namespace sql
} // end namespace oceanbase
