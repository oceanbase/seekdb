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

#include "lib/container/ob_array_serialization.h"
#include "common/datum/datum_param_store.h"
#include <gtest/gtest.h>
#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Generic native container serialization must not import legacy values"
#endif
using namespace oceanbase::common;
TEST(DatumCodec, generic_container_header_can_precede_native_specializations)
{
  ObArenaAllocator arena;
  DatumParamStore source{ObWrapperAllocator(arena)}, output{ObWrapperAllocator(arena)};
  char wire[1]; int64_t pos = 0;
  EXPECT_EQ(1, serialization::encoded_length(source));
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire, sizeof(wire), pos, source));
  EXPECT_EQ(0, wire[0]); pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire, sizeof(wire), pos, output));
  EXPECT_EQ(1, pos); EXPECT_TRUE(output.empty());
}
