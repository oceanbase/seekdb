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

#define USING_LOG_PREFIX STORAGE

#include "ob_tablet_table_store_flag.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace storage
{

/* ObTabletTableStoreFlag */

ObTabletTableStoreFlag::ObTabletTableStoreFlag()
  : with_major_sstable_(ObTabletTableStoreWithMajorFlag::WITH_MAJOR_SSTABLE),
    is_user_data_table_(false),
    reserved_()
{
}

ObTabletTableStoreFlag::~ObTabletTableStoreFlag()
{
}

void ObTabletTableStoreFlag::reset()
{
  with_major_sstable_ = ObTabletTableStoreWithMajorFlag::WITH_MAJOR_SSTABLE;
  is_user_data_table_ = false;
}

int ObTabletTableStoreFlag::serialize(char *buf, const int64_t len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;

  if (OB_ISNULL(buf) || OB_UNLIKELY(len <= 0) || OB_UNLIKELY(pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_FAIL(serialization::encode_i64(buf, len, new_pos, status_))) {
    LOG_WARN("serialize ha status failed.", K(ret), K(new_pos), K(len), K_(status), K(*this));
  } else {
    pos = new_pos;
  }
  return ret;
}

int ObTabletTableStoreFlag::deserialize(const char *buf, const int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t new_pos = pos;

  if (OB_ISNULL(buf) || OB_UNLIKELY(len <= 0) || OB_UNLIKELY(pos < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(buf), K(len), K(pos));
  } else if (OB_FAIL(serialization::decode_i64(buf, len, new_pos, &status_))) {
    LOG_WARN("failed to deserialize ha status", K(ret), K(len), K(new_pos));
  } else {
    pos = new_pos;
  }
  return ret;
}

int64_t ObTabletTableStoreFlag::get_serialize_size() const
{
  return serialization::encoded_length_i64(status_);
}

} // end namespace storage
} // end namespace oceanbase
