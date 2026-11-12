/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *     http://www.apache.org/licenses/LICENSE-2.0
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "file_sql_jsonl.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>

namespace oceanbase { namespace sql { namespace file_sql {
namespace {

const char kTab = 0x09;
const char kLineFeed = 0x0a;
const char kCarriageReturn = 0x0d;
const char kSpace = 0x20;
const char kBackslash = 0x5c;
const char kQuote = 0x22;
const char kSlash = 0x2f;
const char kBackspace = 0x08;
const char kFormFeed = 0x0c;
const char kNewline = 0x0a;
const char kTabEscape = 0x09;
const char kReturn = 0x0d;
const char kLowerU = 0x75;

bool is_json_space(char c)
{
  return c == kSpace || c == kTab || c == kLineFeed || c == kCarriageReturn;
}

// A minimal scanner over the text of one JSON object. It reads the flat
// members of the object and rejects nested containers, which the flat schema
// forbids. The scanner never allocates a parse tree, so the memory does not
// depend on the number of members.
class JsonObjectScanner
{
public:
  JsonObjectScanner(const char *data, size_t size) : data_(data), size_(size) {}

  // Reads the next member. Returns 1 on success, 0 at the end of the object
  // and -1 on a malformed document.
  int next(std::string &name, std::string &text, bool &is_null)
  {
    if (!started_) {
      // The document must start with an opening brace.
      skip_spaces();
      if (at_end() || peek() != '{') { return -1; }
      ++pos_;
      started_ = true;
    }
    skip_spaces();
    if (at_end()) { return -1; }
    if (peek() == ',') { ++pos_; skip_spaces(); }
    if (at_end()) { return -1; }
    // The closing brace of the object ends the member list.
    if (peek() == '}') { return 0; }
    if (peek() != kQuote) { return -1; }
    if (!read_string(name)) { return -1; }
    skip_spaces();
    if (at_end() || peek() != ':') { return -1; }
    ++pos_;
    skip_spaces();
    if (at_end()) { return -1; }
    if (!read_scalar(text, is_null)) { return -1; }
    return 1;
  }

  // Checks that the object is closed and that nothing follows it.
  bool finished() const
  {
    size_t p = pos_;
    while (p < size_ && is_json_space(data_[p])) { ++p; }
    if (p >= size_) { return false; }
    if (data_[p] != '}') { return false; }
    ++p;
    while (p < size_ && is_json_space(data_[p])) { ++p; }
    return p == size_;
  }

private:
  bool at_end() const { return pos_ >= size_; }
  char peek() const { return data_[pos_]; }
  void skip_spaces()
  {
    while (pos_ < size_ && is_json_space(data_[pos_])) { ++pos_; }
  }

  // Appends the code point as UTF-8.
  static void append_utf8(std::string &out, unsigned int cp)
  {
    if (cp < 0x80) {
      out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
      out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
      out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    } else {
      out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
    }
  }

  static int hex_value(char c)
  {
    if (c >= '0' && c <= '9') { return c - '0'; }
    if (c >= 'a' && c <= 'f') { return c - 'a' + 10; }
    if (c >= 'A' && c <= 'F') { return c - 'A' + 10; }
    return -1;
  }

  bool read_hex4(unsigned int &value)
  {
    if (pos_ + 4 > size_) { return false; }
    value = 0;
    for (int k = 0; k < 4; ++k) {
      const int digit = hex_value(data_[pos_ + k]);
      if (digit < 0) { return false; }
      value = (value << 4) | static_cast<unsigned int>(digit);
    }
    pos_ += 4;
    return true;
  }

  // Reads a JSON string and resolves its escape sequences.
  bool read_string(std::string &out)
  {
    out.clear();
    ++pos_;  // the opening quote
    while (pos_ < size_) {
      const char c = data_[pos_++];
      if (c == kQuote) { return true; }
      if (c != kBackslash) { out.push_back(c); continue; }
      if (pos_ >= size_) { return false; }
      const char escaped = data_[pos_++];
      switch (escaped) {
        case kQuote: out.push_back(kQuote); break;
        case kBackslash: out.push_back(kBackslash); break;
        case kSlash: out.push_back(kSlash); break;
        case 'b': out.push_back(kBackspace); break;
        case 'f': out.push_back(kFormFeed); break;
        case 'n': out.push_back(kNewline); break;
        case 'r': out.push_back(kReturn); break;
        case 't': out.push_back(kTabEscape); break;
        case 'u': {
          unsigned int cp = 0;
          if (!read_hex4(cp)) { return false; }
          // Join a surrogate pair when the low part follows.
          if (cp >= 0xd800 && cp <= 0xdbff && pos_ + 6 <= size_
              && data_[pos_] == kBackslash && data_[pos_ + 1] == kLowerU) {
            const size_t saved = pos_;
            pos_ += 2;
            unsigned int low = 0;
            if (read_hex4(low) && low >= 0xdc00 && low <= 0xdfff) {
              cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
            } else {
              pos_ = saved;
            }
          }
          append_utf8(out, cp);
          break;
        }
        default:
          return false;
      }
    }
    return false;
  }

  // Reads a scalar value: a string, a number, a boolean or null.
  bool read_scalar(std::string &text, bool &is_null)
  {
    is_null = false;
    text.clear();
    if (peek() == kQuote) { return read_string(text); }
    if (peek() == '{' || peek() == '[') { return false; }
    const size_t start = pos_;
    while (pos_ < size_) {
      const char c = data_[pos_];
      if (c == ',' || c == '}' || is_json_space(c)) { break; }
      ++pos_;
    }
    if (pos_ == start) { return false; }
    text.assign(data_ + start, pos_ - start);
    if (text == "null") { is_null = true; text.clear(); }
    return true;
  }

  const char *data_ = nullptr;
  size_t size_ = 0;
  size_t pos_ = 0;
  bool started_ = false;
};

} // namespace

FileStatus JsonlReader::fail(FileCode code, size_t column, const char *message)
{
  // The end of the input is not a failure: the reader must stay usable so
  // that the caller can rescan it.
  if (code != FileCode::END) {
    failure_ = {code, record_, record_line_, column, message};
  }
  return {code, record_, record_line_, column, message};
}

FileStatus JsonlReader::init(FILE *input, const JsonlOptions &options)
{
  if (input == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"jsonl input is null"}; }
  if (fstat(fileno(input), &snapshot_) != 0) {
    return {FileCode::IO_ERROR,0,0,0,"cannot stat jsonl input"};
  }
  input_ = input;
  options_ = options;
  start_ = ftello(input_);
  record_ = 0;
  line_ = 1;
  record_line_ = 1;
  eof_ = false;
  failure_ = {};
  return {};
}

FileStatus JsonlReader::next_record(std::vector<JsonlField> &fields)
{
  fields.clear();
  if (!failure_.ok()) { return failure_; }
  if (input_ == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"jsonl reader is not initialized"}; }

  std::string line;
  bool got = false;
  while (true) {
    const int c = fgetc(input_);
    if (c == EOF) {
      if (ferror(input_)) { return fail(FileCode::IO_ERROR,0,"read error on jsonl input"); }
      if (!got) { eof_ = true; return fail(FileCode::END,0,"end of jsonl input"); }
      break;
    }
    got = true;
    if (c == kLineFeed) { break; }
    if (c == kCarriageReturn) {
      const int next = fgetc(input_);
      if (next != kLineFeed && next != EOF) { ungetc(next, input_); }
      break;
    }
    line.push_back(static_cast<char>(c));
    if (line.size() > options_.max_line_bytes) {
      return fail(FileCode::RESOURCE_LIMIT,0,"jsonl line exceeds the configured size limit");
    }
  }
  if (!got) { eof_ = true; return fail(FileCode::END,0,"end of jsonl input"); }

  record_line_ = line_;
  ++line_;

  // A blank line carries no object and must not widen the schema.
  bool blank = true;
  for (const char ch : line) {
    if (!is_json_space(ch)) { blank = false; break; }
  }
  if (blank) { return next_record(fields); }

  // The record must be a flat JSON object.
  size_t start = 0;
  while (start < line.size() && is_json_space(line[start])) { ++start; }
  if (start >= line.size() || line[start] != '{') {
    return fail(FileCode::INVALID_DATA,0,"jsonl record must be a JSON object");
  }
  JsonObjectScanner scanner(line.data() + start, line.size() - start);
  while (true) {
    JsonlField field;
    const int step = scanner.next(field.name, field.text, field.is_null);
    if (step == 0) { break; }
    if (step < 0) {
      return fail(FileCode::INVALID_DATA,0,"invalid JSON in jsonl record");
    }
    if (fields.size() + 1 > options_.max_columns) {
      return fail(FileCode::RESOURCE_LIMIT,0,"jsonl record exceeds the configured column limit");
    }
    fields.push_back(std::move(field));
  }
  if (!scanner.finished()) {
    return fail(FileCode::INVALID_DATA,0,"invalid JSON in jsonl record");
  }
  ++record_;
  return {};
}

FileStatus JsonlReader::rescan()
{
  if (input_ == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"jsonl reader is not initialized"}; }
  if (fseeko(input_, start_, SEEK_SET) != 0) {
    return fail(FileCode::IO_ERROR,0,"cannot rewind jsonl input");
  }
  record_ = 0;
  line_ = 1;
  record_line_ = 1;
  eof_ = false;
  failure_ = {};
  return {};
}

FileStatus JsonlReader::check_unchanged() const
{
  struct stat now {};
  if (input_ == nullptr || fstat(fileno(input_), &now) != 0) {
    return {FileCode::IO_ERROR,0,0,0,"cannot stat jsonl input"};
  }
  if (now.st_ino != snapshot_.st_ino || now.st_dev != snapshot_.st_dev
      || now.st_size != snapshot_.st_size
      || now.st_mtime != snapshot_.st_mtime || now.st_ctime != snapshot_.st_ctime) {
    return {FileCode::FILE_CHANGED,0,0,0,"jsonl input changed during the scan"};
  }
  return {};
}

FileStatus JsonlTableScan::ensure_column(const std::string &name, size_t &index)
{
  const auto found = index_of_name_.find(name);
  if (found != index_of_name_.end()) {
    index = found->second;
    return {};
  }
  if (names_.size() >= options_.max_columns) {
    return {FileCode::RESOURCE_LIMIT,0,0,0,"jsonl file exceeds the configured column limit"};
  }
  index = names_.size();
  names_.push_back(name);
  index_of_name_.emplace(name, index);
  return {};
}

FileStatus JsonlTableScan::build_row(const std::vector<JsonlField> &fields, TextRow &row)
{
  row.assign(names_.size(), TextCell{});
  // A column that the record does not carry is a NULL value. The cell starts
  // as a null cell and every field that is present overwrites it.
  for (auto &cell : row) { cell.is_null = true; }
  for (const auto &field : fields) {
    size_t index = 0;
    const FileStatus added = ensure_column(field.name, index);
    if (!added.ok()) { return added; }
    row[index].text = field.text;
    row[index].is_null = field.is_null;
  }
  return {};
}

FileStatus JsonlTableScan::infer_schema()
{
  // First pass: discover the field union and infer the column types.
  // Only the column state is kept, never a row.
  names_.clear();
  index_of_name_.clear();
  std::vector<JsonlField> fields;
  while (true) {
    const FileStatus status = reader_.next_record(fields);
    if (status.code == FileCode::END) { break; }
    if (!status.ok()) { return status; }
    for (const auto &field : fields) {
      size_t index = 0;
      const FileStatus added = ensure_column(field.name, index);
      if (!added.ok()) { return added; }
    }
  }

  // The schema can only be published once every record has been observed.
  if (names_.empty()) {
    return {FileCode::INVALID_DATA,0,0,0,"jsonl file does not contain any object"};
  }
  const FileStatus init = schema_.init(names_);
  if (!init.ok()) { return init; }

  // Second pass of the inference: replay the records to observe their values.
  const FileStatus rewound = reader_.rescan();
  if (!rewound.ok()) { return rewound; }
  while (true) {
    const FileStatus status = reader_.next_record(fields);
    if (status.code == FileCode::END) { break; }
    if (!status.ok()) { return status; }
    TextRow row;
    const FileStatus built = build_row(fields, row);
    if (!built.ok()) { return built; }
    const FileStatus observed = schema_.observe(row, reader_.record_number(), reader_.record_line());
    if (!observed.ok()) { return observed; }
  }
  inferred_ = true;
  return {};
}

FileStatus JsonlTableScan::prepare(FILE *input, const JsonlOptions &options, int64_t sample_size)
{
  if (sample_size != -1) {
    return {FileCode::INVALID_ARGUMENT,0,0,0,"jsonl only supports a full schema inference"};
  }
  const FileStatus status = reader_.init(input, options);
  if (!status.ok()) { return status; }
  options_ = options;
  prepared_ = true;
  inferred_ = false;
  emitted_ = 0;
  names_.clear();
  index_of_name_.clear();
  const FileStatus inferred = infer_schema();
  if (!inferred.ok()) { return inferred; }
  // Rewind so that the caller receives every record exactly once.
  return reader_.rescan();
}

FileStatus JsonlTableScan::next_batch(size_t limit, std::vector<ScalarRow> &rows)
{
  rows.clear();
  if (!prepared_ || !inferred_) {
    return {FileCode::INVALID_ARGUMENT,0,0,0,"jsonl scan is not prepared"};
  }
  const FileStatus unchanged = reader_.check_unchanged();
  if (!unchanged.ok()) { return unchanged; }

  std::vector<JsonlField> fields;
  size_t bytes = 0;
  while (rows.size() < limit) {
    const FileStatus status = reader_.next_record(fields);
    if (status.code == FileCode::END) { break; }
    if (!status.ok()) { return status; }
    TextRow row;
    const FileStatus built = build_row(fields, row);
    if (!built.ok()) { return built; }
    ScalarRow values;
    values.reserve(row.size());
    for (size_t i = 0; i < row.size(); ++i) {
      ScalarValue value;
      const FileStatus converted = convert_scalar(row[i], schema_.columns()[i].type, value);
      if (!converted.ok()) { return converted; }
      values.push_back(std::move(value));
    }
    bytes += values.size() * sizeof(ScalarValue);
    rows.push_back(std::move(values));
    ++emitted_;
    if (bytes > options_.max_batch_bytes) { break; }
  }
  return {};
}

FileStatus JsonlTableScan::rescan()
{
  if (!prepared_) { return {FileCode::INVALID_ARGUMENT,0,0,0,"jsonl scan is not prepared"}; }
  const FileStatus status = reader_.rescan();
  if (!status.ok()) { return status; }
  return {};
}

}}} // namespace oceanbase::sql::file_sql
