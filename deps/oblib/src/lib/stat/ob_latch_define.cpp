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

#include "lib/stat/ob_latch_define.h"
namespace oceanbase
{
namespace common
{

#define LATCH_DEF_true(def, id, name, policy, max_spin_cnt, max_yield_cnt) \
    {id, name, ObLatchPolicy::policy, max_spin_cnt, max_yield_cnt},
#define LATCH_DEF_false(def, id, name, policy, max_spin_cnt, max_yield_cnt)

const ObLatchDesc OB_LATCHES[] __attribute__ ((init_priority(102))) = {
#define LATCH_DEF(def, id, name, policy, max_spin_cnt, max_yield_cnt, enable) \
LATCH_DEF_##enable(def, id, name, policy, max_spin_cnt, max_yield_cnt)
#include "lib/stat/ob_latch_define.h"
#undef LATCH_DEF
};
#undef LATCH_DEF_true
#undef LATCH_DEF_false

static_assert(ARRAYSIZEOF(OB_LATCHES) == 265, "DO NOT delete latch defination");
static_assert(ObLatchIds::LATCH_END == ARRAYSIZEOF(OB_LATCHES) - 1, "update id of LATCH_END before adding your defination");

}
}
