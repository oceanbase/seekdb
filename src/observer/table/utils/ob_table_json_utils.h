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

#ifndef OCEANBASE_OBSERVER_OB_TABLE_JSON_UTILS_H_
#define OCEANBASE_OBSERVER_OB_TABLE_JSON_UTILS_H_

#include "lib/json/ob_json.h"
#include "share/table/ob_table.h"

namespace oceanbase
{
namespace table
{

class ObTableJsonUtils
{
public:
  static int parse(ObIAllocator &allocator, const ObString &json_str, json::Value *&root);
  static int get_json_value(json::Value *root, const ObString &name, json::Type expect_type, json::Value *&value);
  // deep copy
  static int serialize(ObIAllocator &allocator, json::Value *root, ObString &dst);

private:
  static const uint64_t BUFFER_SIZE = 65535;
};

class ObTableJsonArrayBuilder;

class ObTableJsonObjectBuilder
{
public:
  ObTableJsonObjectBuilder(ObIAllocator &allocator);
  int init();
  int add(const char* key, const int64_t key_len, int64_t value);
  int add(const char* key, const int64_t key_len, const ObString &value);
  int add(const char* key, const int64_t key_len, json::Value *value);
  int add(const char* key, const int64_t key_len, ObTableJsonObjectBuilder &obj);
  json::Value* build();

private:
  ObIAllocator &allocator_;
  json::Value *root_;

  DISALLOW_COPY_AND_ASSIGN(ObTableJsonObjectBuilder);
};


class ObTableJsonArrayBuilder
{
public:
  ObTableJsonArrayBuilder(ObIAllocator &allocator);
  int init();
  int add(int64_t value);
  int add(const ObString &value);
  int add(ObTableJsonArrayBuilder &arr);
  json::Value* build();

private:
  ObIAllocator &allocator_;
  json::Value *root_;

  DISALLOW_COPY_AND_ASSIGN(ObTableJsonArrayBuilder);
};

} // end namespace table
} // end namespace oceanbase

#endif // OCEANBASE_OBSERVER_OB_TABLE_JSON_UTILS_H_
