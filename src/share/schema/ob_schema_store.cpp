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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_schema_store.h"
#include "share/inner_table/ob_inner_table_schema.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
int ObSchemaStore::init(const int64_t init_version_count)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(schema_mgr_cache_.init(init_version_count))) {
    LOG_WARN("init schema_mgr_cache fail", K(ret));
  } else {
    refreshed_version_ = OB_CORE_SCHEMA_VERSION;
    received_version_ = OB_CORE_SCHEMA_VERSION;
    checked_sys_version_ = OB_INVALID_VERSION;
    baseline_schema_version_ = OB_INVALID_VERSION;
    LOG_INFO("[SCHEMA_STORE] schema store init");
  }
  return ret;
}

void ObSchemaStore::reset_version()
{
  refreshed_version_ = OB_INVALID_VERSION;
  received_version_ = OB_INVALID_VERSION;
  checked_sys_version_ = OB_INVALID_VERSION;
  baseline_schema_version_ = OB_INVALID_VERSION;
  LOG_INFO("[SCHEMA_STORE] schema store reset version");
}

void ObSchemaStore::update_refreshed_version(int64_t version)
{
  if (version > refreshed_version_ || version > received_version_) {
    inc_update(&refreshed_version_, version);
    inc_update(&received_version_, version);
    LOG_INFO("[SCHEMA_STORE] schema store update version",
             K(version), K_(refreshed_version), K_(received_version));
  }
}

void ObSchemaStore::update_received_version(int64_t version)
{
  if (version > received_version_) {
    inc_update(&received_version_, version);
    LOG_INFO("[SCHEMA_STORE] schema store update version",
             K(version), K_(received_version));
  }
}

void ObSchemaStore::update_checked_sys_version(int64_t version)
{
  if (version > checked_sys_version_) {
    inc_update(&checked_sys_version_, version);
    LOG_INFO("[SCHEMA_STORE] schema store update version",
             K(version), K_(checked_sys_version));
  }
}

void ObSchemaStore::update_baseline_schema_version(int64_t version)
{
  if (version > baseline_schema_version_) {
    inc_update(&baseline_schema_version_, version);
    LOG_INFO("[SCHEMA_STORE] schema store update version",
             K(version), K_(baseline_schema_version));
  }
}

}; // end namespace schema
}; // end namespace share
}; // end namespace oceanbase
