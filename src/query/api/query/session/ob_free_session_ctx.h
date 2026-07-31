/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_SESSION_OB_FREE_SESSION_CTX_H_
#define OCEANBASE_QUERY_API_SESSION_OB_FREE_SESSION_CTX_H_

#include <stdint.h>
#include "lib/utility/ob_print_utils.h"

namespace oceanbase
{
namespace sql
{

// Value object identifying a temporary session that must be released.
class ObFreeSessionCtx
{
public:
  ObFreeSessionCtx()
    : has_inc_active_num_(false), sessid_(0)
  {}
  ~ObFreeSessionCtx() {}

  VIRTUAL_TO_STRING_KV(K_(has_inc_active_num), K_(sessid));

  bool has_inc_active_num_;
  uint32_t sessid_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_SESSION_OB_FREE_SESSION_CTX_H_
