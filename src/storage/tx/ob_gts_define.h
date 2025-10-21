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

#ifndef OCEANBASE_TRANSACTION_OB_GTS_DEFINE_
#define OCEANBASE_TRANSACTION_OB_GTS_DEFINE_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/utility.h"
#include "share/ob_define.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "ob_trans_define.h"

namespace oceanbase
{
namespace transaction
{

typedef common::ObIntWarp ObTsTenantInfo;

enum ObGTSCacheTaskType
{
  INVALID_GTS_TASK_TYPE = -1,
  GET_GTS = 0,
  WAIT_GTS_ELAPSING,
};

inline bool atomic_update(int64_t *v, const int64_t x)
{
  bool bool_ret = false;
  int64_t ov = ATOMIC_LOAD(v);
  while (ov < x) {
    if (ATOMIC_BCAS(v, ov, x)) {
      bool_ret = true;
      break;
    } else {
      ov = ATOMIC_LOAD(v);
    }
  }
  return bool_ret;
}

} // transaction
} // oceanbase

#endif // OCEANBASE_RANSACTION_OB_GTS_DEFINE_
