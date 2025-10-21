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

#ifndef OCEANBASE_STORAGE_OB_STORAGE_LOG_BATCH_HEADER_H_
#define OCEANBASE_STORAGE_OB_STORAGE_LOG_BATCH_HEADER_H_

#include <inttypes.h>
#include "lib/ob_define.h"
#include "lib/utility/serialization.h"
#include "lib/checksum/ob_crc64.h"
#include "lib/utility/utility.h"

namespace oceanbase
{
namespace storage
{
struct ObStorageLogBatchHeader
{
  int16_t magic_;
  int16_t version_;
  int16_t header_len_;
  int16_t cnt_;
  int32_t rez_;
  int32_t total_len_;
  uint64_t checksum_;

  static const int16_t MAGIC_NUMBER = static_cast<int16_t>(0xAABBL);
  static const int16_t HEADER_VERSION = 1;

  ObStorageLogBatchHeader();
  ~ObStorageLogBatchHeader();

  TO_STRING_KV(K_(magic),
               K_(cnt),
               K_(total_len),
               K_(checksum))

  // calculate data's checksum
  uint64_t cal_checksum(const char *log_data, const int32_t data_len);
  // check data integrity
  int check_data(const char *data);
  // check batch header integrity
  int check_batch_header();

  NEED_SERIALIZE_AND_DESERIALIZE;
};
}
}

#endif
