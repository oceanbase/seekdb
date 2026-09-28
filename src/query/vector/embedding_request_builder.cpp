// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#include "query/vector/embedding_request_builder.h"
#include "embedding_error.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/container/ob_iarray.h"
#include "lib/string/ob_string.h"
#include <cstring>
#include <limits>

namespace oceanbase
{
namespace share
{
using namespace common;
namespace
{
struct RequestOutputContext
{
  ObIAllocator &allocator;
  char *&body;
  int64_t &length;
};

extern "C" int32_t copy_embedding_body(void *opaque, const uint8_t *body, size_t length) noexcept
{
  auto &context = *static_cast<RequestOutputContext *>(opaque);
  if (length == 0 || length > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return OB_SIZE_OVERFLOW;
  }
  char *copy = static_cast<char *>(context.allocator.alloc(static_cast<int64_t>(length)));
  if (nullptr == copy) {
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  std::memcpy(copy, body, length);
  context.body = copy;
  context.length = static_cast<int64_t>(length);
  return OB_SUCCESS;
}

EmbeddingBytes bytes(const ObString &text)
{
  return {reinterpret_cast<const uint8_t *>(text.ptr()), static_cast<size_t>(text.length())};
}
}

int EmbeddingRequestBuilder::build(const ObIArray<ObString> &inputs,
                                  int64_t start, int64_t end, const ObString &model,
                                  int64_t dimension, bool use_base64_format,
                                  ObIAllocator &allocator, char *&body, int64_t &length)
{
  int ret = OB_SUCCESS;
  body = nullptr;
  length = 0;
  EmbeddingBytes local_texts[16];
  EmbeddingBytes *texts = local_texts;
  if (start < 0 || end < start || end > inputs.count()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t count = end - start;
    if (count > std::numeric_limits<int64_t>::max() / sizeof(EmbeddingBytes)) {
      ret = OB_SIZE_OVERFLOW;
    } else if (count > 16) {
      texts = static_cast<EmbeddingBytes *>(allocator.alloc(count * sizeof(EmbeddingBytes)));
      if (nullptr == texts) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      }
    }
    if (OB_SUCC(ret)) {
      for (int64_t i = 0; i < count; ++i) {
        texts[i] = bytes(inputs.at(start + i));
      }
      RequestOutputContext context{allocator, body, length};
      ret = embedding_error_code(embedding_request_build(texts, static_cast<size_t>(count),
          bytes(model), dimension, use_base64_format ? EMBEDDING_BASE64 : EMBEDDING_FLOAT,
          &context, copy_embedding_body));
    }
  }
  if (nullptr != texts && texts != local_texts) {
    allocator.free(texts);
  }
  return ret;
}
}
}
