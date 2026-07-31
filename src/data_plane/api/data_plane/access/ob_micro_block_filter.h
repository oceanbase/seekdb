/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_MICRO_BLOCK_FILTER_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_MICRO_BLOCK_FILTER_H_

namespace oceanbase
{
namespace blocksstable
{
class ObIMicroBlockRowScanner;
}
namespace common
{
class ObBitmap;
}
namespace sql
{
class ObPushdownFilterExecutor;
struct PushdownFilterInfo;
}
namespace data_plane
{

int filter_micro_block(
    blocksstable::ObIMicroBlockRowScanner &scanner,
    sql::ObPushdownFilterExecutor *parent,
    sql::ObPushdownFilterExecutor &filter,
    sql::PushdownFilterInfo &filter_info,
    bool can_use_vectorize,
    common::ObBitmap &bitmap);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_MICRO_BLOCK_FILTER_H_
