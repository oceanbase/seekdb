// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Standalone compatibility oracle using the server's bundled RapidJSON and the
// actual ObBase64Encoder implementation. Only logging is stubbed out. This does
// not link the server or exercise its allocator/task state machine.
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include "rapidjson/document.h"
#include "lib/ob_errno.h"

#define OCEANBASE_LIB_OBLOG_OB_LOG_
#define _OB_LOG(...) ((void)0)
#define OB_ISNULL(value) ((value) == nullptr)
#define OB_UNLIKELY(value) (value)
#define OB_SUCC(value) ((value) == oceanbase::common::OB_SUCCESS)
#define OB_FAIL(value) ((ret = (value)) != oceanbase::common::OB_SUCCESS)
#include "lib/encode/ob_base64_encode.cpp"

using namespace oceanbase::common;
using rapidjson::Value;

// Same handler limits as ObRapidJsonHandler, including ignored/duplicate values.
bool valid_tree(const Value &value, unsigned depth = 0)
{
  if (value.IsDouble() && !std::isfinite(value.GetDouble())) return false;
  if (value.IsArray() || value.IsObject()) {
    if (depth > 100) return false;
    if (value.IsArray()) {
      for (const auto &item : value.GetArray()) if (!valid_tree(item, depth + 1)) return false;
    } else {
      for (auto it = value.MemberBegin(); it != value.MemberEnd(); ++it) {
        if (!valid_tree(it->value, depth + 1)) return false;
      }
    }
  }
  return true;
}

const Value *last_field(const Value &object, const char *name)
{
  const Value *result = nullptr;
  for (auto it = object.MemberBegin(); it != object.MemberEnd(); ++it) {
    if (it->name.GetStringLength() == std::strlen(name) &&
        std::memcmp(it->name.GetString(), name, std::strlen(name)) == 0) result = &it->value;
  }
  return result;
}

int parse(std::string response, int64_t dimension, bool base64,
          std::vector<std::vector<float>> &output)
{
  if (response.empty()) return OB_INVALID_ARGUMENT;
  response.push_back('\0');
  rapidjson::Document document;
  document.ParseInsitu<0>(&response[0]);
  if (document.HasParseError() || !valid_tree(document)) return OB_ERR_INVALID_JSON_TEXT;
  if (!document.IsObject()) return OB_INVALID_ARGUMENT;
  const Value *data = last_field(document, "data");
  if (data == nullptr) return OB_SEARCH_NOT_FOUND;
  if (!data->IsArray()) return OB_INVALID_ARGUMENT;
  for (const auto &item : data->GetArray()) {
    if (!item.IsObject()) return OB_INVALID_ARGUMENT;
    const Value *embedding = last_field(item, "embedding");
    if (embedding == nullptr) return OB_SEARCH_NOT_FOUND;
    std::vector<float> vector;
    if (!base64) {
      if (!embedding->IsArray()) return OB_INVALID_ARGUMENT;
      if (dimension < 0 || embedding->Size() != static_cast<uint64_t>(dimension)) return OB_ERR_UNEXPECTED;
      // ObEmbeddingTask uses ObArenaAllocator: alloc_aligned(0) returns null.
      if (dimension == 0) return OB_ALLOCATE_MEMORY_FAILED;
      for (const auto &number : embedding->GetArray()) {
        if (!number.IsNumber()) return OB_INVALID_ARGUMENT;
        if (number.IsInt64()) vector.push_back(static_cast<float>(number.GetInt64()));
        else if (number.IsUint64()) vector.push_back(static_cast<float>(number.GetUint64()));
        else vector.push_back(static_cast<float>(number.GetDouble()));
      }
    } else {
      if (!embedding->IsString()) return OB_INVALID_ARGUMENT;
      const auto size = embedding->GetStringLength();
      const auto capacity = ObBase64Encoder::needed_decoded_length(size);
      if (capacity <= 0) return OB_INVALID_ARGUMENT;
      std::vector<uint8_t> bytes(capacity);
      int64_t position = 0;
      const int ret = ObBase64Encoder::decode(embedding->GetString(), size, bytes.data(), capacity, position);
      if (ret != OB_SUCCESS) return ret;
      if (dimension < 0 || static_cast<uint64_t>(position) != static_cast<uint64_t>(dimension) * sizeof(float)) return OB_ERR_UNEXPECTED;
      vector.resize(position / sizeof(float));
      if (position > 0) std::memcpy(vector.data(), bytes.data(), position);
    }
    output.push_back(std::move(vector));
  }
  return OB_SUCCESS;
}

int main()
{
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream fields(line);
    int64_t dimension;
    char encoding;
    std::string hex;
    fields >> dimension >> encoding >> hex;
    std::string response;
    for (size_t i = 0; i < hex.size(); i += 2) {
      response.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    std::vector<std::vector<float>> output{{42.0f}};
    int code = parse(response, dimension, encoding == 'b', output);
    std::cout << std::dec << code << ' ' << output.size();
    for (const auto &vector : output) {
      std::cout << ' ' << std::dec << vector.size();
      for (float value : vector) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        std::cout << ' ' << std::hex << std::setw(8) << std::setfill('0') << bits;
      }
    }
    std::cout << '\n';
  }
}
