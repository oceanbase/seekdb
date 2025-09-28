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

#include "ob_macro_block_meta_mgr.h"

namespace oceanbase
{
namespace blocksstable
{
/**
 * ------------------------ObMajorMacroBlockKey-----------------------------
 */

void ObMajorMacroBlockKey::reset()
{
  table_id_ = 0;
  partition_id_ = -1;
  data_version_ = 0;
  data_seq_ = -1;
}


}
}
