/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_EMBEDDING_REQUEST_H
#define SEEKDB_EMBEDDING_REQUEST_H
#include "embedding_response.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct SeekdbEmbeddingBytes {
  const uint8_t *data;
  size_t length;
} SeekdbEmbeddingBytes;

/* Called exactly once on successful serialization, synchronously. The body is
 * borrowed only during this callback: copy it for asynchronous HTTP use. The
 * callback must not throw/unwind, mutate inputs, or retain/free Rust pointers.
 * A nonzero callback error is returned unchanged. */
typedef int32_t (*SeekdbEmbeddingBodyEmit)(void *context, const uint8_t *body, size_t length);

/* Text slices contain UTF-8 bytes; embedded NUL/control bytes are escaped. Null
 * data is allowed only for zero length. inputs may be null only for count zero;
 * otherwise it points to count aligned, readable descriptors. All inputs stay
 * unchanged until return. No input/context pointer is retained. Nonpositive
 * dimension is omitted. encoding uses SEEKDB_EMBEDDING_FLOAT/BASE64. On validation
 * or allocation failure, emit is not called. Returns 0 or an OceanBase error. */
int32_t seekdb_embedding_request_build(const SeekdbEmbeddingBytes *inputs, size_t count,
    SeekdbEmbeddingBytes model, int64_t dimension, uint32_t encoding,
    void *context, SeekdbEmbeddingBodyEmit emit);
#ifdef __cplusplus
}
#endif
#endif
