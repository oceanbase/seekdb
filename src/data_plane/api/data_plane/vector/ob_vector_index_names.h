/*
 * Copyright (c) 2025 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 */

#ifndef OCEANBASE_DATA_PLANE_API_VECTOR_OB_VECTOR_INDEX_NAMES_H_
#define OCEANBASE_DATA_PLANE_API_VECTOR_OB_VECTOR_INDEX_NAMES_H_

namespace oceanbase
{
namespace data_plane
{

class ObVectorIndexNames
{
public:
  static const char *index_id_table_suffix()
  {
    return "_index_id_table";
  }
};

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_VECTOR_OB_VECTOR_INDEX_NAMES_H_
