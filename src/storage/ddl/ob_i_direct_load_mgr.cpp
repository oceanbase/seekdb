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
#define USING_LOG_PREFIX STORAGE

#include "ob_i_direct_load_mgr.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;

ObBaseTabletDirectLoadMgr::ObBaseTabletDirectLoadMgr()
 : ls_id_(), tablet_id_(), table_key_(), tenant_data_version_(), direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID), ref_cnt_(0)
{
}

ObBaseTabletDirectLoadMgr::~ObBaseTabletDirectLoadMgr()
{
  ls_id_.reset();
  tablet_id_.reset();
  table_key_.reset();
  tenant_data_version_ = 0;
  direct_load_type_ = ObDirectLoadType::DIRECT_LOAD_INVALID;
  ATOMIC_STORE(&ref_cnt_, 0);
}
