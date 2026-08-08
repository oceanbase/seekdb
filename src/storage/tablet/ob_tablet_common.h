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

#ifndef OCEANBASE_STORAGE_OB_TABLET_COMMON
#define OCEANBASE_STORAGE_OB_TABLET_COMMON

#include <stdint.h>
#include "lib/literals/ob_literals.h"
#include "share/tablet/ob_tablet_read_mode.h"

namespace oceanbase
{
namespace storage
{
class ObTabletCommon final
{
public:
  static const int64_t DEFAULT_ITERATOR_TABLET_ID_CNT = 128;
  static const int64_t BUCKET_LOCK_BUCKET_CNT = 10243L;
  static const int64_t DEFAULT_GET_TABLET_NO_WAIT = 0; // 0s
  static const int64_t DEFAULT_GET_TABLET_DURATION_US = 1_s;
  static const int64_t DEFAULT_GET_TABLET_DURATION_10_S = 10_s;
  static const int64_t FINAL_TX_ID = 0;
  // The length of tablet_addr contains first-level meta's length and inline-meta's length.
  // We ensures that the first-level meta's length will not exceed MAX_TABLET_FIRST_LEVEL_META_SIZE by implementation,
  // in fact, within 4k in most cases. So just use this length in the situation where only want to read first-level meta,
  // although there is some IO amplification, but avoid the trouble of recording the first-level meta's length.
  static const int64_t MAX_TABLET_FIRST_LEVEL_META_SIZE = 16 * 1024; // 16k
};
} // namespace storage
} // namespace oceanbase

#endif // OCEANBASE_STORAGE_OB_TABLET_COMMON
