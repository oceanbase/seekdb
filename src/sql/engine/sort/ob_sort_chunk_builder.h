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

#ifndef OCEANBASE_SQL_ENGINE_SORT_OB_SORT_CHUNK_BUILDER_H_
#define OCEANBASE_SQL_ENGINE_SORT_OB_SORT_CHUNK_BUILDER_H_

namespace oceanbase
{
namespace sql
{

class ObSortChunkSingleSlicer
{
public:
  int64_t get_slice_idx(const int64_t row_idx) const
  {
    UNUSED(row_idx);
    return 0;
  }
};

class ObSortChunkMultiSlicer
{
public:
  explicit ObSortChunkMultiSlicer(const int64_t slice_cnt) : slice_cnt_(slice_cnt)
  {}

  int64_t get_slice_idx(const int64_t row_idx) const
  {
    return slice_cnt_ <= 1 ? 0 : row_idx % slice_cnt_;
  }

private:
  int64_t slice_cnt_;
};

template <typename SortImpl, typename Slicer = ObSortChunkSingleSlicer>
class ObSortChunkBuilder
{
public:
  explicit ObSortChunkBuilder(SortImpl &impl, const Slicer &slicer = Slicer())
    : impl_(impl), slicer_(slicer)
  {}

  template <typename Input>
  int build(const int64_t level, Input &input)
  {
    UNUSED(slicer_);
    return impl_.build_chunk(level, input);
  }

private:
  SortImpl &impl_;
  Slicer slicer_;
};

} // namespace sql
} // namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_SORT_OB_SORT_CHUNK_BUILDER_H_ */
