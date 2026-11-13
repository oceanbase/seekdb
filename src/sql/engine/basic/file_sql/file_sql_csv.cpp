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
#include "file_sql_csv.h"
#include <algorithm>
#include <set>
#include <unistd.h>

namespace oceanbase { namespace sql { namespace file_sql {
namespace {
FileStatus validate_options(const CsvOptions &o)
{
  const auto ascii_option = [](char c) { return c > 0 && c < 127 && c != '\r' && c != '\n'; };
  if (!ascii_option(o.delimiter) || !ascii_option(o.quote) || !ascii_option(o.escape)
      || o.delimiter == o.quote || o.delimiter == o.escape || o.max_columns == 0 || o.max_columns > 65536
      || o.max_record_bytes == 0 || o.max_record_bytes > 64 * 1024 * 1024
      || o.max_batch_bytes < o.max_record_bytes + o.max_columns * sizeof(TextCell)
      || o.max_batch_bytes > 128 * 1024 * 1024
      || !valid_utf8(o.null_string) || o.null_string.find_first_of(std::string()+o.delimiter+o.quote+"\r\n") != std::string::npos) {
    return {FileCode::INVALID_ARGUMENT,0,0,0,"invalid CSV options or resource limits"};
  }
  return {};
}
std::string folded(std::string s)
{
  for (char &c : s) { if (c >= 'A' && c <= 'Z') { c += 'a'-'A'; } }
  return s;
}
std::vector<std::string> column_names(const TextRow &header)
{
  std::vector<std::string> names;
  std::set<std::string> reserved, used;
  for (size_t i = 0; i < header.size(); ++i) {
    names.push_back(header[i].text.empty() ? "column"+std::to_string(i) : header[i].text);
    reserved.insert(folded(names.back()));
  }
  for (std::string &name : names) {
    if (used.insert(folded(name)).second) { continue; }
    const std::string base = name;
    size_t suffix = 2;
    do { name = base+"__"+std::to_string(suffix++); }
    while (reserved.count(folded(name)) != 0 || used.count(folded(name)) != 0);
    used.insert(folded(name));
  }
  return names;
}
size_t row_bytes(const TextRow &row)
{
  size_t total = row.size() * sizeof(TextCell);
  for (const auto &cell : row) { total += cell.text.size(); }
  return total;
}
} // namespace

FileStatus CsvReader::init(FILE *input, const CsvOptions &options)
{
  input_ = nullptr; failure_ = {}; eof_ = false; record_ = 0; line_ = 1; record_line_ = 1;
  FileStatus s = validate_options(options);
  if (!s.ok()) { return s; }
  if (input == nullptr || fstat(fileno(input), &snapshot_) != 0 || !S_ISREG(snapshot_.st_mode)) {
    return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV requires an authorized regular file"};
  }
  start_ = ftello(input);
  if (start_ < 0) { return {FileCode::IO_ERROR,0,0,0,"cannot seek CSV input"}; }
  input_ = input; options_ = options;
  return rescan();
}
FileStatus CsvReader::check_unchanged() const
{
  if (input_ == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV reader is not initialized"}; }
  struct stat now {};
  if (fstat(fileno(input_), &now) != 0) { return {FileCode::IO_ERROR,record_,line_,0,"cannot stat CSV input"}; }
  if (now.st_dev != snapshot_.st_dev || now.st_ino != snapshot_.st_ino || now.st_size != snapshot_.st_size
      || now.st_mtim.tv_sec != snapshot_.st_mtim.tv_sec || now.st_mtim.tv_nsec != snapshot_.st_mtim.tv_nsec
      || now.st_ctim.tv_sec != snapshot_.st_ctim.tv_sec || now.st_ctim.tv_nsec != snapshot_.st_ctim.tv_nsec) {
    return {FileCode::FILE_CHANGED,record_,line_,0,"CSV file changed between inference and scan"};
  }
  return {};
}
FileStatus CsvReader::rescan()
{
  FileStatus s = check_unchanged();
  if (!s.ok()) { return s; }
  if (fseeko(input_, start_, SEEK_SET) != 0) { return {FileCode::IO_ERROR,0,0,0,"cannot rewind CSV input"}; }
  clearerr(input_); eof_ = false; failure_ = {}; record_ = 0; line_ = 1; record_line_ = 1;
  unsigned char prefix[3];
  const size_t count = fread(prefix, 1, 3, input_);
  if (ferror(input_)) { return {FileCode::IO_ERROR,0,1,0,"cannot read CSV input"}; }
  if (count != 3 || prefix[0] != 0xef || prefix[1] != 0xbb || prefix[2] != 0xbf) {
    if (fseeko(input_,start_,SEEK_SET) != 0) { return {FileCode::IO_ERROR,0,1,0,"cannot rewind CSV input"}; }
  }
  clearerr(input_);
  return {};
}
FileStatus CsvReader::fail(FileCode code, size_t column, const char *message)
{
  failure_ = {code,record_+1,line_,column,message};
  return failure_;
}
FileStatus CsvReader::next_row(TextRow &out)
{
  out.clear();
  if (input_ == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV reader is not initialized"}; }
  if (!failure_.ok()) { return failure_; }
  if (eof_) { return {FileCode::END,record_,line_,0,{}}; }
  for (;;) {
    record_line_ = line_;
    TextRow row; std::string field;
    enum class State { START, UNQUOTED, QUOTED, AFTER_QUOTE };
    State state = State::START;
    bool quoted = false, touched = false, finish = false;
    size_t bytes = 0;
    const auto field_done = [&]() -> FileStatus {
      if (row.size() >= options_.max_columns) { return fail(FileCode::RESOURCE_LIMIT,row.size()+1,"CSV column limit exceeded"); }
      if (!valid_utf8(field)) { return fail(FileCode::INVALID_DATA,row.size()+1,"invalid UTF-8 or NUL byte"); }
      const bool is_null = !quoted && field == options_.null_string;
      row.push_back({std::move(field),is_null}); field.clear(); quoted = false; state = State::START;
      return {};
    };
    while (!finish) {
      const int c = fgetc(input_);
      if (c == EOF) {
        if (ferror(input_)) { return fail(FileCode::IO_ERROR,row.size()+1,"CSV input read failed"); }
        eof_ = true;
        if (state == State::QUOTED) { return fail(FileCode::INVALID_DATA,row.size()+1,"unterminated quoted CSV field"); }
        if (!touched && row.empty() && field.empty()) { return {FileCode::END,record_,line_,0,{}}; }
        FileStatus s = field_done(); if (!s.ok()) { return s; }
        finish = true; continue;
      }
      if (++bytes > options_.max_record_bytes) { return fail(FileCode::RESOURCE_LIMIT,row.size()+1,"CSV record byte limit exceeded"); }
      if (c == '\n') { ++line_; }
      if (state == State::QUOTED) {
        if (c == options_.quote) { state = State::AFTER_QUOTE; }
        else if (options_.escape != options_.quote && c == options_.escape) {
          const int next = fgetc(input_);
          if (next == EOF) { return fail(ferror(input_) ? FileCode::IO_ERROR : FileCode::INVALID_DATA,row.size()+1,"truncated CSV escape"); }
          if (++bytes > options_.max_record_bytes) { return fail(FileCode::RESOURCE_LIMIT,row.size()+1,"CSV record byte limit exceeded"); }
          if (next == '\n') { ++line_; }
          field.push_back(static_cast<char>(next));
        } else { field.push_back(static_cast<char>(c)); }
        continue;
      }
      if (state == State::AFTER_QUOTE && c == options_.quote && options_.escape == options_.quote) {
        field.push_back(static_cast<char>(c)); state = State::QUOTED; continue;
      }
      if (c == options_.delimiter) {
        touched = true; FileStatus s = field_done(); if (!s.ok()) { return s; } continue;
      }
      if (c == '\r' || c == '\n') {
        if (c == '\r') {
          const int next = fgetc(input_);
          if (next == '\n') { ++line_; if (++bytes > options_.max_record_bytes) { return fail(FileCode::RESOURCE_LIMIT,row.size()+1,"CSV record byte limit exceeded"); } }
          else {
            ++line_;
            if (next != EOF) { ungetc(next,input_); }
            else if (ferror(input_)) { return fail(FileCode::IO_ERROR,row.size()+1,"CSV input read failed"); }
          }
        }
        if (!touched && row.empty() && field.empty() && options_.skip_blank_lines) { break; }
        FileStatus s = field_done(); if (!s.ok()) { return s; }
        finish = true; continue;
      }
      if (state == State::AFTER_QUOTE) { return fail(FileCode::INVALID_DATA,row.size()+1,"unexpected byte after closing CSV quote"); }
      if (c == options_.quote) {
        if (state != State::START) { return fail(FileCode::INVALID_DATA,row.size()+1,"quote in unquoted CSV field"); }
        state = State::QUOTED; quoted = true; touched = true;
      } else { state = State::UNQUOTED; touched = true; field.push_back(static_cast<char>(c)); }
    }
    if (finish) { ++record_; out = std::move(row); return {}; }
  }
}
FileStatus CsvReader::next_batch(size_t limit, std::vector<TextRow> &out)
{
  out.clear();
  if (limit == 0 || limit > 65536) { return {FileCode::INVALID_ARGUMENT,record_,line_,0,"invalid CSV batch size"}; }
  FileStatus s = check_unchanged(); if (!s.ok()) { return s; }
  std::vector<TextRow> rows; size_t bytes = 0;
  while (rows.size() < limit) {
    TextRow row; s = next_row(row);
    if (s.code == FileCode::END) { break; }
    if (!s.ok()) { return s; }
    bytes += row_bytes(row); rows.push_back(std::move(row));
    const size_t next_record_bound = options_.max_record_bytes + options_.max_columns * sizeof(TextCell);
    if (bytes >= options_.max_batch_bytes - next_record_bound) { break; }
  }
  s = check_unchanged(); if (!s.ok()) { return s; }
  if (rows.empty()) { return {FileCode::END,record_,line_,0,{}}; }
  out = std::move(rows); return {};
}

FileStatus CsvTableScan::prepare(FILE *input, const CsvOptions &options, int64_t sample_size)
{
  prepared_ = false; options_ = options;
  if (sample_size != -1) { return {FileCode::INVALID_ARGUMENT,0,0,0,"only full-file sample_size=-1 is supported"}; }
  FileStatus s = reader_.init(input,options); if (!s.ok()) { return s; }
  TextRow row; s = reader_.next_row(row);
  if (s.code == FileCode::END) { return {FileCode::INVALID_DATA,0,1,0,"empty CSV has no schema"}; }
  if (!s.ok()) { return s; }
  std::vector<std::string> names;
  if (options.header) { names = column_names(row); }
  else { for (size_t i=0; i<row.size(); ++i) { names.push_back("column"+std::to_string(i)); } }
  s = schema_.init(names); if (!s.ok()) { return s; }
  if (!options.header) { s = schema_.observe(row,reader_.record_number(),reader_.record_line()); }
  if (!s.ok()) { return s; }
  for (;;) {
    s = reader_.next_row(row);
    if (s.code == FileCode::END) { break; }
    if (!s.ok()) { return s; }
    s = schema_.observe(row,reader_.record_number(),reader_.record_line()); if (!s.ok()) { return s; }
  }
  s = reader_.check_unchanged(); if (!s.ok()) { return s; }
  prepared_ = true; s = rescan(); if (!s.ok()) { prepared_ = false; }
  return s;
}
FileStatus CsvTableScan::rescan()
{
  if (!prepared_) { return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV schema has not been prepared"}; }
  FileStatus s = reader_.rescan(); if (!s.ok()) { return s; }
  if (options_.header) { TextRow ignored; s = reader_.next_row(ignored); }
  return s;
}
FileStatus CsvTableScan::next_batch(size_t limit, std::vector<ScalarRow> &out)
{
  out.clear();
  if (!prepared_) { return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV schema has not been prepared"}; }
  std::vector<TextRow> raw;
  FileStatus s = reader_.next_batch(limit,raw); if (!s.ok()) { return s; }
  std::vector<ScalarRow> rows;
  const uint64_t first_record = reader_.record_number()-raw.size()+1;
  for (size_t r=0; r<raw.size(); ++r) {
    if (raw[r].size() != columns().size()) { return {FileCode::INVALID_DATA,first_record+r,0,0,"CSV record width differs from schema"}; }
    ScalarRow row;
    for (size_t c=0; c<raw[r].size(); ++c) {
      ScalarValue value; s = convert_scalar(raw[r][c],columns()[c].type,value);
      if (!s.ok()) { s.record = first_record+r; s.column = c+1; return s; }
      row.push_back(std::move(value));
    }
    rows.push_back(std::move(row));
  }
  out = std::move(rows); return {};
}

FileStatus CsvWriter::init(FILE *output, const std::vector<std::string> &names, const CsvOptions &options)
{
  output_ = nullptr; failure_ = {}; record_ = 0;
  FileStatus s = validate_options(options); if (!s.ok()) { return s; }
  if (output == nullptr || names.empty() || names.size() > options.max_columns) {
    return {FileCode::INVALID_ARGUMENT,0,0,0,"invalid CSV output or columns"};
  }
  options_ = options; output_ = output; width_ = names.size();
  if (options.header) {
    TextRow header; for (const auto &name : names) { header.push_back({name,false}); }
    s = write_record(header);
    if (!s.ok()) { failure_ = s; }
  }
  return s;
}
FileStatus CsvWriter::write_record(const TextRow &row)
{
  if (output_ == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV writer is not initialized"}; }
  if (!failure_.ok()) { return failure_; }
  if (row.size() != width_) { return {FileCode::INVALID_DATA,record_+1,0,0,"CSV output width mismatch"}; }
  std::string encoded;
  const auto append = [&](char c) -> bool {
    if (encoded.size() >= options_.max_record_bytes) { return false; }
    encoded.push_back(c); return true;
  };
  for (size_t i=0; i<row.size(); ++i) {
    if (i != 0 && !append(options_.delimiter)) { return {FileCode::RESOURCE_LIMIT,record_+1,0,i+1,"CSV output record byte limit exceeded"}; }
    const auto &cell = row[i];
    const std::string &text = cell.is_null ? options_.null_string : cell.text;
    if (!valid_utf8(text)) { return {FileCode::INVALID_DATA,record_+1,0,i+1,"invalid UTF-8 or NUL byte"}; }
    bool quote = !cell.is_null && (text == options_.null_string || text.find_first_of(std::string()+options_.delimiter+options_.quote+options_.escape+"\r\n") != std::string::npos);
    if (quote && !append(options_.quote)) { return {FileCode::RESOURCE_LIMIT,record_+1,0,i+1,"CSV output record byte limit exceeded"}; }
    for (char c : text) {
      if (quote && (c == options_.quote || (options_.escape != options_.quote && c == options_.escape))) {
        if (!append(options_.escape)) { return {FileCode::RESOURCE_LIMIT,record_+1,0,i+1,"CSV output record byte limit exceeded"}; }
      }
      if (!append(c)) { return {FileCode::RESOURCE_LIMIT,record_+1,0,i+1,"CSV output record byte limit exceeded"}; }
    }
    if (quote && !append(options_.quote)) { return {FileCode::RESOURCE_LIMIT,record_+1,0,i+1,"CSV output record byte limit exceeded"}; }
  }
  if (!append('\n')) { return {FileCode::RESOURCE_LIMIT,record_+1,0,0,"CSV output record byte limit exceeded"}; }
  if (fwrite(encoded.data(),1,encoded.size(),output_) != encoded.size()) {
    failure_ = {FileCode::IO_ERROR,record_+1,0,0,"CSV output write failed"}; return failure_;
  }
  ++record_; return {};
}
FileStatus CsvWriter::write_row(const TextRow &row)
{
  const FileStatus s = write_record(row);
  if (!s.ok()) { failure_ = s; }
  return s;
}
FileStatus CsvWriter::finish()
{
  if (output_ == nullptr) { return {FileCode::INVALID_ARGUMENT,0,0,0,"CSV writer is not initialized"}; }
  if (!failure_.ok()) { return failure_; }
  if (fflush(output_) != 0) { failure_ = {FileCode::IO_ERROR,record_,0,0,"CSV output flush failed"}; return failure_; }
  return {};
}

}}} // namespace oceanbase::sql::file_sql
