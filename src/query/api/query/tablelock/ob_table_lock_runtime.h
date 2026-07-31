/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_QUERY_API_TABLELOCK_OB_TABLE_LOCK_RUNTIME_H_
#define OCEANBASE_QUERY_API_TABLELOCK_OB_TABLE_LOCK_RUNTIME_H_

#include <stdint.h>

namespace oceanbase
{
namespace query
{

// Query-owned autonomous cleanup for session-scoped locks.  The data plane
// supplies only the persisted owner identity; transaction/session setup stays
// behind this semantic interface.
int release_locks_for_dead_owner(uint8_t owner_type, int64_t owner_id);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_TABLELOCK_OB_TABLE_LOCK_RUNTIME_H_
