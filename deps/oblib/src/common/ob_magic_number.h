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

#ifndef OCEANBASE_COMMON_MAGIC_NUMBER_H_
#define OCEANBASE_COMMON_MAGIC_NUMBER_H_

#include "lib/ob_define.h"

namespace oceanbase
{
namespace common
{
//block sstable
const int64_t BLOCKSSTABLE_SUPER_BLOCK_MAGIC = 1000;
const int64_t BLOCKSSTABLE_COMMON_HEADER_MAGIC = 1001;
const int64_t BLOCKSSTABLE_MACRO_BLOCK_HEADER_MAGIC = 1002;
const int64_t BLOCKSSTABLE_SCHEMA_INDEX_HEADER_MAGIC = 1003;
const int64_t BLOCKSSTABLE_TABLET_IMAGE_HEADER_MAGIC = 1004;
const int64_t BLOCKSSTABLE_MICRO_BLOCK_DATA_MAGIC = 1005;
const int64_t BLOCKSSTABLE_COMPRESSOR_NAME_HEADER_MAGIC = 1006;

}//end namespace common
}//end namespace oceanbase

#endif
