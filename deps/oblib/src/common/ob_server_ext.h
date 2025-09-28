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

#ifndef __OCEANBASE_ZONEMANAGER_OB_SERVER_EXT_H__
#define __OCEANBASE_ZONEMANAGER_OB_SERVER_EXT_H__

#include "lib/net/ob_addr.h"

namespace oceanbase
{
namespace common
{
class ObServerExt
{
public:
  friend class ObOcmInstance;
  ObServerExt();
  ~ObServerExt();

private:
  char hostname_[OB_MAX_HOST_NAME_LENGTH];
  ObAddr server_;
  int64_t magic_num_;
};
}
}
#endif
