/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_READ_H_
#define OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_READ_H_

#include "common/object/ob_object.h"

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

int lob_binary_equal(common::ObLobLocatorV2 &left,
                     common::ObLobLocatorV2 &right,
                     int64_t timeout_ts,
                     transaction::ObTxDesc *tx_desc,
                     bool &is_equal);

// Read a LOB into a buffer whose storage/capacity has already been supplied by
// the caller. On success, buffer.length() is the number of materialized bytes.
int read_lob_to_buffer(common::ObIAllocator &allocator,
                       common::ObLobLocatorV2 &lob,
                       int64_t timeout_ts,
                       transaction::ObTxDesc *tx_desc,
                       common::ObString &buffer);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_READ_H_
