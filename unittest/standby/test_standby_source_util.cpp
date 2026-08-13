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

#include "standby/ob_standby_source_util.h"
#include <gtest/gtest.h>

namespace oceanbase
{
namespace standby
{

using namespace common;

TEST(TestStandbySourceUtil, parse_supported_service_sources)
{
  ObAddr addr;
  const ObAddr first(ObAddr::IPV4, "127.0.0.1", 2882);

  ASSERT_EQ(OB_SUCCESS, StandbySourceParser::get_first_service_addr(
      ObString::make_string("127.0.0.1:2882"), addr));
  ASSERT_EQ(first, addr);

  ASSERT_EQ(OB_SUCCESS, StandbySourceParser::get_first_service_addr(
      ObString::make_string(
          " service=127.0.0.1:2882;127.0.0.2:2882 USER=standby PASSWORD=secret "),
      addr));
  ASSERT_EQ(first, addr);
}

TEST(TestStandbySourceUtil, reject_non_service_sources)
{
  ObAddr addr;

  ASSERT_EQ(OB_ENTRY_NOT_EXIST, StandbySourceParser::get_first_service_addr(
      ObString::make_string("  "), addr));
  ASSERT_NE(OB_SUCCESS, StandbySourceParser::get_first_service_addr(
      ObString::make_string("LOCATION=file:///data/archive"), addr));
  ASSERT_NE(OB_SUCCESS, StandbySourceParser::get_first_service_addr(
      ObString::make_string("RAWPATH=/data/archive"), addr));
  ASSERT_NE(OB_SUCCESS, StandbySourceParser::get_first_service_addr(
      ObString::make_string("s3://bucket/archive"), addr));
}

} // namespace standby
} // namespace oceanbase
