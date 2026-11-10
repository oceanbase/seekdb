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

#include <cstring>
#include <gtest/gtest.h>
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "lib/allocator/ob_allocator.h"

using namespace oceanbase::common;

namespace {
constexpr unsigned char LEGACY_PAYLOAD[] = {
  0x19,0x07,0x00,0x07,0x40,0,0,0,0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x01
};
ObDatum legacy_view()
{
  ObDatum value(reinterpret_cast<const char *>(LEGACY_PAYLOAD), sizeof(LEGACY_PAYLOAD), false);
  value.set_flag(ObDatumDesc::EXT);
  return value;
}
void assert_wire(const ObDatum &value)
{
  char wire[128];
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, value.serialize(wire, sizeof(wire), pos));
  ASSERT_EQ(27, pos);
  ASSERT_EQ(0, memcmp(wire + pos - sizeof(LEGACY_PAYLOAD), LEGACY_PAYLOAD, sizeof(LEGACY_PAYLOAD)));
}
} // namespace

TEST(StorageDatumExtend, owned_storage_sentinel_copy_detaches_inline_payload)
{
  using oceanbase::blocksstable::ObStorageDatum;
  ObStorageDatum original;
  ASSERT_TRUE(original.is_nop());
  ASSERT_TRUE(original.is_local_buf());
  ASSERT_EQ(15U, original.len_);
  original.set_min();
  ObStorageDatum copy(original);
  ASSERT_NE(original.ptr_, copy.ptr_);
  ASSERT_TRUE(copy.is_local_buf());
  original.set_max();
  ASSERT_TRUE(copy.is_min());
  ASSERT_TRUE(original.is_max());
  copy = original;
  ASSERT_TRUE(copy.is_local_buf());
  ASSERT_TRUE(copy.is_max());
  original.set_nop();
  ASSERT_TRUE(copy.is_max());
  ASSERT_TRUE(original.is_nop());
}

TEST(StorageDatumExtend, storage_deep_copy_accepts_legacy_byte_views)
{
  using oceanbase::blocksstable::ObStorageDatum;
  const ObDatum old = legacy_view();
  ObStorageDatum borrowed;
  borrowed.ptr_ = old.ptr_;
  borrowed.pack_ = old.pack_;
  ObStorageDatum owned;
  ObArenaAllocator alloc;
  ASSERT_EQ(OB_SUCCESS, owned.deep_copy(borrowed, alloc));
  ASSERT_NE(old.ptr_, owned.ptr_);
  ASSERT_EQ(old.get_ext(), owned.get_ext());
  ASSERT_EQ(old.get_extend_type(), owned.get_extend_type());
  ASSERT_EQ(old.get_extend_size(), owned.get_extend_size());
  ASSERT_EQ(old.checksum(0), owned.checksum(0));
  assert_wire(owned);
}


int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
