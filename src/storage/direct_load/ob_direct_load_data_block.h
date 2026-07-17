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
#pragma once

#include "lib/utility/ob_print_utils.h"
#include "share/ob_errno.h"

namespace oceanbase
{
namespace storage
{

class ObDirectLoadDataBlock
{
public:
  static const int64_t DEFAULT_DATA_BLOCK_SIZE = 128 * 1024; // 128K
  struct Header
  {
    OB_UNIS_VERSION(1);
  public:
    Header();
    ~Header();
    void reset();
    TO_STRING_KV(K_(occupy_size), K_(data_size), K_(checksum));
  public:
    int32_t occupy_size_; // occupy size of data block, include header
    int32_t data_size_; // size of raw data, include header
    int64_t checksum_; // checksum of valid data
  };
public:
};

} // namespace storage
} // namespace oceanbase
