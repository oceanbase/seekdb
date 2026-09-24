/*
 * Copyright (c) 2026 OceanBase.
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

#include "sql/parser/parse_node.h"
#include "lib/ob_errno.h"
#include <new>
#include <string>
#include <utility>

namespace oceanbase { namespace sql {

// Owned declaration, not a catalog object or an activation capability. AS uses
// logical module/implementation IDs in SeekDB's stable C ABI, never a library
// path or a C++ address. SQL name/signature remain in the CREATE FUNCTION tree.
// No function lookup by SQL alias, DSO load, schema write or privilege check is
// performed here. The install coordinator must admit all of those separately.
struct NativeFunctionDeclaration final
{
  std::string module_id_;
  std::string implementation_id_;

  static bool is_native(const ParseNode *body)
  { return body != nullptr && body->type_ == T_SF_NATIVE_BODY; }

  // Output is reset even on malformed trees/unsupported languages. Own strings
  // before parser arenas or package source buffers are released.
  static int read(const ParseNode *body, NativeFunctionDeclaration &output)
  {
    output = {};
    if (!is_native(body) || body->num_child_ != 3 || body->children_ == nullptr) {
      return common::OB_INVALID_ARGUMENT;
    }
    const auto *module = body->children_[0];
    const auto *implementation = body->children_[1];
    const auto *language = body->children_[2];
    if (module == nullptr || implementation == nullptr || language == nullptr ||
        module->type_ != T_VARCHAR || implementation->type_ != T_VARCHAR ||
        language->type_ != T_IDENT || language->str_value_ == nullptr ||
        language->str_len_ <= 0) return common::OB_INVALID_ARGUMENT;
    // LANGUAGE C identifies the calling ABI; Rust implementations can expose
    // the same ABI. Do not silently accept another procedural language.
    if (language->str_len_ != 1 || (language->str_value_[0] != 'c' && language->str_value_[0] != 'C')) {
      return common::OB_NOT_SUPPORTED;
    }
    if (!valid_id(module) || !valid_id(implementation)) return common::OB_INVALID_ARGUMENT;
    try {
      NativeFunctionDeclaration candidate;
      candidate.module_id_.assign(module->str_value_, module->str_len_);
      candidate.implementation_id_.assign(implementation->str_value_, implementation->str_len_);
      output = std::move(candidate);
      return common::OB_SUCCESS;
    } catch (const std::bad_alloc &) {
      return common::OB_ALLOCATE_MEMORY_FAILED;
    }
  }

private:
  static bool valid_id(const ParseNode *node)
  {
    if (node->str_value_ == nullptr || node->str_len_ <= 0 || node->str_len_ > 255) return false;
    for (int64_t i = 0; i < node->str_len_; ++i) {
      const unsigned char value = node->str_value_[i];
      if (!((value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') ||
            value == '.' || value == '_' || value == '-')) return false;
    }
    return true;
  }
};

} }
