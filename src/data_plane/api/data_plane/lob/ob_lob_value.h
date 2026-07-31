/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_VALUE_H_
#define OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_VALUE_H_

#include "common/object/ob_object.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace blocksstable
{
class ObDatumRow;
struct ObStorageDatum;
}
namespace share
{
namespace schema
{
struct ObColDesc;
}
}
namespace data_plane
{

void set_zero_lob_value(common::ObObjType type, common::ObObj &value);
int fill_lob_header(common::ObIAllocator &allocator,
                    common::ObString &data,
                    common::ObString &out);
int fill_lob_header(common::ObIAllocator &allocator,
                    blocksstable::ObStorageDatum &datum);
int fill_lob_header(
    common::ObIAllocator &allocator,
    const common::ObIArray<share::schema::ObColDesc> &column_ids,
    blocksstable::ObDatumRow &datum_row);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_LOB_OB_LOB_VALUE_H_
