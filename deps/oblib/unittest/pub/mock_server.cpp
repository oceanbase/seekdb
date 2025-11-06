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

#include "mock_server.h"

using namespace oceanbase::unittest::pub;
using namespace oceanbase::common;
using namespace oceanbase::observer;

int main()
{
  MockServer s;
  if (OB_SUCCESS == s.init(25000)) {
    s.start();
    // s.wait_for();
  } else {
    OB_LOG(ERROR, "init fail");
  }
  sleep(5);
  return 0;
}

namespace oceanbase
{
namespace unittest
{
namespace pub
{



}
}
}
