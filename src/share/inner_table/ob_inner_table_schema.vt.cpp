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

#define USING_LOG_PREFIX SHARE_SCHEMA
#include "ob_inner_table_schema.h"

namespace oceanbase
{
namespace share
{
VTMapping vt_mappings[5000];
bool vt_mapping_init()
{
   int64_t start_idx = common::OB_MAX_MYSQL_VIRTUAL_TABLE_ID + 1;
   return true;
} // end define vt_mappings

bool inited_vt = vt_mapping_init();

} // end namespace share
} // end namespace oceanbase
