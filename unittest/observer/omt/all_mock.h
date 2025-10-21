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

#ifndef _ALL_MOCK_H_
#define _ALL_MOCK_H_

#ifndef private
#define private public
#endif
#ifndef protected
#define protected public
#endif

#include "observer/ob_server.h"

using namespace oceanbase::storage;

void all_mock_init()
{
  static bool inited = false;
  if (!inited) {
    OBSERVER.init_global_context();
    OBSERVER.init_schema();
    OBSERVER.init_tz_info_mgr();
    GCTX.sql_engine_->plan_cache_manager_.init(GCTX.self_addr());
    GCTX.sql_engine_->plan_cache_manager_.inited_ = false;
    inited = true;
  }
}

#endif /* _ALL_MOCK_H_ */
