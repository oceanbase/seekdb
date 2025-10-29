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

#ifndef _ALLOC_FUNC_H_
#define _ALLOC_FUNC_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
struct ObLabelItem;
} // end of namespace common

namespace lib
{
// statistic relating
struct ObLabel;
struct ObMemAttr;
void set_hard_memory_limit(int64_t bytes);
int64_t get_hard_memory_limit();
void set_memory_limit(int64_t bytes);
int64_t get_memory_limit();
int64_t get_memory_hold();
int64_t get_memory_used();
int64_t get_memory_avail();
int64_t get_hard_memory_remain();

void set_tenant_memory_limit(uint64_t tenant_id, int64_t bytes);
int64_t get_tenant_memory_limit(uint64_t tenant_id);
int64_t get_tenant_memory_hold(uint64_t tenant_id);
int64_t get_tenant_memory_hold(const uint64_t tenant_id, const uint64_t ctx_id);
int64_t get_tenant_cache_hold(uint64_t tenant_id);
int64_t get_tenant_memory_remain(uint64_t tenant_id);
void get_tenant_label_memory(
  uint64_t tenant_id, ObLabel &label, common::ObLabelItem &item);
void ob_set_reserved_memory(const int64_t bytes);
int64_t ob_get_reserved_memory();

// Set Work Area memory limit for specified tenant.
// ms_pctg: percentage limitation of tenant memory can be used by MemStore
// pc_pctg: percentage limitation of tenant memory can be used by Plan Cache
// wa_pctg: percentage limitation of tenant memory can be used by Work Area

int set_ctx_limit(uint64_t tenant_id, uint64_t ctx_id, const int64_t limit);
int set_wa_limit(uint64_t tenand_id, int64_t wa_pctg);

// set meta object memory limit for specified tenant.
// - meta_obj_pct_lmt: percentage limitation of tenant memory can be used for meta object.
int set_meta_obj_limit(uint64_t tenant_id, int64_t meta_obj_pct_lmt);

// set rpc memory limit for specified tenant.
// - rpc_pct_lmt: percentage limitation of tenant rpc memory.
int set_rpc_limit(uint64_t tenant_id, int64_t rpc_pct_lmt);

bool errsim_alloc(const ObMemAttr &attr);

int set_req_chunkmgr_parallel(uint64_t tenant_id, uint64_t ctx_id, int32_t parallel);
} // end of namespace lib
} // end of namespace oceanbase

#endif /* _ALLOC_FUNC_H_ */
