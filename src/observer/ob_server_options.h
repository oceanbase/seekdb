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
