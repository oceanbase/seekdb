/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_SHARE_OB_LIB_CACHE_NAMESPACE_H_
#define OCEANBASE_SHARE_OB_LIB_CACHE_NAMESPACE_H_

namespace oceanbase
{
namespace sql
{

enum ObLibCacheNameSpace
{
  NS_INVALID,
#define LIB_CACHE_OBJ_DEF(ns, ns_name, ck_class, cn_class, co_class, label) ns,
#include "share/ob_lib_cache_namespace.def"
#undef LIB_CACHE_OBJ_DEF
  NS_MAX
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_LIB_CACHE_NAMESPACE_H_
