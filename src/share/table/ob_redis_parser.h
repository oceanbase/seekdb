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

#ifndef OCEANBASE_SHARE_TABLE_OB_REDIS_PARSER_H_
#define OCEANBASE_SHARE_TABLE_OB_REDIS_PARSER_H_

#include "src/share/table/ob_redis_common.h"
#include "lib/string/ob_string.h"
#include "lib/container/ob_array.h"
#include "lib/string/ob_string_buffer.h"

namespace oceanbase
{
namespace table
{
class ObRedisParser
{
public:
  /**
   * @brief Decodes Redis messages
   *
   * @param redis_msg Redis message
   * @param row_strs decoded array of row strings
   * @return int Returns 0 for success and any other value for failure
   */
  static int decode(
      ObIAllocator &allocator,
      const ObString &redis_msg,
      ObString &cmd_name,
      ObArray<ObString> &args);

  /**
   * @brief encodes the error message
   * @param err_msg error message
   * @param encoded_msg encoded message
   * @return Returns the encoded result, 0 on success or some other value on failure
   */
  static int encode_error(ObIAllocator &allocator, const ObString &err_msg, ObString &encoded_msg);
  /**
   * @brief encodes a simple string
   * @param simpe_str Simple string
   * @param encoded_msg encoded message
   * @return Returns the encoded result, 0 on success or some other value on failure
   */
  static int encode_simple_string(
      ObIAllocator &allocator,
      const ObString &simpe_str,
      ObString &encoded_msg);
  /**
   * @brief encodes long strings
   * @param bulk_str Long string
   * @param encoded_msg encoded message
   * @return Returns the encoded result, 0 on success or some other value on failure
   */
  static int encode_bulk_string(
      ObIAllocator &allocator,
      const ObString &bulk_str,
      ObString &encoded_msg);
  /**
   * @brief encodes integers
   * @param integer Integer
   * @param encoded_msg encoded message
   * @return Returns the encoded result, 0 on success or some other value on failure
   */
  static int encode_integer(ObIAllocator &allocator, const int64_t integer, ObString &encoded_msg);

  static int encode_array(
      ObIAllocator &allocator,
      const ObIArray<ObString> &array,
      ObString &encoded_msg);

private:
  static int encode_with_flag(const char flag, const ObString &msg, ObStringBuffer &buffer);
  static int inner_encode_bulk_string(const ObString &bulk_str, ObStringBuffer &buffer);
  DISALLOW_COPY_AND_ASSIGN(ObRedisParser);
};

class ObRedisDecoder
{
public:
  ObRedisDecoder(ObIAllocator &allocator, const ObString &redis_msg)
      : redis_msg_(redis_msg.ptr()),
        length_(redis_msg.length()),
        cur_pos_(0),
        args_(OB_MALLOC_NORMAL_BLOCK_SIZE, ModulePageAllocator(allocator, "RedisDecode")),
        allocator_(&allocator)
  {}
  ~ObRedisDecoder() {}

  int decode();

  inline int get_result(ObString &cmd_name, ObArray<ObString> &args);

  TO_STRING_KV(K(length_), K(cur_pos_));

private:
  int read_until_crlf(ObString &splited);
  int decode_bulk_string(ObString &bulk_str);
  int decode_array(const ObString &header);

  const char *redis_msg_;
  int32_t length_;
  int32_t cur_pos_;
  ObString cmd_name_;
  ObArray<ObString> args_;
  ObIAllocator *allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObRedisDecoder);
};

}  // end namespace table
}  // end namespace oceanbase

#endif /* OCEANBASE_SHARE_TABLE_OB_REDIS_PARSER_H_ */
