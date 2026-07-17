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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_VEC_STRATEGY_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_VEC_STRATEGY_H_

namespace oceanbase
{
namespace sql
{

template <typename SortImpl>
class ObISortStrategy
{
public:
  virtual ~ObISortStrategy() {}
  virtual int sort(SortImpl &impl, const int64_t begin) = 0;
};

template <typename SortImpl>
class ObFullSortStrategy final : public ObISortStrategy<SortImpl>
{
public:
  virtual int sort(SortImpl &impl, const int64_t begin) override
  {
    return impl.do_full_sort_strategy(begin);
  }
};

template <typename SortImpl>
class ObPartitionSortStrategy final : public ObISortStrategy<SortImpl>
{
public:
  virtual int sort(SortImpl &impl, const int64_t begin) override
  {
    return impl.do_partition_sort_strategy(begin);
  }
};

template <typename SortImpl>
class ObPartitionTopNSortStrategy final : public ObISortStrategy<SortImpl>
{
public:
  virtual int sort(SortImpl &impl, const int64_t begin) override
  {
    return impl.do_partition_topn_sort_strategy(begin);
  }
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_VEC_STRATEGY_H_ */
