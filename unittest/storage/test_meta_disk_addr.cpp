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

#define private public
#include "storage/meta_mem/ob_meta_obj_struct.h"
#include "storage/tablet/ob_tablet.h"
#undef private

namespace oceanbase
{
namespace storage
{
namespace unittest
{

TEST(MetaDiskAddr, RejectsInvalidAndDecodesValidAddresses)
{
  int64_t file_id = -1;
  int64_t offset = 0;
  int64_t size = 0;
  blocksstable::MacroBlockId macro_id;

  ObMetaDiskAddr none_addr;
  EXPECT_FALSE(none_addr.is_valid());
  none_addr.set_none_addr();
  EXPECT_TRUE(none_addr.is_valid());
  EXPECT_EQ(ObMetaDiskAddr::DiskType::NONE, none_addr.type_);
  EXPECT_EQ(OB_NOT_SUPPORTED, none_addr.get_block_addr(macro_id, offset, size));
  EXPECT_EQ(OB_NOT_SUPPORTED, none_addr.get_file_addr(file_id, offset, size));
  EXPECT_EQ(OB_NOT_SUPPORTED, none_addr.get_mem_addr(offset, size));

  ObMetaDiskAddr file_addr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, file_addr.set_file_addr(-1, 0, sizeof(ObTablet)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, file_addr.set_file_addr(1, -1, sizeof(ObTablet)));
  EXPECT_EQ(
      OB_INVALID_ARGUMENT,
      file_addr.set_file_addr(
          1, ObMetaDiskAddr::MAX_OFFSET + 1, sizeof(ObTablet)));
  ASSERT_EQ(
      OB_SUCCESS,
      file_addr.set_file_addr(1, 0, sizeof(ObTablet)));
  EXPECT_TRUE(file_addr.is_valid());
  EXPECT_EQ(OB_SUCCESS, file_addr.get_file_addr(file_id, offset, size));
  EXPECT_EQ(1, file_id);
  EXPECT_EQ(0, offset);
  EXPECT_EQ(sizeof(ObTablet), size);

  ObMetaDiskAddr block_addr;
  EXPECT_EQ(
      OB_INVALID_ARGUMENT,
      block_addr.set_block_addr(
          macro_id, 0, sizeof(ObTablet), ObMetaDiskAddr::DiskType::BLOCK));
  macro_id.block_index_ = 100;
  ASSERT_EQ(
      OB_SUCCESS,
      block_addr.set_block_addr(
          macro_id, 0, sizeof(ObTablet), ObMetaDiskAddr::DiskType::BLOCK));
  EXPECT_TRUE(block_addr.is_valid());
  EXPECT_EQ(OB_SUCCESS, block_addr.get_block_addr(macro_id, offset, size));
  EXPECT_EQ(0, offset);
  EXPECT_EQ(sizeof(ObTablet), size);

  ObMetaDiskAddr mem_addr;
  EXPECT_EQ(
      OB_INVALID_ARGUMENT,
      mem_addr.set_mem_addr(ObMetaDiskAddr::MAX_OFFSET + 1, sizeof(ObTablet)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, mem_addr.set_mem_addr(0, -1));
  ASSERT_EQ(OB_SUCCESS, mem_addr.set_mem_addr(0, sizeof(ObTablet)));
  EXPECT_TRUE(mem_addr.is_valid());
  EXPECT_EQ(OB_SUCCESS, mem_addr.get_mem_addr(offset, size));
  EXPECT_EQ(0, offset);
  EXPECT_EQ(sizeof(ObTablet), size);
}

} // namespace unittest
} // namespace storage
} // namespace oceanbase
