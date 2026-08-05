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

#ifndef OCEANBASE_DATA_PLANE_API_FTS_OB_DOC_ID_H_
#define OCEANBASE_DATA_PLANE_API_FTS_OB_DOC_ID_H_

#include "object/ob_object.h"
#include "share/ob_define.h"

namespace oceanbase
{
namespace share
{

class ObDocIDUtils
{
public:
  static ObDocIDType get_type_by_col_id(const uint64_t col_id)
  {
    return col_id == OB_HIDDEN_PK_INCREMENT_COLUMN_ID
        ? ObDocIDType::HIDDEN_INC_PK
        : ObDocIDType::TABLET_SEQUENCE;
  }

  static bool is_docid_col_id_valid(const uint64_t col_id)
  {
    return OB_HIDDEN_PK_INCREMENT_COLUMN_ID == col_id
        || (col_id > OB_APP_MIN_COLUMN_ID && OB_INVALID_ID != col_id);
  }
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_API_FTS_OB_DOC_ID_H_
