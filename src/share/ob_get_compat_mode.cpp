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
#include "ob_get_compat_mode.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;

ObCompatModeGetter::ObCompatModeGetter()
{
}

ObCompatModeGetter::~ObCompatModeGetter()
{
  destroy();
}

ObCompatModeGetter &ObCompatModeGetter::instance()
{
  static ObCompatModeGetter the_compat_mode_getter;
  return the_compat_mode_getter;
}

int ObCompatModeGetter::get_tenant_mode(const uint64_t tenant_id, lib::Worker::CompatMode& mode)
{
  return instance().get_tenant_compat_mode(tenant_id, mode);
}

int ObCompatModeGetter::get_table_compat_mode(
    const uint64_t tenant_id,
    const int64_t table_id,
    lib::Worker::CompatMode& mode)
{
  int ret = OB_SUCCESS;
  ret = instance().get_tenant_compat_mode(tenant_id, mode);
  return ret;
}

int ObCompatModeGetter::get_tablet_compat_mode(
    const uint64_t tenant_id,
    const common::ObTabletID &tablet_id,
    lib::Worker::CompatMode& mode)
{
  int ret = OB_SUCCESS;
  ret = instance().get_tenant_compat_mode(tenant_id, mode);
  return ret;
}

int ObCompatModeGetter::check_is_oracle_mode_with_tenant_id(const uint64_t tenant_id, bool &is_oracle_mode)
{
  int ret = OB_SUCCESS;
  is_oracle_mode = false;
  return ret;
}

int ObCompatModeGetter::check_is_oracle_mode_with_table_id(
    const uint64_t tenant_id,
    const int64_t table_id,
    bool &is_oracle_mode)
{
  int ret = OB_SUCCESS;
  is_oracle_mode = false;
  return ret;
}

int ObCompatModeGetter::init(common::ObMySQLProxy *)
{
  int ret = OB_SUCCESS;
  return ret;
}


void ObCompatModeGetter::destroy()
{
}

int ObCompatModeGetter::get_tenant_compat_mode(const uint64_t tenant_id, lib::Worker::CompatMode &mode)
{
  int ret = OB_SUCCESS;
  mode = lib::Worker::CompatMode::MYSQL;
  return ret;
}

// only for unittest
