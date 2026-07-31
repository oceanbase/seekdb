/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_LOCK_WAIT_STAT_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_LOCK_WAIT_STAT_H_

namespace oceanbase
{
namespace data_plane
{

// Marks the query execution interval used by lock-wait accounting.  The
// request node and its internal state machine remain owned by the data plane.
void begin_lock_wait_request();
void end_lock_wait_request();

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_LOCK_WAIT_STAT_H_
