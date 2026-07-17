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

#define USING_LOG_PREFIX SHARE
#include "share/catalog/ob_catalog_utils.h"

#include "lib/worker.h"

namespace oceanbase
{
namespace share
{

bool ObCatalogUtils::is_internal_catalog_name(const common::ObString &name_from_sql, const ObNameCaseMode &case_mode)
{
  bool is_internal = false;
  if (OB_ORIGIN_AND_SENSITIVE == case_mode) {
    is_internal = (name_from_sql.compare(OB_INTERNAL_CATALOG_NAME) == 0);
  } else {
    is_internal = (name_from_sql.case_compare(OB_INTERNAL_CATALOG_NAME) == 0);
  }
  return is_internal;
}

bool ObCatalogUtils::is_internal_catalog_name(const common::ObString &name_from_meta)
{
  return (name_from_meta.compare(OB_INTERNAL_CATALOG_NAME) == 0);
}

} // namespace share
} // namespace oceanbase
