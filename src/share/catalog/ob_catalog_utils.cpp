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
#include "sql/session/ob_basic_session_info.h"

namespace oceanbase
{
namespace share
{

bool ObCatalogUtils::is_internal_catalog_name(const common::ObString &name_from_sql, const ObNameCaseMode &case_mode)
{
  bool is_internal = false;
  if (lib::is_oracle_mode()) {
    is_internal = (name_from_sql.compare(OB_INTERNAL_CATALOG_NAME_UPPER) == 0);
  } else if (OB_ORIGIN_AND_SENSITIVE == case_mode) {
    is_internal = (name_from_sql.compare(OB_INTERNAL_CATALOG_NAME) == 0);
  } else {
    is_internal = (name_from_sql.case_compare(OB_INTERNAL_CATALOG_NAME) == 0);
  }
  return is_internal;
}

bool ObCatalogUtils::is_internal_catalog_name(const common::ObString &name_from_meta)
{
  return lib::is_oracle_mode() ? (name_from_meta.compare(OB_INTERNAL_CATALOG_NAME_UPPER) == 0)
                               : (name_from_meta.compare(OB_INTERNAL_CATALOG_NAME) == 0);
}

int ObSwitchCatalogHelper::set(uint64_t catalog_id,
                               uint64_t db_id,
                               const common::ObString& database_name,
                               sql::ObBasicSessionInfo* session_info) {
  int ret = OB_SUCCESS;
  old_catalog_id_ = catalog_id;
  old_db_id_ = db_id;
  session_info_ = session_info;
  OZ(old_database_name_.assign(database_name));
  return ret;
}

int ObSwitchCatalogHelper::restore() {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected null", K(ret));
  } else if (OB_FAIL(session_info_->set_default_catalog_db(old_catalog_id_,
                                                           old_db_id_,
                                                           old_database_name_.string()))) {
    LOG_WARN("failed to restore catalog and db", K(ret));
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
