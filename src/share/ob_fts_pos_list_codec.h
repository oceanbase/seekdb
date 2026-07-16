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

#ifndef OCEANBASE_SHARE_OB_FTS_POS_LIST_CODEC_H_
#define OCEANBASE_SHARE_OB_FTS_POS_LIST_CODEC_H_

#include "lib/container/ob_array.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{

class ObFTSPositionListStore final
{
public:
  static const int16_t MAGIC_NUMBER = static_cast<int16_t>(0xFACE);
  static const int16_t VERSION = 1;
  static const int64_t MAX_INLINE_ENCODED_LENGTH = 16 * 1024;
  enum CodecType : int16_t
  {
    VARIABLE_INT64 = 0,
    DELTA_ZIGZAG_PFOR = 1,
  };

  static int encode(
      const common::ObIArray<int64_t> &pos_list,
      common::ObIAllocator &allocator,
      common::ObString &encoded_pos_list);
  static int decode(
      const common::ObString &encoded_pos_list,
      common::ObArray<int64_t, common::ObIAllocator &> &pos_list);

  static int encode_with_variable_int64(
      const common::ObIArray<int64_t> &pos_list,
      common::ObIAllocator &allocator,
      common::ObString &payload);
  static int decode_with_variable_int64(
      const common::ObString &payload,
      common::ObArray<int64_t, common::ObIAllocator &> &pos_list);

  static int encode_with_delta_zigzag_pfor(
      const common::ObIArray<int64_t> &pos_list,
      common::ObIAllocator &allocator,
      common::ObString &payload);
  static int decode_with_delta_zigzag_pfor(
      const common::ObString &payload,
      common::ObArray<int64_t, common::ObIAllocator &> &pos_list);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_OB_FTS_POS_LIST_CODEC_H_
