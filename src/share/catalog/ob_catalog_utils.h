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

#ifndef OB_CATALOG_UTILS_H
#define OB_CATALOG_UTILS_H

#include "lib/string/ob_sql_string.h"

namespace oceanbase
{
namespace share
{

class ObCatalogUtils
{
public:
  // The name used is derived from sql, and has not been processed
  static bool is_internal_catalog_name(const common::ObString &name_from_sql, const ObNameCaseMode &case_mode);
  // The name used is derived from CatalogSchema, the case of the name has already been converted
  static bool is_internal_catalog_name(const common::ObString &name_from_meta);

private:
  DISALLOW_COPY_AND_ASSIGN(ObCatalogUtils);
};

} // namespace share
} // namespace oceanbase

#endif // OB_CATALOG_UTILS_H
