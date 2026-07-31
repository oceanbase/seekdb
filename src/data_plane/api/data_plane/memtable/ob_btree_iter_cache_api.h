/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_MEMTABLE_OB_BTREE_ITER_CACHE_API_H_
#define OCEANBASE_DATA_PLANE_API_MEMTABLE_OB_BTREE_ITER_CACHE_API_H_

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace data_plane
{

void *create_btree_iter_cache(common::ObIAllocator &allocator);
void destroy_btree_iter_cache(common::ObIAllocator &allocator, void *&cache);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_MEMTABLE_OB_BTREE_ITER_CACHE_API_H_
