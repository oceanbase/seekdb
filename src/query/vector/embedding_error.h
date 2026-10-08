// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#pragma once

#include "embedding.h"
#include "lib/ob_errno.h"

namespace oceanbase
{
namespace share
{
inline int embedding_error_code(const EmbeddingResult &result)
{
  using namespace common;
  switch (result.status) {
    case EmbeddingStatus_Success: return OB_SUCCESS;
    case EmbeddingStatus_InvalidArgument: return OB_INVALID_ARGUMENT;
    case EmbeddingStatus_AllocationFailed: return OB_ALLOCATE_MEMORY_FAILED;
    case EmbeddingStatus_DimensionMismatch: return OB_ERR_UNEXPECTED;
    case EmbeddingStatus_BufferNotEnough: return OB_BUF_NOT_ENOUGH;
    case EmbeddingStatus_MissingField: return OB_SEARCH_NOT_FOUND;
    case EmbeddingStatus_InvalidJson: return OB_ERR_INVALID_JSON_TEXT;
    case EmbeddingStatus_CallbackFailed:
      return result.callback_error == OB_SUCCESS ? OB_ERR_UNEXPECTED : result.callback_error;
    default: return OB_ERR_UNEXPECTED;
  }
}
} // namespace share
} // namespace oceanbase
