/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_ACCESS_CONTEXT_H_
#define OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_ACCESS_CONTEXT_H_

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObILobAccessContext;
}
namespace data_plane
{

// Query may retain and pass this opaque handle, but the data plane owns its
// construction, destruction, and layout.
int create_lob_access_context(
    common::ObIAllocator &allocator,
    common::ObILobAccessContext *&context);
void destroy_lob_access_context(common::ObILobAccessContext *&context);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_ACCESS_CONTEXT_H_
