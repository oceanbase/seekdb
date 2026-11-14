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

#pragma once

#include "rust/local.h"
#include "lib/ob_define.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_string.h"
#include <algorithm>
#include <cstring>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oceanbase { namespace share {

inline int local_status(int code)
{
  if (code <= 0) { return code; } // Preserve errors returned by a C++ callback.
  switch (static_cast<local::Code>(code)) {
    case local::Code::End: return OB_ITER_END;
    case local::Code::NotFound: return OB_ENTRY_NOT_EXIST;
    case local::Code::Duplicate: return OB_ERROR;
    case local::Code::Invalid: return OB_INVALID_ARGUMENT;
    case local::Code::Busy: return OB_EAGAIN;
    case local::Code::Limit: return OB_SIZE_OVERFLOW;
    case local::Code::NotInit: return OB_NOT_INIT;
    default: return OB_IO_ERROR;
  }
}

struct LocalValue {
  local::Kind kind{local::Kind::Null};
  int64_t integer{0};
  std::string bytes;
  bool valid{true};
  LocalValue() = default;
  LocalValue(int64_t value) : kind(local::Kind::Integer), integer(value) {}
  LocalValue(const char *value) : LocalValue(text(value, value ? strlen(value) : 0)) {}
  static LocalValue text(const char *value, int64_t length)
  {
    return from_bytes(value, length, local::Kind::Text);
  }
  static LocalValue blob(const void *value, int64_t length)
  {
    return from_bytes(value, length, local::Kind::Blob);
  }
  local::Value wire() const
  {
    local::Value value{kind, integer, {}};
    value.bytes.reserve(bytes.size());
    for (unsigned char byte : bytes) { value.bytes.push_back(byte); }
    return value;
  }
private:
  static LocalValue from_bytes(const void *value, int64_t length, local::Kind kind)
  {
    LocalValue result;
    result.valid = length >= 0 && (value || length == 0);
    result.kind = value ? kind : local::Kind::Null;
    if (result.valid && length > 0) { result.bytes.assign(static_cast<const char *>(value), length); }
    return result;
  }
};

inline local::Schema local_schema(uint8_t namespace_id,
    std::initializer_list<local::Column> columns,
    std::initializer_list<uint32_t> primary,
    std::initializer_list<uint32_t> indexes = {})
{
  local::Schema schema;
  schema.namespace_id = namespace_id;
  for (const auto &column : columns) { schema.columns.push_back(column); }
  for (auto column : primary) { schema.primary.push_back(column); }
  for (auto column : indexes) { schema.indexes.push_back(column); }
  return schema;
}

// Business modules supply the schema; Rust owns encoding, indexes and selection.
class LocalRequest {
public:
  static LocalRequest select(const local::Schema &schema, std::initializer_list<uint32_t> columns)
  {
    LocalRequest request(schema); request.columns_ = columns; return request;
  }
  static LocalRequest insert(const local::Schema &schema)
  {
    LocalRequest request(schema); request.action_ = local::Action::Insert; return request;
  }
  static LocalRequest upsert(const local::Schema &schema)
  {
    LocalRequest request(schema); request.action_ = local::Action::Upsert; return request;
  }
  static LocalRequest update(const local::Schema &schema) { return LocalRequest(schema); }
  static LocalRequest erase(const local::Schema &schema)
  {
    LocalRequest request(schema); request.action_ = local::Action::Delete; return request;
  }
  LocalRequest &eq(uint32_t column, LocalValue value) { return where(column, local::Compare::Equal, std::move(value)); }
  LocalRequest &ne(uint32_t column, LocalValue value) { return where(column, local::Compare::NotEqual, std::move(value)); }
  LocalRequest &lt(uint32_t column, LocalValue value) { return where(column, local::Compare::Less, std::move(value)); }
  LocalRequest &le(uint32_t column, LocalValue value) { return where(column, local::Compare::LessEqual, std::move(value)); }
  LocalRequest &gt(uint32_t column, LocalValue value) { return where(column, local::Compare::Greater, std::move(value)); }
  LocalRequest &ge(uint32_t column, LocalValue value) { return where(column, local::Compare::GreaterEqual, std::move(value)); }
  LocalRequest &order(uint32_t column) { order_ = column; return *this; }
  LocalRequest &take(int64_t limit) { limit_ = std::move(limit); return *this; }
  LocalRequest &skip(int64_t offset) { offset_ = std::move(offset); return *this; }
  LocalRequest &aggregate(local::Aggregate aggregate) { aggregate_ = aggregate; return *this; }
  LocalRequest &set(uint32_t column, LocalValue value, int clamp = -1)
  {
    fields_.push_back({column, std::move(value), clamp}); return *this;
  }
  template <class A> LocalRequest &in(uint32_t column, const A &ids)
  {
    Condition condition{column, local::Compare::In, {}};
    condition.values.reserve(ids.count());
    for (int64_t i = 0; i < ids.count(); ++i) { condition.values.emplace_back(static_cast<int64_t>(ids.at(i).id())); }
    conditions_.push_back(std::move(condition)); return *this;
  }
  int encode(local::Mutation &output) const
  {
    auto &query = output.query;
    query.schema = schema_;
    for (auto column : columns_) { query.columns.push_back(column); }
    for (const auto &condition : conditions_) {
      local::Condition wire;
      wire.column = condition.column;
      wire.compare = condition.compare;
      for (const LocalValue &arg : condition.values) {
        if (!arg.valid) { return OB_INVALID_ARGUMENT; }
        wire.values.push_back(arg.wire());
      }
      query.conditions.push_back(std::move(wire));
    }
    for (const Field &field : fields_) {
      if (!field.value.valid) { return OB_INVALID_ARGUMENT; }
      output.fields.push_back({field.column, field.value.wire(), field.clamp});
    }
    query.order_column = order_;
    query.limit = limit_;
    query.offset = static_cast<uint64_t>(std::max<int64_t>(0, offset_));
    query.aggregate = aggregate_;
    output.action = action_;
    return OB_SUCCESS;
  }

private:
  struct Condition { uint32_t column; local::Compare compare; std::vector<LocalValue> values; };
  struct Field { uint32_t column; LocalValue value; int clamp; };
  explicit LocalRequest(const local::Schema &schema) : schema_(schema) {}
  LocalRequest &where(uint32_t column, local::Compare compare, LocalValue value)
  {
    conditions_.push_back({column, compare, {std::move(value)}}); return *this;
  }
  local::Schema schema_;
  std::vector<uint32_t> columns_;
  std::vector<Condition> conditions_;
  std::vector<Field> fields_;
  int64_t limit_{-1}, offset_{0};
  int32_t order_{-1};
  local::Aggregate aggregate_{local::Aggregate::None};
  local::Action action_{local::Action::Update};
};

class LocalRowReader {
public:
  void set(rust::Slice<const local::Value> values) { values_ = values; index_ = 0; }
  int64_t get_int64() { return get_int64(index_++); }
  int32_t get_int() { return static_cast<int32_t>(get_int64()); }
  int64_t get_int64(int index) const
  {
    return index >= 0 && static_cast<size_t>(index) < values_.size() ? values_[index].integer : 0;
  }
  const char *get_text(int *length = nullptr) { return get_text(index_++, length); }
  const char *get_text(int index, int *length = nullptr) const
  {
    return static_cast<const char *>(get_blob(index, length));
  }
  const void *get_blob(int *length = nullptr) { return get_blob(index_++, length); }
  const void *get_blob(int index, int *length = nullptr) const
  {
    const local::Value *value = index >= 0 && static_cast<size_t>(index) < values_.size() ? &values_[index] : nullptr;
    if (length) { *length = value ? static_cast<int>(value->bytes.size()) : 0; }
    return value && value->bytes.size() > 0 ? value->bytes.data() : nullptr;
  }
private:
  rust::Slice<const local::Value> values_;
  int index_{0};
};

class LocalCursor {
public:
  void reset() { cursor_.reset(); }
  int next(LocalRowReader &reader)
  {
    if (!cursor_) { return OB_NOT_INIT; }
    const int ret = local_status((*cursor_)->next());
    reader.set((*cursor_)->row());
    return ret;
  }
private:
  friend class LocalStore;
  std::optional<rust::Box<local::Cursor>> cursor_;
};

// Borrows the Rust transaction only for the synchronous Store::write callback.
class LocalTransaction {
public:
  LocalTransaction(const LocalTransaction &) = delete;
  LocalTransaction &operator=(const LocalTransaction &) = delete;
  int execute(const LocalRequest &request, int64_t *affected = nullptr)
  {
    local::Mutation mutation;
    int ret = request.encode(mutation);
    if (OB_SUCCESS == ret) {
      const auto result = transaction_.execute(mutation);
      if (affected) { *affected = static_cast<int64_t>(result.affected); }
      ret = local_status(result.code);
    }
    if (OB_SUCCESS != ret) { transaction_.abort(ret); }
    return ret;
  }
  int query(const LocalRequest &request, const std::function<int(LocalRowReader &)> &visitor)
  {
    local::Mutation mutation;
    int ret = request.encode(mutation);
    if (OB_SUCCESS != ret) { transaction_.abort(ret); return ret; }
    bool found = false;
    const local::RowCallback rows([&](rust::Slice<const local::Value> values) -> int32_t {
      found = true;
      LocalRowReader reader;
      reader.set(values);
      const int status = visitor(reader);
      return status == OB_ITER_END ? static_cast<int32_t>(local::Code::End) : status;
    });
    ret = local_status(transaction_.query(mutation.query, rows));
    return OB_SUCCESS == ret && !found ? OB_ENTRY_NOT_EXIST : ret;
  }
private:
  friend class LocalStore;
  explicit LocalTransaction(const local::Transaction &transaction) : transaction_(transaction) {}
  const local::Transaction &transaction_;
};

class LocalStore {
public:
  int init(const char *path)
  {
    if (store_) { return OB_INIT_TWICE; }
    if (!path) { return OB_INVALID_ARGUMENT; }
    auto store = local::open_storage(rust::Str(path));
    const int ret = local_status(store->status());
    auto summary = store->directory_summary();
    if (summary.count != 0) {
      SHARE_LOG(WARN, "directory durability unconfirmed; only first failure detail retained", K(ret),
          "failed_flush_count", summary.count, "count_is_lower_bound", summary.count == UINT64_MAX,
          "first_path", common::ObString(static_cast<int32_t>(summary.first_path.size()),
              reinterpret_cast<const char *>(summary.first_path.data())),
          "native_domain", static_cast<int>(summary.first_error.domain),
          "native_error", summary.first_error.native_code, "error", summary.first_error.message.c_str());
    }
    if (OB_SUCCESS != ret) {
      auto detail = store->open_error();
      const auto sync = store->open_sync_error();
      SHARE_LOG(WARN, "local storage open failed", K(ret), K(path), "error", detail.c_str(),
          "sync_stage", static_cast<int>(sync.stage), "native_domain", static_cast<int>(sync.primary.domain),
          "native_error", sync.primary.native_code, "close_domain", static_cast<int>(sync.close.domain),
          "close_error", sync.close.native_code);
    }
    if (OB_SUCCESS == ret) { store_.emplace(std::move(store)); }
    return ret;
  }
  void destroy() { store_.reset(); }
  bool is_inited() const { return store_.has_value(); }
  const local::Store &handle() const { return **store_; }
  int write(const std::function<int(LocalTransaction &)> &callback) const
  {
    if (!is_inited()) { return OB_NOT_INIT; }
    const local::WriteCallback work([&](const local::Transaction &transaction) -> int32_t {
      LocalTransaction tx(transaction);
      return callback(tx);
    });
    return local_status(handle().write(work));
  }
  int execute(const LocalRequest &request, int64_t *affected = nullptr) const
  {
    return write([&](LocalTransaction &tx) { return tx.execute(request, affected); });
  }
  int open_cursor(const LocalRequest &request, LocalCursor &cursor) const
  {
    cursor.reset();
    if (!is_inited()) { return OB_NOT_INIT; }
    local::Mutation mutation;
    int ret = request.encode(mutation);
    if (OB_SUCCESS == ret) {
      auto opened = handle().cursor_open(mutation.query);
      ret = local_status(opened->status());
      if (OB_SUCCESS == ret) { cursor.cursor_.emplace(std::move(opened)); }
    }
    return ret;
  }
  int query(const LocalRequest &request, const std::function<int(LocalRowReader &)> &visitor) const
  {
    LocalCursor cursor;
    int ret = open_cursor(request, cursor);
    bool found = false;
    LocalRowReader reader;
    while (OB_SUCCESS == ret) {
      ret = cursor.next(reader);
      if (OB_SUCCESS == ret) {
        found = true;
        ret = visitor(reader);
        if (OB_ITER_END == ret) { ret = OB_SUCCESS; break; }
      } else if (OB_ITER_END == ret) { ret = found ? OB_SUCCESS : OB_ENTRY_NOT_EXIST; break; }
    }
    return ret;
  }
private:
  std::optional<rust::Box<local::Store>> store_;
};
} } // namespace oceanbase::share
