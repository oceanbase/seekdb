#define USING_LOG_PREFIX SQL_ENG
#include "jsonl_reader.h"
#include "share/ob_errno.h"
#include "common/json_type/ob_json_base.h"
#include <algorithm>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace oceanbase::sql::file_sql {
using namespace common;

void *JsonlArena::alloc(const int64_t size)
{
  constexpr int64_t limit = 64 * 1024 * 1024;
  if (size < 0 || size > limit - requested_) { exhausted_ = true; return nullptr; }
  requested_ += size;
  return arena_.alloc(size);
}

static int malformed_jsonl()
{
  return OB_ERR_PARSE_SQL;
}

int JsonlReader::next(NamedRow &row)
{
  row.clear();
  arena_.reuse();
  if (ended_) return OB_ITER_END;
  for (;;) {
    line_.clear();
    for (;;) {
      const int ch = input_.get();
      if (ch == std::char_traits<char>::eof()) {
        ended_ = true;
        if (input_.bad()) return OB_IO_ERROR;
        break;
      }
      if (ch == '\n') break;
      if (line_.size() == 64 * 1024 * 1024) return OB_SIZE_OVERFLOW;
      line_.push_back(static_cast<char>(ch));
    }
    if (first_line_) {
      first_line_ = false;
      if (line_.compare(0, 3, "\xef\xbb\xbf") == 0) line_.erase(0, 3);
    }
    if (line_.find_first_not_of(" \t\r") != std::string::npos) break;
    if (ended_) return OB_ITER_END;
  }
  if (line_.find('\0') != std::string::npos) return malformed_jsonl();
  ObIJsonBase *object = nullptr;
  int ret = ObJsonBaseFactory::get_json_base(&arena_, ObString(line_.size(), line_.data()),
      ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, object);
  if (arena_.exhausted()) return OB_SIZE_OVERFLOW;
  if (ret == OB_ALLOCATE_MEMORY_FAILED) return ret;
  if (ret != OB_SUCCESS || object == nullptr || object->json_type() != ObJsonNodeType::J_OBJECT) return malformed_jsonl();
  if (object->element_count() > 4096) return OB_SIZE_OVERFLOW;
  for (uint64_t i = 0; i < object->element_count(); ++i) {
    ObString key;
    ObIJsonBase *value = nullptr;
    if (OB_FAIL(object->get_object_value(i, key, value))) return ret;
    if (value == nullptr) return OB_ERR_UNEXPECTED;
    if (key.length() > 128 || (key.length() > 0 && std::memchr(key.ptr(), 0, key.length()) != nullptr)) {
      LOG_USER_ERROR(OB_WRONG_COLUMN_NAME, key.length(), key.ptr());
      return OB_WRONG_COLUMN_NAME;
    }
    NamedField field;
    field.name.assign(key.ptr(), key.length());
    const ObJsonNodeType kind = value->json_type();
    if (kind != ObJsonNodeType::J_NULL) {
      field.value.quoted = true; // JSON string "NULL" and empty strings are values.
      if (kind == ObJsonNodeType::J_STRING) {
        field.value.text.assign(value->get_data(), value->get_data_length());
        const Type inferred = classify(field.value.text);
        field.type = inferred == Type::date || inferred == Type::datetime ? inferred : Type::text;
      } else {
        ObJsonBuffer encoded(&arena_);
        if (OB_FAIL(value->print(encoded, false))) return arena_.exhausted() ? OB_SIZE_OVERFLOW : ret;
        const ObString text = encoded.string();
        field.value.text.assign(text.ptr(), text.length());
        if (kind == ObJsonNodeType::J_BOOLEAN) field.type = Type::boolean;
        else if (kind == ObJsonNodeType::J_INT || kind == ObJsonNodeType::J_UINT ||
                 kind == ObJsonNodeType::J_DOUBLE || kind == ObJsonNodeType::J_DECIMAL) field.type = classify(field.value.text);
        else field.type = Type::text; // Nested values retain their compact JSON.
      }
    }
    row.push_back(std::move(field));
  }
  return OB_SUCCESS;
}

int JsonlReader::infer(Schema &schema, const std::function<bool()> &keep_running)
{
  schema.clear();
  std::unordered_map<std::string, size_t> columns;
  std::vector<uint64_t> seen;
  uint64_t rows = 0;
  NamedRow row;
  for (;;) {
    if (!keep_running()) return OB_CANCELED;
    int ret = next(row);
    if (ret == OB_ITER_END) break;
    if (ret != OB_SUCCESS) return ret;
    ++rows;
    for (const NamedField &field : row) {
      if (!keep_running()) return OB_CANCELED;
      auto found = columns.find(field.name);
      if (found == columns.end()) {
        if (schema.size() == 4096) return OB_SIZE_OVERFLOW;
        const size_t index = schema.size();
        columns.emplace(field.name, index);
        Column column;
        column.name = field.name.empty() ? "column" + std::to_string(index) : field.name;
        column.source_name = field.name;
        schema.push_back(std::move(column));
        seen.push_back(0);
        found = columns.find(field.name);
      }
      const size_t index = found->second;
      ++seen[index];
      schema[index].nullable |= field.value.is_null();
      schema[index].max_bytes = std::max(schema[index].max_bytes, field.value.text.size());
      schema[index].type = widen(schema[index].type, field.type);
    }
  }
  if (schema.empty()) return malformed_jsonl();
  for (size_t i = 0; i < schema.size(); ++i) schema[i].nullable |= seen[i] < rows;
  return OB_SUCCESS;
}
} // namespace oceanbase::sql::file_sql
