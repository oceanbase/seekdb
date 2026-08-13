/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
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
