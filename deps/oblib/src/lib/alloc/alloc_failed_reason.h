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

#ifndef _OCEABASE_LIB_ALLOC_FAILED_REASON_H_
#define _OCEABASE_LIB_ALLOC_FAILED_REASON_H_

#include <stdint.h>
#include "lib/coro/co_var.h"

namespace oceanbase
{
namespace lib
{
enum AllocFailedReason // FARM COMPAT WHITELIST
{
  UNKNOWN = 0,
  INVALID_ALLOC_SIZE,
  SINGLE_ALLOC_SIZE_OVERFLOW,
  CTX_HOLD_REACH_LIMIT,
  TENANT_HOLD_REACH_LIMIT,
  SERVER_HOLD_REACH_LIMIT,
  PHYSICAL_MEMORY_EXHAUST,
  ERRSIM_INJECTION
};

struct AllocFailedCtx
{
public:
  int reason_;
  int64_t alloc_size_;
  union
  {
    int errno_;
    struct {
      int64_t ctx_id_;
      int64_t ctx_hold_;
      int64_t ctx_limit_;
    };
    struct {
      uint64_t tenant_id_;
      int64_t tenant_hold_;
      int64_t tenant_limit_;
    };
    struct {
      int64_t server_hold_;
      int64_t server_limit_;
    };
  };
  bool need_wash_block() const
  {
    return reason_ == lib::CTX_HOLD_REACH_LIMIT ||
           reason_ == lib::TENANT_HOLD_REACH_LIMIT ||
           reason_ == lib::SERVER_HOLD_REACH_LIMIT;
  }
  bool need_wash_chunk() const
  {
    return reason_ == lib::PHYSICAL_MEMORY_EXHAUST;

  }
};

char *alloc_failed_msg();

AllocFailedCtx &g_alloc_failed_ctx();
void print_alloc_failed_msg(uint64_t tenant_id, uint64_t ctx_id,
                            int64_t ctx_hold, int64_t ctx_limit,
                            int64_t tenant_hold, int64_t tenant_limit);

} // end of namespace lib
} // end of namespace oceanbase

#endif /* _OCEABASE_LIB_ALLOC_FAILED_REASON_H_ */
