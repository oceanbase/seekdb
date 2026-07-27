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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_ENGINE_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_LOG_ENGINE_

#include "logservice/palf/log_engine.h"

namespace oceanbase
{
namespace palf
{

class MockLogEngine : public LogEngine
{
public:
  MockLogEngine()
    : flush_log_ret_(OB_SUCCESS),
      handle_submit_ret_(OB_SUCCESS)
  {}
  ~MockLogEngine() override = default;

  int submit_flush_log_task(const FlushLogCbCtx &flush_log_cb_ctx,
                            const LogWriteBuf &write_buf) override
  {
    UNUSEDx(flush_log_cb_ctx, write_buf);
    return flush_log_ret_;
  }

  int submit_handle_submit_task() override
  {
    return handle_submit_ret_;
  }

  int64_t get_palf_epoch() const override
  {
    return 1;
  }

public:
  int flush_log_ret_;
  int handle_submit_ret_;
};

} // namespace palf
} // namespace oceanbase

#endif
