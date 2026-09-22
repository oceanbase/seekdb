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

#include "query/vector/embedding_response_parser.h"
#include "embedding_response.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/container/ob_iarray.h"
#include <cstring>
#include <limits>

namespace oceanbase
{
namespace share
{
using namespace common;

namespace
{
struct EmbeddingOutputContext
{
  ObIAllocator &allocator;
  ObIArray<float *> &vectors;
};

// Rust borrows this callback only during parse. No Rust pointer is retained.
extern "C" int32_t append_embedding_vector(void *opaque, const float *values,
                                           size_t count) noexcept
{
  auto &context = *static_cast<EmbeddingOutputContext *>(opaque);
  if (count == 0 || count > static_cast<size_t>(std::numeric_limits<int64_t>::max()) / sizeof(float)) {
    return OB_ERR_UNEXPECTED;
  }
  const int64_t bytes = static_cast<int64_t>(count * sizeof(float));
  float *copy = static_cast<float *>(context.allocator.alloc(bytes));
  if (nullptr == copy) {
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  std::memcpy(copy, values, static_cast<size_t>(bytes));
  const int ret = context.vectors.push_back(copy);
  if (OB_SUCCESS != ret) {
    // Never leave an unpublished buffer owned by neither side of the boundary.
    context.allocator.free(copy);
  }
  return ret;
}
} // namespace

int EmbeddingResponseParser::parse(const char *response_data,
                                   const size_t response_size,
                                   const int64_t dimension,
                                   const bool use_base64_format,
                                   ObIAllocator &allocator,
                                   ObIArray<float *> &output_vectors)
{
  EmbeddingOutputContext context{allocator, output_vectors};
  // Rust scratch uses the platform allocator. Attribute malloc-hook allocations
  // to the same scoped label used for other third-party parsers in this process.
  lib::ObMallocHookAttrGuard scratch_guard(lib::ObMemAttr("EmbRustTmp"));
  return seekdb_embedding_response_parse(
      reinterpret_cast<const uint8_t *>(response_data), response_size, dimension,
      use_base64_format ? SEEKDB_EMBEDDING_BASE64 : SEEKDB_EMBEDDING_FLOAT,
      &context, append_embedding_vector);
}

} // namespace share
} // namespace oceanbase
