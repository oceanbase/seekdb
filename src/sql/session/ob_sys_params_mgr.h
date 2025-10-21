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

#ifndef OCEANBASE_SQL_SYS_PARAMS_MGR_H_
#define OCEANBASE_SQL_SYS_PARAMS_MGR_H_

#include "lib/list/ob_list.h"
#include "sql/engine/ob_physical_plan.h"

namespace oceanbase
{
namespace sql
{
const char *STR_SYS_PAREAMS = "sys_params";
const char *STR_SORT_MEM_SIZE_LIMIT = "sort_mem_size_limit";
const char *STR_GROUP_MEM_SIZE_LIMIT = "group_mem_size_limit";
// FIX ME,
// 1. MIN value
static const int64_t MIN_SORT_MEM_SIZE_LIMIT = 10000; // 10M
static const int64_t MIN_GROUP_MEM_SIZE_LIMIT = 10000; // 10M
// 2. MAX value, -1 means no limit

class ObSysParamsMgr
{
public:
  ObSysParamsMgr();
  virtual ~ObSysParamsMgr();

  void set_sort_mem_size_limit(const int64_t size);
  void set_gorup_mem_size_limit(const int64_t size);


  int64_t get_sort_mem_size_limit() const;
  int64_t get_group_mem_size_limit() const;

private:
  int64_t sort_mem_size_limit_;
  int64_t group_mem_size_limit_;
  DISALLOW_COPY_AND_ASSIGN(ObSysParamsMgr);
};

inline int64_t ObSysParamsMgr::get_sort_mem_size_limit() const
{
  return sort_mem_size_limit_;
}

inline int64_t ObSysParamsMgr::get_group_mem_size_limit() const
{
  return group_mem_size_limit_;
}

void ObSysParamsMgr::set_sort_mem_size_limit(const int64_t size)
{
  if (size >= MIN_SORT_MEM_SIZE_LIMIT) {
    sort_mem_size_limit_ = size;
  } else if (size < 0) {
    sort_mem_size_limit_ = -1;
  } else {
    ; // lease than MIN value, no change
  }
}

void ObSysParamsMgr::set_gorup_mem_size_limit(const int64_t size)
{
  if (size >= MIN_GROUP_MEM_SIZE_LIMIT) {
    group_mem_size_limit_ = size;
  } else if (size < 0) {
    group_mem_size_limit_ = -1;
  } else {
    ; // lease than MIN value, no change
  }
}
} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_SYS_PARAMS_MGR_H_ */



