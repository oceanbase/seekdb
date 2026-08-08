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

#ifndef OCEANBASE_QUERY_ENGINE_BASIC_OB_EXTERNAL_PUSHDOWN_FILTER_H_
#define OCEANBASE_QUERY_ENGINE_BASIC_OB_EXTERNAL_PUSHDOWN_FILTER_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObBitmap;
class ObIAllocator;
}
namespace sql
{
class ObPushdownFilterExecutor;

// A query-owned execution envelope for a filter whose row-domain behaviour is
// owned by another module.  The native batch is deliberately opaque: query
// transports it but must not learn the producer's native data vocabulary.
class ObExternalFilterExecutionContext final
{
public:
  ObExternalFilterExecutionContext(
      void *native_batch,
      const int64_t start,
      const int64_t count,
      const common::ObBitmap *candidate_rows,
      common::ObBitmap &result,
      const bool use_vectorize)
    : native_batch_(native_batch),
      start_(start),
      count_(count),
      candidate_rows_(candidate_rows),
      result_(result),
      use_vectorize_(use_vectorize)
  {}

  void *native_batch() const { return native_batch_; }
  int64_t start() const { return start_; }
  int64_t count() const { return count_; }
  const common::ObBitmap *candidate_rows() const { return candidate_rows_; }
  common::ObBitmap &result() const { return result_; }
  bool use_vectorize() const { return use_vectorize_; }

private:
  void *native_batch_;
  int64_t start_;
  int64_t count_;
  const common::ObBitmap *candidate_rows_;
  common::ObBitmap &result_;
  bool use_vectorize_;
};

// Behavioural extension seam for filters that are evaluated by the owner of
// the native batch.  Query only schedules this operation and combines its
// bitmap with the ordinary query filter tree.
class ObIExternalPushdownFilter
{
public:
  virtual ~ObIExternalPushdownFilter() = default;
  virtual int execute(ObExternalFilterExecutionContext &context) = 0;
};

class ObExternalPushdownFilterRuntime;

int create_external_pushdown_filter_runtime(
    common::ObIAllocator &allocator,
    ObIExternalPushdownFilter &filter,
    ObExternalPushdownFilterRuntime *&runtime);

void destroy_external_pushdown_filter_runtime(
    common::ObIAllocator &allocator,
    ObExternalPushdownFilterRuntime *&runtime);

int attach_external_pushdown_filter(
    ObExternalPushdownFilterRuntime &runtime,
    ObPushdownFilterExecutor *&root_filter);

void detach_external_pushdown_filter(ObExternalPushdownFilterRuntime &runtime);

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_ENGINE_BASIC_OB_EXTERNAL_PUSHDOWN_FILTER_H_
