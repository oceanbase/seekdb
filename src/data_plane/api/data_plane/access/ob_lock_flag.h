/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_ACCESS_OB_LOCK_FLAG_H_
#define OCEANBASE_DATA_PLANE_API_ACCESS_OB_LOCK_FLAG_H_

namespace oceanbase
{
namespace data_plane
{

enum ObLockFlag
{
  LF_NONE = 0,
  LF_WRITE = 1,
};

} // namespace data_plane

// Compatibility names for data-plane implementation code.  New callers at
// the boundary use data_plane::ObLockFlag directly.
namespace storage
{
using ObLockFlag = data_plane::ObLockFlag;
static constexpr ObLockFlag LF_NONE = data_plane::LF_NONE;
static constexpr ObLockFlag LF_WRITE = data_plane::LF_WRITE;
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_ACCESS_OB_LOCK_FLAG_H_
