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

#define USING_LOG_PREFIX SQL
#include "ob_json_parse.h"
#include "rust/json.h"

namespace oceanbase {
namespace common {


static json::Duplicate duplicate_policy(uint32_t parse_flag)
{
  if (HAS_FLAG(parse_flag, ObJsonParser::JSN_SCHEMA_FLAG)) {
    return json::Duplicate::KeepFirst;
  } else if (HAS_FLAG(parse_flag, ObJsonParser::JSN_PRESERVE_DUP_FLAG)) {
    return json::Duplicate::Preserve;
  } else if (HAS_FLAG(parse_flag, ObJsonParser::JSN_UNIQUE_FLAG)) {
    return json::Duplicate::Reject;
  }
  return json::Duplicate::Overwrite;
}

static uint32_t depth_limit(uint32_t configured)
{
  return configured < JSON_DOCUMENT_MAX_DEPTH ? JSON_DOCUMENT_MAX_DEPTH : configured;
}

static uint32_t text_depth_limit(uint32_t configured)
{
  const uint32_t limit = depth_limit(configured);
  return limit == UINT32_MAX ? limit : limit + 1;
}

static int copy_text(ObIAllocator *allocator, const rust::Vec<uint8_t> &input, ObString &output)
{
  int ret = OB_SUCCESS;
  char *buffer = NULL;
  const uint64_t length = input.size();
  if (length == 0) {
    output.reset();
  } else if (OB_ISNULL(buffer = static_cast<char *>(allocator->alloc(length)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    MEMCPY(buffer, input.data(), length);
    output.assign_ptr(buffer, length);
  }
  return ret;
}

static int status_error(json::Status status)
{
  switch (status) {
    case json::Status::DuplicateKey: return OB_ERR_DUPLICATE_KEY;
    case json::Status::Depth: return OB_ERR_JSON_OUT_OF_DEPTH;
    case json::Status::UnsupportedSchemaRef: return OB_ERR_UNSUPPROTED_REF_IN_JSON_SCHEMA;
    default: return OB_ERR_INVALID_JSON_TEXT;
  }
}





int ObJsonParser::get_binary(ObIAllocator *allocator, const char *text,
                             uint64_t length, ObString &binary,
                             uint32_t parse_flag, uint32_t max_depth_config)
{
  binary.reset();
  if (OB_ISNULL(allocator) || OB_ISNULL(text) || length == 0) {
    return OB_ERR_NULL_VALUE;
  }
  const uint8_t *input = reinterpret_cast<const uint8_t *>(text);
  json::BinaryResult result = json::parse_binary(
      rust::Slice<const uint8_t>(input, length), duplicate_policy(parse_flag),
      text_depth_limit(max_depth_config), HAS_FLAG(parse_flag, JSN_SCHEMA_FLAG));
  if (result.status != json::Status::Ok) {
    return result.status == json::Status::Depth ? OB_ERR_INVALID_JSON_TEXT
                                                : status_error(result.status);
  }
  return copy_text(allocator, result.binary, binary);
}



int ObJsonParser::check_json_syntax(const ObString &j_doc, ObIAllocator *allocator,
                                    uint32_t parse_flag, uint32_t max_depth_config)
{
  UNUSED(allocator);
  if (j_doc.empty() || OB_ISNULL(j_doc.ptr())) {
    return OB_ERR_NULL_VALUE;
  }
  const uint8_t *input = reinterpret_cast<const uint8_t *>(j_doc.ptr());
  json::ParseResult result = json::validate(
      rust::Slice<const uint8_t>(input, j_doc.length()), duplicate_policy(parse_flag),
      depth_limit(max_depth_config), HAS_FLAG(parse_flag, JSN_SCHEMA_FLAG));
  if (result.status == json::Status::Ok) {
    return OB_SUCCESS;
  }
  LOG_DEBUG("fail to validate standard JSON text", K(result.byte_offset));
  if (HAS_FLAG(parse_flag, JSN_UNIQUE_FLAG)
      && (result.status == json::Status::DuplicateKey || result.status == json::Status::Depth)) {
    return OB_ERR_INVALID_JSON_TEXT;
  }
  return status_error(result.status);
}

} // namespace common
} // namespace oceanbase
