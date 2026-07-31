/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_ENGINE_EXPR_OB_AI_MODEL_RESOLVER_H_
#define OCEANBASE_QUERY_API_ENGINE_EXPR_OB_AI_MODEL_RESOLVER_H_

#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace query
{

// Resolves query-owned AI model metadata without exposing expression objects
// or the SQL schema-resolution implementation to callers.
class ObAIModelResolver
{
public:
  static int resolve_model_name(
      common::ObIAllocator &allocator,
      const common::ObString &model_id,
      common::ObString &model_name);
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_ENGINE_EXPR_OB_AI_MODEL_RESOLVER_H_
