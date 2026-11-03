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

#include "ob_json_schema.h"
#include "ob_json_bin.h"
#include "rust/json.h"

namespace oceanbase {
namespace common {

static int json_schema_status_error(json::Status status, int platform_error = 0)
{
  if (platform_error != 0) { return platform_error; }
  switch (status) {
    case json::Status::Ok: return OB_SUCCESS;
    case json::Status::InvalidSchema: return OB_ERR_TYPE_OF_JSON_SCHEMA;
    case json::Status::UnsupportedComparison: return OB_OP_NOT_ALLOW;
    case json::Status::InvalidSchemaMap: return OB_ERR_WRONG_VALUE;
    case json::Status::MissingSchema: return OB_BAD_NULL_ERROR;
    case json::Status::SchemaObjectType: return OB_OBJ_TYPE_ERROR;
    case json::Status::SchemaArgument: return OB_INVALID_ARGUMENT;
    case json::Status::SchemaCompositionFailure: return 1;
    case json::Status::InvalidSchemaIndex: return OB_INVALID_INDEX;
    case json::Status::UnsupportedSchemaRef: return OB_ERR_UNSUPPROTED_REF_IN_JSON_SCHEMA;
    case json::Status::DuplicateKey: return OB_ERR_DUPLICATE_KEY;
    case json::Status::NumericOverflow: return OB_NUMERIC_OVERFLOW;
    case json::Status::PatternFailure: return OB_ALLOCATE_MEMORY_FAILED;
    case json::Status::Depth:
    case json::Status::Syntax: return OB_ERR_INVALID_JSON_TEXT;
    default: return OB_ERR_UNEXPECTED;
  }
}

int compile_json_schema(ObIAllocator &allocator, const ObString &input,
                        ObJsonInType input_type, uint32_t max_depth, ObString &output)
{
  int ret = OB_SUCCESS;
  const auto compiled = json::compile_schema(rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(input.ptr()), input.length()),
      input_type == ObJsonInType::JSON_BIN, max_depth);
  if (OB_FAIL(json_schema_status_error(compiled.status, compiled.platform_error))) {
  } else {
    ObString bytes(compiled.binary.size(), reinterpret_cast<const char *>(compiled.binary.data()));
    ret = ob_write_string(allocator, bytes, output);
  }
  return ret;
}

struct ObJsonSchemaCache::State
{
  State() : cache_(json::new_schema_cache()) {}
  rust::Box<json::SchemaCache> cache_;
};

ObJsonSchemaCache::~ObJsonSchemaCache()
{
  if (state_ != nullptr) {
    state_->~State();
    allocator_->free(state_);
    state_ = nullptr;
  }
}

int ObJsonSchemaCache::prepare(const ObString &input, int slot, ObJsonInType input_type, bool normalized)
{
  if (slot < 0) {
    return OB_INVALID_INDEX;
  } else if (input.empty() || input.ptr() == nullptr) {
    return OB_ERR_NULL_VALUE;
  } else if (allocator_ == nullptr) {
    return OB_BAD_NULL_ERROR;
  }
  if (state_ == nullptr) {
    void *memory = allocator_->alloc(sizeof(State));
    if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    state_ = new (memory) State();
  }
  const auto result = state_->cache_->prepare(slot, rust::Slice<const uint8_t>(
      reinterpret_cast<const uint8_t *>(input.ptr()), input.length()),
      input_type == ObJsonInType::JSON_BIN, normalized);
  return json_schema_status_error(result.status, result.platform_error);
}

int ObJsonSchemaCache::validate(const ObString &input, int slot, bool with_report, bool &valid,
                               ObString &report, ObIAllocator &allocator) const
{
  int ret = OB_SUCCESS;
  valid = false;
  report.reset();
  if (state_ == nullptr || slot < 0) {
    ret = OB_INVALID_INDEX;
  } else {
    const auto result = state_->cache_->validate_schema(slot, rust::Slice<const uint8_t>(
        reinterpret_cast<const uint8_t *>(input.ptr()), input.length()), with_report);
    if (OB_FAIL(json_schema_status_error(result.status, result.platform_error))) {
    } else if (with_report && OB_FAIL(ob_write_string(allocator,
        ObString(result.report.size(), reinterpret_cast<const char *>(result.report.data())), report))) {
    } else {
      valid = result.valid;
    }
  }
  return ret;
}

} // namespace common
} // namespace oceanbase
