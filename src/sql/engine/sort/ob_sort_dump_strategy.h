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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_DUMP_STRATEGY_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_DUMP_STRATEGY_H_

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace sql
{

class ObSortResourceManager;

template <typename SortImpl>
class NormalDumpStrategy
{
public:
  NormalDumpStrategy(SortImpl &impl, ObSortResourceManager &res_mgr)
    : impl_(impl), res_mgr_(res_mgr) {}

  bool should_dump(int64_t mem_used)
  {
    return res_mgr_.should_dump(mem_used);
  }

  int do_dump()
  {
    return impl_.build_chunk_inner();
  }

private:
  SortImpl &impl_;
  ObSortResourceManager &res_mgr_;
};

template <typename SortImpl>
class IMMSDumpStrategy
{
public:
  IMMSDumpStrategy(SortImpl &impl, ObSortResourceManager &res_mgr)
    : impl_(impl), res_mgr_(res_mgr) {}

  bool should_dump(int64_t mem_used)
  {
    return res_mgr_.should_dump(mem_used);
  }

  int do_dump()
  {
    return impl_.build_chunk_inner();
  }

private:
  SortImpl &impl_;
  ObSortResourceManager &res_mgr_;
};

template <typename SortImpl>
class PartitionTopnDumpStrategy
{
public:
  PartitionTopnDumpStrategy(SortImpl &impl, ObSortResourceManager &res_mgr,
                            int64_t part_cnt)
    : impl_(impl), res_mgr_(res_mgr), part_cnt_(part_cnt) {}

  bool should_dump(int64_t mem_used)
  {
    int64_t total_mem = mem_used + part_cnt_ * 1024;
    return total_mem > res_mgr_.get_mem_limit();
  }

  int do_dump()
  {
    return impl_.build_chunk_inner();
  }

private:
  SortImpl &impl_;
  ObSortResourceManager &res_mgr_;
  int64_t part_cnt_;
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_DUMP_STRATEGY_H_ */
