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

#ifndef OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_PUB_LOG_SLIDING_WINDOW_
#define OCEANBASE_UNITTEST_LOGSERVICE_MOCK_CONTAINER_PUB_LOG_SLIDING_WINDOW_

#define private public
#include "logservice/palf/log_sliding_window.h"
#undef private

namespace oceanbase
{
namespace palf
{
class PalfFSCbWrapper;

class MockPublicLogSlidingWindow : public LogSlidingWindow
{
public:
  MockPublicLogSlidingWindow()
  {}
  virtual ~MockPublicLogSlidingWindow()
  {}
};

} // namespace palf
} // namespace oceanbase

#endif
