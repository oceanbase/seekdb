/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_BLOCKSSTABLE_OB_DATUM_ROW_FACTORY_H_
#define OCEANBASE_DATA_PLANE_API_BLOCKSSTABLE_OB_DATUM_ROW_FACTORY_H_

#include <stdint.h>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace blocksstable
{
struct ObDatumRow;
}
namespace data_plane
{

int create_datum_row(
    common::ObIAllocator &allocator,
    int64_t column_count,
    blocksstable::ObDatumRow *&row);

int create_datum_rows(
    common::ObIAllocator &allocator,
    int64_t row_count,
    int64_t column_count,
    blocksstable::ObDatumRow *&rows);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_BLOCKSSTABLE_OB_DATUM_ROW_FACTORY_H_
