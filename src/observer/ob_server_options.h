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

#pragma once

#include "lib/string/ob_string.h"
#include "lib/string/ob_sql_string.h"
#include "lib/container/ob_array.h"
#include "lib/allocator/ob_malloc.h"

namespace oceanbase {
namespace observer {

/**
 * 记录命令行参数
 */
class ObServerOptions final
{
public:
  ObServerOptions() {}
  ~ObServerOptions() {}

public:
  using KeyValuePair = std::pair<common::ObString, common::ObString>;
  using KeyValueArray = common::ObArray<KeyValuePair>;

public:
  int     port_        = 0;
  int8_t  log_level_   = 0;
  bool    nodaemon_    = false;
  bool    use_ipv6_    = false;
  bool    initialize_  = false;
  bool    embed_mode_  = false;

  common::ObSqlString base_dir_;
  common::ObSqlString data_dir_;
  common::ObSqlString redo_dir_;
  KeyValueArray       parameters_;
  KeyValueArray       variables_;
  const char *        devname_ = nullptr;

private:
  DISALLOW_COPY_AND_ASSIGN(ObServerOptions);
};

} // end of namespace observer
} // end of namespace oceanbase