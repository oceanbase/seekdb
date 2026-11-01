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

using JsonEvent = json::Event;
using JsonEvents = rust::Vec<JsonEvent>;

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

static uint32_t tree_depth_limit(uint32_t configured)
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

template <typename T, typename... Args>
static int allocate_node(ObIAllocator *allocator, ObJsonNode *&node, Args &&...args)
{
  void *buffer = allocator->alloc(sizeof(T));
  if (OB_ISNULL(buffer)) {
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  node = new (buffer) T(std::forward<Args>(args)...);
  return OB_SUCCESS;
}

static int build_node(ObIAllocator *allocator, const JsonEvents &events,
                      size_t &index, ObJsonNode *&node)
{
  int ret = OB_SUCCESS;
  node = NULL;
  if (index >= events.size()) {
    return OB_ERR_UNEXPECTED;
  }
  const JsonEvent &event = events[index++];
  switch (event.kind) {
    case json::EventKind::Null:
      ret = allocate_node<ObJsonNull>(allocator, node);
      break;
    case json::EventKind::Bool:
      ret = allocate_node<ObJsonBoolean>(allocator, node, event.bool_value);
      break;
    case json::EventKind::I64:
      ret = allocate_node<ObJsonInt>(allocator, node, event.i64_value);
      break;
    case json::EventKind::U64:
      ret = allocate_node<ObJsonUint>(allocator, node, event.u64_value);
      break;
    case json::EventKind::F64:
      ret = allocate_node<ObJsonDouble>(allocator, node, event.f64_value);
      break;
    case json::EventKind::String: {
      ObString value;
      if (OB_FAIL(copy_text(allocator, event.bytes, value))) {
      } else if (value.empty()) {
        ret = allocate_node<ObJsonString>(allocator, node, "", 0);
      } else {
        ret = allocate_node<ObJsonString>(allocator, node, value.ptr(), value.length());
      }
      break;
    }
    case json::EventKind::StartArray: {
      ObJsonArray *array = NULL;
      if (OB_FAIL(allocate_node<ObJsonArray>(allocator, node, allocator))) {
      } else {
        array = static_cast<ObJsonArray *>(node);
      }
      while (OB_SUCC(ret) && index < events.size()
             && events[index].kind != json::EventKind::EndArray) {
        ObJsonNode *child = NULL;
        if (OB_FAIL(build_node(allocator, events, index, child))) {
        } else {
          ret = array->append(child);
        }
      }
      if (OB_FAIL(ret)) {
      } else if (index >= events.size()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ++index;
        array->update_serialize_size();
      }
      break;
    }
    case json::EventKind::StartObject: {
      ObJsonObject *object = NULL;
      if (OB_FAIL(allocate_node<ObJsonObject>(allocator, node, allocator))) {
      } else {
        object = static_cast<ObJsonObject *>(node);
      }
      while (OB_SUCC(ret) && index < events.size()
             && events[index].kind != json::EventKind::EndObject) {
        ObString key;
        ObJsonNode *child = NULL;
        if (events[index].kind != json::EventKind::Key) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(copy_text(allocator, events[index++].bytes, key))) {
        } else if (OB_FAIL(build_node(allocator, events, index, child))) {
        } else {
          ret = object->add(key, child, false, true, false, false);
        }
      }
      if (OB_FAIL(ret)) {
      } else if (index >= events.size()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ++index;
        object->stable_sort();
        object->update_serialize_size();
      }
      break;
    }
    default:
      ret = OB_ERR_UNEXPECTED;
      break;
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

int ObJsonParser::get_tree(ObIAllocator *allocator, const ObString &text, ObJsonNode *&j_tree,
                           uint32_t parse_flag, uint32_t max_depth_config)
{
  return get_tree(allocator, text.ptr(), text.length(), j_tree, parse_flag, max_depth_config);
}

int ObJsonParser::get_tree(ObIAllocator *allocator, const char *text,
                           uint64_t length, ObJsonNode *&j_tree,
                           uint32_t parse_flag, uint32_t max_depth_config)
{
  char buffer[PARSE_SYNTAXERR_MESSAGE_LENGTH] = {0};
  const char *syntaxerr = buffer;
  uint64_t offset = 0;
  return parse_json_text(allocator, text, length, syntaxerr, &offset, j_tree,
                         parse_flag, max_depth_config);
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
      tree_depth_limit(max_depth_config), HAS_FLAG(parse_flag, JSN_SCHEMA_FLAG));
  if (result.status != json::Status::Ok) {
    return result.status == json::Status::Depth ? OB_ERR_INVALID_JSON_TEXT
                                                : status_error(result.status);
  }
  return copy_text(allocator, result.binary, binary);
}

int ObJsonParser::parse_json_text(ObIAllocator *allocator,
                                  const char *text, uint64_t length,
                                  const char *&syntaxerr, uint64_t *offset,
                                  ObJsonNode *&j_tree, uint32_t parse_flag,
                                  uint32_t max_depth_config)
{
  int ret = OB_SUCCESS;
  j_tree = NULL;
  if (OB_ISNULL(allocator) || OB_ISNULL(text) || length == 0) {
    return OB_ERR_NULL_VALUE;
  }
  const uint8_t *input = reinterpret_cast<const uint8_t *>(text);
  json::ParseResult result = json::parse(
      rust::Slice<const uint8_t>(input, length), duplicate_policy(parse_flag),
      tree_depth_limit(max_depth_config), HAS_FLAG(parse_flag, JSN_SCHEMA_FLAG));
  if (result.status != json::Status::Ok) {
    ret = result.status == json::Status::Depth ? OB_ERR_INVALID_JSON_TEXT
                                               : status_error(result.status);
    if (offset != NULL) {
      *offset = result.byte_offset;
      if (result.status == json::Status::Depth || result.status == json::Status::DuplicateKey) {
        ++*offset;
      }
    }
    if (syntaxerr != NULL) {
      static thread_local char message[PARSE_SYNTAXERR_MESSAGE_LENGTH];
      size_t length = result.message.size();
      if (length >= sizeof(message)) {
        length = sizeof(message) - 1;
      }
      MEMCPY(message, result.message.data(), length);
      message[length] = '\0';
      syntaxerr = message;
    }
    LOG_WARN("fail to parse standard JSON text", K(ret), K(result.byte_offset));
    return ret;
  }
  size_t index = 0;
  if (OB_FAIL(build_node(allocator, result.events, index, j_tree))) {
    j_tree = NULL;
  } else if (index != result.events.size() || OB_ISNULL(j_tree)) {
    ret = OB_ERR_UNEXPECTED;
    j_tree = NULL;
  } else if (OB_ISNULL(j_tree->get_allocator())) {
    j_tree->set_allocator(allocator);
  }
  return ret;
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
