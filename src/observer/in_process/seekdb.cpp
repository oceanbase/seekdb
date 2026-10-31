// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "seekdb.h"
#include "rust/in_process.h"
#include <cstring>
#include <string>
#include <vector>

using oceanbase::in_process::OpenCode;
using oceanbase::in_process::OpenResult;
using oceanbase::in_process::Parameter;

static_assert(static_cast<int>(OpenCode::Success) == SEEKDB_SUCCESS);
static_assert(static_cast<int>(OpenCode::InternalError) == SEEKDB_INTERNAL_ERROR);
static_assert(static_cast<int>(OpenCode::InvalidArgument) == SEEKDB_INVALID_ARGUMENT);

namespace {
thread_local std::string last_open_error;

rust::Slice<const uint8_t> bytes(const char *value)
{
  return {reinterpret_cast<const uint8_t *>(value), value == nullptr ? 0 : std::strlen(value)};
}

rust::Vec<uint8_t> copy_bytes(const char *value)
{
  rust::Vec<uint8_t> result;
  const auto input = bytes(value);
  result.reserve(input.size());
  for (uint8_t byte : input) {
    result.push_back(byte);
  }
  return result;
}

int invalid_argument(const char *message)
{
  last_open_error = message;
  return SEEKDB_INVALID_ARGUMENT;
}
} // namespace

int seekdb_open(const char *db_dir, const char **parameters, SeekdbHandle *out_handle)
{
  last_open_error.clear();
  if (out_handle == nullptr) {
    return invalid_argument("out_handle must not be NULL");
  }
  *out_handle = nullptr;
  std::vector<Parameter> pairs;
  if (parameters != nullptr) {
    for (size_t i = 0; parameters[i] != nullptr; i += 2) {
      if (parameters[i][0] == '\0' || parameters[i + 1] == nullptr) {
        return invalid_argument("parameters must be key/value pairs with non-empty keys");
      }
      pairs.push_back(Parameter{copy_bytes(parameters[i]), copy_bytes(parameters[i + 1])});
    }
  }
  auto result = oceanbase::in_process::open(bytes(db_dir), {pairs.data(), pairs.size()});
  const int code = static_cast<int>(result->status());
  if (code == SEEKDB_SUCCESS) {
    *out_handle = result.into_raw();
  } else {
    last_open_error = std::string(result->error());
  }
  return code;
}

int seekdb_connection_options(SeekdbHandle handle, SeekdbConnectionOptions *out_options)
{
  if (handle == nullptr || out_options == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  const auto path = static_cast<const OpenResult *>(handle)->socket_path();
  *out_options = SeekdbConnectionOptions{
      SEEKDB_CONNECTION_TRANSPORT_UNIX_SOCKET, 0, nullptr,
      reinterpret_cast<const char *>(path.data()), nullptr, "root"};
  return SEEKDB_SUCCESS;
}

const char *seekdb_last_open_error()
{
  return last_open_error.c_str();
}

int seekdb_close(SeekdbHandle handle)
{
  if (handle == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  auto owned = rust::Box<OpenResult>::from_raw(static_cast<OpenResult *>(handle));
  return SEEKDB_SUCCESS;
}
