/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
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
