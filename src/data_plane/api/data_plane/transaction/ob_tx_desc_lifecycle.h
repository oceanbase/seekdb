/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_LIFECYCLE_H_
#define OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_LIFECYCLE_H_

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace transaction
{
class ObTxDesc;
}
namespace data_plane
{

// Transaction descriptors are owned by the data plane. Query may keep a
// borrowed/duplicated handle, but must not reach into ObTransService to manage
// that handle's lifetime.
int clone_tx_desc(common::ObIAllocator &allocator,
                  transaction::ObTxDesc *source,
                  transaction::ObTxDesc *&clone);
void release_tx_desc(transaction::ObTxDesc *&desc);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_TRANSACTION_OB_TX_DESC_LIFECYCLE_H_
