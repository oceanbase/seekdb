/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#ifndef OCEANBASE_SHARE_INSTANCE_META_COLLECTION_H_
#define OCEANBASE_SHARE_INSTANCE_META_COLLECTION_H_

#include <cstdint>

namespace oceanbase
{
namespace share
{
namespace instance_meta
{

// Persistent collection identities. These are not SQL schema object IDs.
enum class MetaCollection : uint64_t
{
  NAMESPACES = 1,
  NAMESPACE_NAMES = 2,
  PAGES = 5,
  COUNTERS = 6,
  SNAPSHOT_COORDINATION = 7,
  STORAGE_LAYOUTS = 8,
};

} // namespace instance_meta
} // namespace share
} // namespace oceanbase
#endif
