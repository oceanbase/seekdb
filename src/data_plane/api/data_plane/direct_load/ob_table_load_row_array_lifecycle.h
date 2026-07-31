/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_DIRECT_LOAD_OB_TABLE_LOAD_ROW_ARRAY_LIFECYCLE_H_
#define OCEANBASE_DATA_PLANE_DIRECT_LOAD_OB_TABLE_LOAD_ROW_ARRAY_LIFECYCLE_H_

namespace oceanbase
{
namespace table
{
class ObTableLoadTabletObjRow;
template <class T>
class ObTableLoadRowArray;
}
namespace data_plane
{

// The table-load module owns the concrete row-array type and its allocator.
// Storage may carry the opaque payload, but destruction stays with its owner.
void destroy_table_load_row_array(
    table::ObTableLoadRowArray<table::ObTableLoadTabletObjRow> *&row_array);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_DIRECT_LOAD_OB_TABLE_LOAD_ROW_ARRAY_LIFECYCLE_H_
