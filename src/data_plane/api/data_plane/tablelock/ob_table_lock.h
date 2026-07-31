/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_H_
#define OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_H_

#include <stdint.h>
#include "data_plane/tablelock/ob_table_lock_mode.h"

namespace oceanbase
{
namespace transaction
{
class ObTxDesc;
struct ObTxParam;
}
namespace data_plane
{

// Transactional table-lock capabilities.  The data plane owns request
// construction and service lookup; query supplies only the lock intent.
int lock_table(transaction::ObTxDesc &tx,
               const transaction::ObTxParam &tx_param,
               uint64_t table_id,
               transaction::tablelock::ObTableLockMode lock_mode,
               int64_t timeout_us);

int lock_partition_or_subpartition(
    transaction::ObTxDesc &tx,
    const transaction::ObTxParam &tx_param,
    uint64_t table_id,
    uint64_t partition_object_id,
    transaction::tablelock::ObTableLockMode lock_mode,
    int64_t timeout_us);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TABLELOCK_OB_TABLE_LOCK_H_
