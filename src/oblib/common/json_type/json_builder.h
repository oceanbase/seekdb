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

#include "lib/string/ob_string.h"
#include <memory>

namespace oceanbase {
namespace common {

class ObIAllocator;
class ObIJsonBase;
namespace json { struct BuildResult; }

// The allocator owns both the copied bytes and the integration view.
int copy_json_binary(ObIAllocator &allocator, const ObString &binary, ObIJsonBase *&value);
int make_json_integer(ObIAllocator &allocator, int64_t integer, ObIJsonBase *&value);
int make_json_string(ObIAllocator &allocator, const ObString &text, ObIJsonBase *&value);

namespace number { class ObNumber; }
struct ObTime;
enum class ObJsonNodeType;
int encode_json_null(ObIAllocator &, ObString &);
int encode_json_integer(ObIAllocator &, int64_t, ObString &);
int encode_json_unsigned(ObIAllocator &, uint64_t, ObString &);
int encode_json_real(ObIAllocator &, double, ObString &);
int encode_json_boolean(ObIAllocator &, bool, ObString &);
int encode_json_text(ObIAllocator &, const ObString &, ObString &);
int encode_json_opaque(ObIAllocator &, const ObString &, uint16_t field_type, ObString &);
int encode_json_decimal(ObIAllocator &, const number::ObNumber &, int16_t precision,
                        int16_t scale, ObString &);
int encode_json_time(ObIAllocator &, ObJsonNodeType, const ObTime &, ObString &);

class JsonContainerBuilder {
public:
  explicit JsonContainerBuilder(bool object);
  ~JsonContainerBuilder();
  JsonContainerBuilder(const JsonContainerBuilder &) = delete;
  JsonContainerBuilder &operator=(const JsonContainerBuilder &) = delete;
  int append(const ObString &key, const ObString &binary);
  int append_encoded(const ObString &key, json::BuildResult encoded);
  int append_aggregate(const ObString &key, const ObString &binary);
  int extend(const ObString &binary);
  int rename_key(const ObString &old_key, const ObString &new_key);
  int append_integer(const ObString &key, int64_t integer);
  int append_string(const ObString &key, const ObString &text);
  int append_container(const ObString &key, JsonContainerBuilder &value);
  int finish(ObString &binary, bool aggregate = false, bool sort_keys = true,
             bool unique_keys = true, uint32_t max_depth = 0);
  int format(ObString &text);
  uint64_t approximate_length() const;
  uint64_t input_count() const;
  uint64_t output_count() const;
private:
  class State;
  std::unique_ptr<State> state_;
};

} // namespace common
} // namespace oceanbase
