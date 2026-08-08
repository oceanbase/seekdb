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

#ifndef OB_STORAGE_BLOCKSSTABLE_STORAGE_DATUM_H
#define OB_STORAGE_BLOCKSSTABLE_STORAGE_DATUM_H

#include "data_plane/blocksstable/ob_storage_datum.h"
#include "data_plane/blocksstable/ob_storage_datum_utils.h"

namespace oceanbase
{
namespace blocksstable
{

class ObStorageDatumWrapper final
{
public:
  ObStorageDatumWrapper(const ObStorageDatum &datum, bool for_dump)
    : datum_(datum),
      for_dump_(for_dump)
  {}
  ~ObStorageDatumWrapper() = default;
  OB_INLINE int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    if (nullptr != buf && buf_len > 0) {
      pos = datum_.storage_to_string(buf, buf_len - 1, for_dump_);
      if (pos >= 0 && pos < buf_len) {
        buf[pos] = '\0';
      }
    }
    return pos;
  }
private:
  DISABLE_COPY_ASSIGN(ObStorageDatumWrapper);
  const ObStorageDatum &datum_;
  const bool for_dump_;
};

} // namespace blocksstable
} // namespace oceanbase

#endif // OB_STORAGE_BLOCKSSTABLE_STORAGE_DATUM_H
