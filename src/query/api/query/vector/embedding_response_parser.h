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

#ifndef OCEANBASE_QUERY_VECTOR_EMBEDDING_RESPONSE_PARSER_H_
#define OCEANBASE_QUERY_VECTOR_EMBEDDING_RESPONSE_PARSER_H_

#include <cstddef>
#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
template <typename T> class ObIArray;
}
namespace share
{

// Synchronous response decoding, independent of HTTP and task scheduling.
class EmbeddingResponseParser
{
public:
  // Input is borrowed only for this call. Rust releases its parsing scratch
  // before returning. Decoded vectors are copied into allocator and remain valid
  // until that allocator is reset; no Rust pointers escape to the caller.
  // Append in response order without clearing existing entries. On failure,
  // successfully appended vectors remain in output_vectors, matching the task
  // parser's existing partial-result behavior. The caller owns synchronization
  // and must not publish results until parsing has completed successfully.
  static int parse(const char *response_data,
                   size_t response_size,
                   int64_t dimension,
                   bool use_base64_format,
                   common::ObIAllocator &allocator,
                   common::ObIArray<float *> &output_vectors);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_QUERY_VECTOR_EMBEDDING_RESPONSE_PARSER_H_
