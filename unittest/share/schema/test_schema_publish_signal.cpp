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

#include <gtest/gtest.h>

#include "share/schema/ob_schema_publish_signal.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

TEST(TestSchemaPublishSignal, notification_is_durable)
{
  ObSchemaPublishSignal signal;
  ASSERT_EQ(common::OB_SUCCESS, signal.init());

  int64_t observed_epoch = signal.current_epoch();
  signal.notify_schema_published();

  ASSERT_EQ(common::OB_SUCCESS, signal.wait_after(observed_epoch, 1));
  ASSERT_EQ(signal.current_epoch(), observed_epoch);
  ASSERT_GT(observed_epoch, 0);
}

} // namespace schema
} // namespace share
} // namespace oceanbase
