// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef OCEANBASE_QUERY_VECTOR_EMBEDDING_REQUEST_BUILDER_H_
#define OCEANBASE_QUERY_VECTOR_EMBEDDING_REQUEST_BUILDER_H_
#include <cstdint>
namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObString;
template <typename T> class ObIArray;
}
namespace share
{
class EmbeddingRequestBuilder
{
public:
  // Serialize [start, end) through Rust. On success, body belongs to allocator
  // and stays valid until freed/reset, including throughout asynchronous HTTP.
  // On failure, body is null and length is zero. No Rust pointers escape.
  static int build(const common::ObIArray<common::ObString> &inputs,
                   int64_t start, int64_t end, const common::ObString &model,
                   int64_t dimension, bool use_base64_format,
                   common::ObIAllocator &allocator, char *&body, int64_t &length);
};
}
}
#endif
