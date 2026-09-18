/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_EMBEDDING_RESPONSE_H_
#define SEEKDB_EMBEDDING_RESPONSE_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEEKDB_EMBEDDING_FLOAT 0u
#define SEEKDB_EMBEDDING_BASE64 1u

/* The vector is borrowed, native-endian IEEE 754 float32, valid ONLY during this
 * callback. Copy it into caller-owned storage before returning; do not retain or
 * free its pointer. Return 0 on success or an OceanBase error code to stop parsing.
 * The callback must not throw/unwind, change the input bytes or invalidate context.
 */
typedef int32_t (*SeekdbEmbeddingEmit)(void *context, const float *values, size_t count);

/* Synchronous, reentrant; no global state and no pointers retained after return.
 * data must address length readable bytes in one allocation throughout the call.
 * No trailing NUL is required. encoding must be one of the constants above.
 * context is passed through unchanged (and may be NULL if the callback permits).
 * Calls sharing mutable context/output must be synchronized by the caller.
 *
 * All JSON is validated before emitting anything. Complete vectors are emitted
 * in response order. On a later semantic or callback error, earlier callback
 * effects remain; the first error is returned unchanged. A failing callback is
 * responsible for rolling back its own unpublished allocation.
 *
 * Rust owns and releases all parsing scratch and temporary vectors before return.
 * The caller owns copied output. No cross-language free function is needed.
 * Expected input/allocation failures return error codes; linked server profiles
 * use panic=abort, so an unexpected Rust panic cannot unwind through C++.
 */
int32_t seekdb_embedding_response_parse(const uint8_t *data, size_t length,
                                      int64_t dimension, uint32_t encoding,
                                      void *context, SeekdbEmbeddingEmit emit);

#ifdef __cplusplus
}
#endif
#endif
