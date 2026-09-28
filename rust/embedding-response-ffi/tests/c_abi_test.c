/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "embedding_response.h"
#include "embedding_request.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "C ABI check failed at %d: %s\n", __LINE__, #x); abort(); } } while (0)

struct Output {
  float copied[4];
  int calls;
  int count;
  int fail_at;
};

static int32_t emit(void *context, const float *values, size_t count)
{
  struct Output *out = context;
  ++out->calls;
  if (out->calls == out->fail_at) return -4013;
  CHECK(count == 1 && out->count < 4);
  memcpy(out->copied + out->count++, values, sizeof(float));
  return 0;
}

static int32_t emit_body(void *context, const uint8_t *body, size_t length)
{
  const char expected[] = "{\"input\":[\"a\\u0000\"],\"model\":\"\",\"encoding_format\":\"float\",\"dimensions\":1}";
  CHECK(length == sizeof(expected) - 1);
  CHECK(memcmp(body, expected, length) == 0);
  ++*(int *)context;
  return -4013;
}

int main(void)
{
  const char input[] = "{\"data\":[{\"embedding\":[1]},{\"embedding\":[2]}]}";
  struct Output out = {{0}, 0, 0, -1};
  CHECK(seekdb_embedding_response_parse((const uint8_t *)input, sizeof(input) - 1,
      1, SEEKDB_EMBEDDING_FLOAT, &out, emit) == 0);
  CHECK(out.calls == 2 && out.count == 2 && out.copied[0] == 1 && out.copied[1] == 2);
  out.calls = out.count = 0;
  out.fail_at = 2;
  CHECK(seekdb_embedding_response_parse((const uint8_t *)input, sizeof(input) - 1,
      1, SEEKDB_EMBEDDING_FLOAT, &out, emit) == -4013);
  CHECK(out.calls == 2 && out.count == 1 && out.copied[0] == 1);
  CHECK(seekdb_embedding_response_parse(NULL, 1, 1, SEEKDB_EMBEDDING_FLOAT, &out, emit) == -4002);
  CHECK(seekdb_embedding_response_parse((const uint8_t *)input, sizeof(input) - 1,
      1, SEEKDB_EMBEDDING_FLOAT, &out, NULL) == -4002);
  CHECK(seekdb_embedding_response_parse((const uint8_t *)input, SIZE_MAX,
      1, SEEKDB_EMBEDDING_FLOAT, &out, emit) == -4002);
  const uint8_t text[] = {'a', 0};
  const SeekdbEmbeddingBytes request_input = {text, sizeof(text)};
  const SeekdbEmbeddingBytes model = {NULL, 0};
  int body_calls = 0;
  CHECK(seekdb_embedding_request_build(&request_input, 1, model, 1,
      SEEKDB_EMBEDDING_FLOAT, &body_calls, emit_body) == -4013);
  CHECK(body_calls == 1);
  puts("PASS: C header and linked Rust ABI, copied output and callback failure");
  return 0;
}
