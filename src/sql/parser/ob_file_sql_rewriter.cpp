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

#define USING_LOG_PREFIX SQL_PARSER

#include "sql/parser/ob_file_sql_rewriter.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <string>
#include <vector>

#include "lib/ob_errno.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/utility/ob_macro_utils.h"

namespace oceanbase
{
namespace sql
{
namespace
{

enum FileType
{
  FT_NULL = 0,
  FT_BIGINT,
  FT_DOUBLE,
  FT_BOOL,
  FT_DATE,
  FT_DATETIME,
  FT_VARCHAR
};

struct Column
{
  std::string name;
  FileType type;
  bool nullable;
  Column() : type(FT_NULL), nullable(false) {}
  explicit Column(const std::string &n) : name(n), type(FT_NULL), nullable(false) {}
};

typedef std::vector<std::vector<std::string>> Rows;

const char *type_name(FileType t)
{
  switch (t) {
    case FT_BIGINT: return "BIGINT";
    case FT_DOUBLE: return "DOUBLE";
    case FT_BOOL: return "BOOLEAN";
    case FT_DATE: return "DATE";
    case FT_DATETIME: return "DATETIME";
    case FT_VARCHAR: return "VARCHAR";
    default: return "VARCHAR";
  }
}

FileType merge_type(FileType a, FileType b)
{
  if (FT_NULL == a) return b;
  if (FT_NULL == b) return a;
  if (a == b) return a;
  // numeric widening
  const bool a_num = (FT_BIGINT == a || FT_DOUBLE == a);
  const bool b_num = (FT_BIGINT == b || FT_DOUBLE == b);
  if (a_num && b_num) return FT_DOUBLE;
  if ((FT_DATE == a && FT_DATETIME == b) || (FT_DATETIME == a && FT_DATE == b)) return FT_DATETIME;
  if ((FT_BOOL == a && FT_BIGINT == b) || (FT_BIGINT == a && FT_BOOL == b)) return FT_BIGINT;
  if ((FT_BOOL == a && FT_DOUBLE == b) || (FT_DOUBLE == a && FT_BOOL == b)) return FT_DOUBLE;
  return FT_VARCHAR;
}

bool is_all_digits(const std::string &s)
{
  if (s.empty()) return false;
  size_t i = ('-' == s[0] || '+' == s[0]) ? 1 : 0;
  if (i >= s.size()) return false;
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

bool is_double_text(const std::string &s)
{
  if (s.empty()) return false;
  char *end = nullptr;
  const double v = strtod(s.c_str(), &end);
  (void)v;
  if (nullptr == end || '\0' != *end) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if ('.' == s[i] || 'e' == s[i] || 'E' == s[i]) return true;
  }
  return false;
}

bool is_date_text(const std::string &s)
{
  if (s.size() != 10) return false;
  if ('-' != s[4] || '-' != s[7]) return false;
  for (size_t i = 0; i < s.size(); ++i) {
    if (4 == i || 7 == i) continue;
    if (s[i] < '0' || s[i] > '9') return false;
  }
  return true;
}

bool is_datetime_text(const std::string &s)
{
  if (s.size() < 19) return false;
  if ('-' != s[4] || '-' != s[7]) return false;
  if (' ' != s[10] && 'T' != s[10]) return false;
  if (':' != s[13] || ':' != s[16]) return false;
  return true;
}

std::string lower_ascii(const std::string &s);

FileType infer_text_type(const std::string &raw)
{
  const std::string s = raw;
  const std::string low = lower_ascii(s);
  if (s.empty()) return FT_NULL;
  if ("true" == low || "false" == low) return FT_BOOL;
  if (is_all_digits(s)) {
    // CSV 0/1 columns are booleans in the reference behaviour
    if (1 == s.size() && ('0' == s[0] || '1' == s[0])) return FT_BOOL;
    return FT_BIGINT;
  }
  if (is_double_text(s)) return FT_DOUBLE;
  if (is_date_text(s)) return FT_DATE;
  if (is_datetime_text(s)) return FT_DATETIME;
  return FT_VARCHAR;
}

std::string lower_ascii(const std::string &s)
{
  std::string r(s);
  for (size_t i = 0; i < r.size(); ++i) {
    if (r[i] >= 'A' && r[i] <= 'Z') r[i] = static_cast<char>(r[i] - 'A' + 'a');
  }
  return r;
}

std::string trim(const std::string &s)
{
  size_t b = 0;
  size_t e = s.size();
  while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\n' || s[b] == '\r')) ++b;
  while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\n' || s[e - 1] == '\r')) --e;
  return s.substr(b, e - b);
}

// unquote a single quoted SQL literal, doubling means an escaped quote
std::string unquote(const std::string &s)
{
  if (s.size() < 2) return s;
  const char q = s[0];
  if ('\'' != q && '"' != q) return s;
  std::string r;
  for (size_t i = 1; i + 1 < s.size(); ++i) {
    if (s[i] == q && i + 2 < s.size() && s[i + 1] == q) {
      r += q;
      ++i;
    } else if ('\\' == s[i] && i + 2 < s.size()) {
      r += s[i + 1];
      ++i;
    } else {
      r += s[i];
    }
  }
  return r;
}

std::string json_escape(const std::string &s)
{
  std::string r;
  r.reserve(s.size() + 8);
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    switch (c) {
      case '"': r += "\\\""; break;
      case '\\': r += "\\\\"; break;
      case '\n': r += "\\n"; break;
      case '\r': r += "\\r"; break;
      case '\t': r += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          r += buf;
        } else {
          r += static_cast<char>(c);
        }
        break;
    }
  }
  return r;
}

std::string sql_quote(const std::string &s)
{
  std::string r("'");
  for (size_t i = 0; i < s.size(); ++i) {
    if ('\'' == s[i]) r += "''";
    else r += s[i];
  }
  r += "'";
  return r;
}

int read_whole_file(const std::string &path, std::string &content)
{
  int ret = OB_SUCCESS;
  FILE *fp = ::fopen(path.c_str(), "rb");
  if (nullptr == fp) {
    ret = OB_FILE_NOT_EXIST;
  } else {
    char buf[65536];
    size_t n = 0;
    while ((n = ::fread(buf, 1, sizeof(buf), fp)) > 0) {
      content.append(buf, n);
    }
    ::fclose(fp);
  }
  return ret;
}

// ---------------------------------------------------------------- CSV

void parse_csv(const std::string &content, char delim, Rows &rows)
{
  std::vector<std::string> row;
  std::string field;
  bool in_quote = false;
  bool field_started = false;
  for (size_t i = 0; i < content.size(); ++i) {
    const char c = content[i];
    if (in_quote) {
      if ('"' == c) {
        if (i + 1 < content.size() && '"' == content[i + 1]) {
          field += '"';
          ++i;
        } else {
          in_quote = false;
        }
      } else {
        field += c;
      }
    } else if ('"' == c && !field_started) {
      in_quote = true;
      field_started = true;
    } else if (c == delim) {
      row.push_back(field);
      field.clear();
      field_started = false;
    } else if ('\n' == c) {
      row.push_back(field);
      field.clear();
      field_started = false;
      rows.push_back(row);
      row.clear();
    } else if ('\r' == c) {
      // handled by the following '\n'
    } else {
      field += c;
      field_started = true;
    }
  }
  if (!field.empty() || !row.empty()) {
    row.push_back(field);
    rows.push_back(row);
  }
}

void make_unique_headers(std::vector<Column> &cols)
{
  for (size_t i = 0; i < cols.size(); ++i) {
    int suffix = 2;
    for (size_t j = 0; j < i; ++j) {
      if (cols[j].name == cols[i].name) {
        cols[i].name = cols[i].name + "__" + std::to_string(suffix);
        ++suffix;
      }
    }
  }
}

int load_csv(const std::string &path, bool header, char delim,
             std::vector<Column> &cols, Rows &data)
{
  int ret = OB_SUCCESS;
  std::string content;
  Rows raw;
  if (OB_FAIL(read_whole_file(path, content))) {
  } else {
    parse_csv(content, delim, raw);
    // drop trailing empty rows
    while (!raw.empty() && raw.back().size() == 1 && raw.back()[0].empty()) {
      raw.pop_back();
    }
    if (raw.empty()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      const std::vector<std::string> &first = header ? raw[0] : raw[0];
      const size_t start = header ? 1 : 0;
      std::vector<std::string> names;
      if (header) {
        names = first;
      } else {
        for (size_t i = 0; i < first.size(); ++i) {
          names.push_back("c" + std::to_string(i + 1));
        }
      }
      for (size_t i = 0; i < names.size(); ++i) {
        cols.push_back(Column(trim(names[i])));
      }
      make_unique_headers(cols);
      for (size_t r = start; r < raw.size(); ++r) {
        std::vector<std::string> row = raw[r];
        row.resize(cols.size());
        data.push_back(row);
        for (size_t c = 0; c < cols.size(); ++c) {
          const std::string cell = trim(row[c]);
          if (cell.empty()) cols[c].nullable = true;
          cols[c].type = merge_type(cols[c].type, infer_text_type(cell));
        }
      }
      for (size_t c = 0; c < cols.size(); ++c) {
        if (FT_NULL == cols[c].type) cols[c].type = FT_VARCHAR;
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------- JSONL

// Splits a flat JSON object into key/value raw texts.  Nested containers are
// rejected because the reference behaviour only supports flat documents.
bool parse_flat_object(const std::string &line, std::vector<std::pair<std::string, std::string>> &out)
{
  size_t i = 0;
  const size_t n = line.size();
  while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i >= n || '{' != line[i]) return false;
  ++i;
  while (i < n) {
    while (i < n && (line[i] == ' ' || line[i] == '\t' || line[i] == ',')) ++i;
    if (i >= n) return false;
    if ('}' == line[i]) return true;
    if ('"' != line[i]) return false;
    std::string key;
    ++i;
    while (i < n && line[i] != '"') {
      if ('\\' == line[i] && i + 1 < n) {
        key += line[i];
        ++i;
      }
      key += line[i];
      ++i;
    }
    if (i >= n) return false;
    ++i;  // closing quote
    while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= n || ':' != line[i]) return false;
    ++i;
    while (i < n && (line[i] == ' ' || line[i] == '\t')) ++i;
    if (i >= n) return false;
    std::string value;
    if ('"' == line[i]) {
      value += line[i++];
      while (i < n && line[i] != '"') {
        if ('\\' == line[i] && i + 1 < n) {
          value += line[i];
          ++i;
        }
        value += line[i];
        ++i;
      }
      if (i >= n) return false;
      value += line[i++];
    } else if ('{' == line[i] || '[' == line[i]) {
      return false;  // nested container is not supported
    } else {
      while (i < n && line[i] != ',' && line[i] != '}') {
        value += line[i];
        ++i;
      }
      value = trim(value);
    }
    out.push_back(std::make_pair(key, value));
  }
  return false;
}

FileType infer_json_type(const std::string &raw)
{
  const std::string s = trim(raw);
  if (s.empty() || "null" == s) return FT_NULL;
  if ("true" == s || "false" == s) return FT_BOOL;
  if (!s.empty() && '"' == s[0]) {
    const std::string inner = unquote(s);
    if (is_date_text(inner)) return FT_DATE;
    if (is_datetime_text(inner)) return FT_DATETIME;
    return FT_VARCHAR;
  }
  if (is_all_digits(s)) {
    if (1 == s.size() && ('0' == s[0] || '1' == s[0])) return FT_BOOL;
    return FT_BIGINT;
  }
  if (is_double_text(s)) return FT_DOUBLE;
  return FT_VARCHAR;
}

int load_jsonl(const std::string &path, std::vector<Column> &cols, Rows &data)
{
  int ret = OB_SUCCESS;
  std::string content;
  if (OB_FAIL(read_whole_file(path, content))) {
  } else {
    std::vector<std::vector<std::pair<std::string, std::string>>> objects;
    size_t pos = 0;
    while (pos <= content.size()) {
      size_t nl = content.find('\n', pos);
      std::string line = content.substr(pos, (std::string::npos == nl) ? std::string::npos : nl - pos);
      if (std::string::npos == nl) pos = content.size() + 1;
      else pos = nl + 1;
      line = trim(line);
      if (line.empty()) continue;
      std::vector<std::pair<std::string, std::string>> kv;
      if (!parse_flat_object(line, kv)) {
        ret = OB_INVALID_DATA;
        break;
      }
      objects.push_back(kv);
    }
    if (OB_SUCC(ret)) {
      if (objects.empty()) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        for (size_t o = 0; o < objects.size(); ++o) {
          for (size_t k = 0; k < objects[o].size(); ++k) {
            const std::string name = unquote(objects[o][k].first);
            bool found = false;
            for (size_t c = 0; c < cols.size(); ++c) {
              if (cols[c].name == name) {
                found = true;
                break;
              }
            }
            if (!found) cols.push_back(Column(name));
          }
        }
        for (size_t o = 0; o < objects.size(); ++o) {
          std::vector<std::string> row(cols.size(), std::string());
          for (size_t k = 0; k < objects[o].size(); ++k) {
            const std::string name = unquote(objects[o][k].first);
            for (size_t c = 0; c < cols.size(); ++c) {
              if (cols[c].name == name) {
                row[c] = objects[o][k].second;
                cols[c].type = merge_type(cols[c].type, infer_json_type(row[c]));
                break;
              }
            }
          }
          data.push_back(row);
        }
        for (size_t c = 0; c < cols.size(); ++c) {
          if (FT_NULL == cols[c].type) cols[c].type = FT_VARCHAR;
        }
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------- glob

int load_glob(const std::string &pattern, std::vector<Column> &cols, Rows &data)
{
  int ret = OB_SUCCESS;
  Column file_col("file");
  file_col.type = FT_VARCHAR;
  cols.push_back(file_col);
  const size_t slash = pattern.find_last_of('/');
  if (std::string::npos == slash) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const std::string dir = pattern.substr(0, slash + 1);
    const std::string mask = pattern.substr(slash + 1);
    bool has_wild = mask.find('*') != std::string::npos || mask.find('?') != std::string::npos;
    if (!has_wild) {
      // exact path: emit it when it exists
      FILE *fp = ::fopen(pattern.c_str(), "rb");
      if (nullptr != fp) {
        ::fclose(fp);
        data.push_back(std::vector<std::string>(1, pattern));
      }
    } else {
      // simple mask matcher over one directory level
      std::string cmd_dir = dir;
      DIR *d = nullptr;
      struct dirent *ent = nullptr;
      if (nullptr == (d = ::opendir(cmd_dir.c_str()))) {
        ret = OB_FILE_NOT_EXIST;
      } else {
        while (nullptr != (ent = ::readdir(d))) {
          const std::string name(ent->d_name);
          if ("." == name || ".." == name) continue;
          // wildcard match
          size_t mi = 0, ni = 0;
          bool ok = true;
          size_t star = std::string::npos, mark = 0;
          while (ni < name.size()) {
            if (mi < mask.size() && (mask[mi] == '?' || mask[mi] == name[ni])) {
              ++mi;
              ++ni;
            } else if (mi < mask.size() && mask[mi] == '*') {
              star = mi++;
              mark = ni;
            } else if (std::string::npos != star) {
              mi = star + 1;
              ni = ++mark;
            } else {
              ok = false;
              break;
            }
          }
          while (mi < mask.size() && mask[mi] == '*') ++mi;
          if (ok && mi == mask.size()) {
            data.push_back(std::vector<std::string>(1, dir + name));
          }
        }
        ::closedir(d);
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------- SQL text

std::string build_json(const std::vector<Column> &cols, const Rows &data, bool jsonl_style)
{
  std::string json("[");
  for (size_t r = 0; r < data.size(); ++r) {
    if (r > 0) json += ",";
    json += "{";
    for (size_t c = 0; c < cols.size(); ++c) {
      if (c > 0) json += ",";
      json += "\"" + json_escape(cols[c].name) + "\":";
      std::string v = (c < data[r].size()) ? data[r][c] : std::string();
      if (jsonl_style) {
        v = trim(v);
        if (v.empty() || "null" == v) {
          json += "null";
        } else {
          json += v;  // already valid JSON text
        }
      } else {
        const std::string tv = trim(v);
        if (tv.empty()) {
          json += "null";
        } else if (FT_BOOL == cols[c].type) {
          const std::string low = lower_ascii(tv);
          json += (("0" == low || "false" == low || "f" == low) ? "false" : "true");
        } else if (FT_BIGINT == cols[c].type || FT_DOUBLE == cols[c].type) {
          json += tv;
        } else if (FT_DATETIME == cols[c].type && tv.size() > 10 && 'T' == tv[10]) {
          std::string norm(tv);
          norm[10] = ' ';
          json += "\"" + json_escape(norm) + "\"";
        } else {
          json += "\"" + json_escape(tv) + "\"";
        }
      }
    }
    json += "}";
  }
  json += "]";
  return json;
}

std::string sql_quote_json(const std::string &s)
{
  std::string r("'");
  for (size_t i = 0; i < s.size(); ++i) {
    if ('\'' == s[i]) r += "''";
    else if ('\\' == s[i]) r += "\\\\";
    else r += s[i];
  }
  r += "'";
  return r;
}

std::string build_json_table(const std::vector<Column> &cols, const std::string &json)
{
  std::string sql = "JSON_TABLE(" + sql_quote_json(json) + ", '$[*]' COLUMNS(";
  for (size_t c = 0; c < cols.size(); ++c) {
    if (c > 0) sql += ", ";
    sql += "`" + cols[c].name + "` " + type_name(cols[c].type);
    if (FT_VARCHAR == cols[c].type) sql += "(4000)";
    sql += " PATH '$.\"" + cols[c].name + "\"'";
  }
  sql += "))";
  return sql;
}

std::string build_describe(const std::vector<Column> &cols)
{
  std::string sql;
  for (size_t c = 0; c < cols.size(); ++c) {
    if (c > 0) sql += " UNION ALL ";
    const std::string type_text = (FT_VARCHAR == cols[c].type) ? "VARCHAR" : type_name(cols[c].type);
    const std::string nullable = cols[c].nullable ? "YES" : "NO";
    sql += "SELECT " + sql_quote(cols[c].name) + " AS column_name, " + sql_quote(type_text) +
           " AS column_type, " + sql_quote(nullable) + " AS `null`, 'NULL' AS `key`, "
           "'NULL' AS `default`, 'NULL' AS `extra`";
  }
  return sql;
}

struct CallArgs
{
  std::string path;
  std::string format;
  bool header;
  long sample_size;
  char delim;
  bool has_format;
  CallArgs() : header(true), sample_size(-1), delim(','), has_format(false) {}
};

bool split_args(const std::string &body, std::vector<std::string> &args)
{
  std::string cur;
  int depth = 0;
  bool in_str = false;
  bool in_bt = false;
  for (size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (in_str) {
      cur += c;
      if ('\'' == c) {
        if (i + 1 < body.size() && '\'' == body[i + 1]) {
          cur += body[++i];
        } else {
          in_str = false;
        }
      }
      continue;
    }
    if (in_bt) {
      cur += c;
      if ('`' == c) in_bt = false;
      continue;
    }
    if ('\'' == c) { in_str = true; cur += c; continue; }
    if ('`' == c) { in_bt = true; cur += c; continue; }
    if ('(' == c) { ++depth; cur += c; continue; }
    if (')' == c) { --depth; cur += c; continue; }
    if (',' == c && 0 == depth) {
      args.push_back(trim(cur));
      cur.clear();
      continue;
    }
    cur += c;
  }
  if (!trim(cur).empty()) args.push_back(trim(cur));
  return true;
}

int parse_call_args(const std::vector<std::string> &args, CallArgs &out, bool csv_like)
{
  int ret = OB_SUCCESS;
  bool first = true;
  for (size_t i = 0; i < args.size() && OB_SUCC(ret); ++i) {
    const std::string a = args[i];
    const size_t eq = a.find('=');
    if (std::string::npos != eq) {
      const std::string k = lower_ascii(trim(a.substr(0, eq)));
      const std::string v = trim(a.substr(eq + 1));
      if ("header" == k) {
        out.header = (lower_ascii(unquote(v)) != "false" && v != "0");
      } else if ("sample_size" == k) {
        out.sample_size = atol(unquote(v).c_str());
      } else if ("delimiter" == k || "sep" == k) {
        const std::string d = unquote(v);
        if (!d.empty()) out.delim = d[0];
      } else if ("format" == k) {
        out.format = lower_ascii(unquote(v));
        out.has_format = true;
      }
    } else if (first) {
      out.path = unquote(a);
      first = false;
    } else if (csv_like && i == 1 && !a.empty() && ('\'' == a[0] || '"' == a[0])) {
      out.format = lower_ascii(unquote(a));
      out.has_format = true;
    }
  }
  if (out.path.empty()) ret = OB_INVALID_ARGUMENT;
  if (OB_SUCC(ret) && std::string::npos != out.path.find("..")) ret = OB_INVALID_ARGUMENT;
  if (OB_SUCC(ret) && (out.path.empty() || '/' != out.path[0])) ret = OB_INVALID_ARGUMENT;
  return ret;
}

bool has_alias_after(const std::string &sql, size_t close_pos)
{
  static const char *kKeywords[] = {"where", "order", "group", "limit", "having", "join",
                                    "inner", "left", "right", "full", "cross", "natural",
                                    "on", "union", "for", "use", "window", "offset", "fetch",
                                    "straight_join", "as", "partition", ""};
  size_t i = close_pos + 1;
  while (i < sql.size() && (sql[i] == ' ' || sql[i] == '\t' || sql[i] == '\n' || sql[i] == '\r')) ++i;
  if (i >= sql.size()) return false;
  const char c = sql[i];
  if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || '_' == c || '`' == c)) return false;
  size_t j = i;
  if ('`' == c) {
    return true;  // quoted alias
  }
  while (j < sql.size() && ((sql[j] >= 'a' && sql[j] <= 'z') || (sql[j] >= 'A' && sql[j] <= 'Z') ||
                            (sql[j] >= '0' && sql[j] <= '9') || '_' == sql[j])) {
    ++j;
  }
  const std::string word = lower_ascii(sql.substr(i, j - i));
  for (size_t k = 0; kKeywords[k][0] != '\0'; ++k) {
    if (word == kKeywords[k]) return false;
  }
  return true;
}

int find_call_end(const std::string &sql, size_t open_pos, size_t &close_pos)
{
  int ret = OB_SUCCESS;
  int depth = 0;
  bool in_str = false;
  close_pos = std::string::npos;
  for (size_t i = open_pos; i < sql.size(); ++i) {
    const char c = sql[i];
    if (in_str) {
      if ('\'' == c) {
        if (i + 1 < sql.size() && '\'' == sql[i + 1]) ++i;
        else in_str = false;
      }
      continue;
    }
    if ('\'' == c) { in_str = true; continue; }
    if ('(' == c) ++depth;
    else if (')' == c) {
      --depth;
      if (0 == depth) { close_pos = i; break; }
    }
  }
  if (std::string::npos == close_pos) ret = OB_INVALID_ARGUMENT;
  return ret;
}

}  // namespace

int ObFileSQLRewriter::maybe_rewrite(ObIAllocator &alloc,
                                     const common::ObString &stmt,
                                     common::ObString &out,
                                     bool &rewritten)
{
  int ret = OB_SUCCESS;
  rewritten = false;
  if (stmt.empty()) {
  } else {
    const std::string sql(stmt.ptr(), stmt.length());
    const std::string lowered = lower_ascii(sql);
    // only statements that actually reference a file table function are touched
    size_t probe = lowered.find("read_csv(");
    if (std::string::npos == probe) probe = lowered.find("read_json(");
    if (std::string::npos == probe) probe = lowered.find("read_ndjson(");
    if (std::string::npos == probe) probe = lowered.find("read_parquet(");
    if (std::string::npos == probe) probe = lowered.find("glob(");
    if (std::string::npos == probe) {
      // nothing to do
    } else {
      std::string built;
      bool ok = true;
      std::vector<std::string> schema_names;
      std::vector<FileType> schema_types;
      std::vector<bool> schema_nullable;
      size_t i = 0;
      const bool is_describe =
          (0 == lowered.compare(0, 8, "describe") || 0 == lowered.compare(0, 4, "desc"));
      while (i < sql.size() && ok) {
        // copy a quoted string verbatim
        if ('\'' == sql[i] || '"' == sql[i] || '`' == sql[i]) {
          const char q = sql[i];
          built += sql[i++];
          while (i < sql.size()) {
            built += sql[i];
            if (q == sql[i]) {
              if (i + 1 < sql.size() && q == sql[i + 1]) {
                built += sql[i + 1];
                i += 2;
                continue;
              }
              ++i;
              break;
            }
            ++i;
          }
          continue;
        }
        bool matched = false;
        static const char *kFuncs[] = {"read_csv", "read_json", "read_ndjson",
                                       "read_parquet", "glob"};
        for (size_t f = 0; f < sizeof(kFuncs) / sizeof(kFuncs[0]) && !matched; ++f) {
          const size_t len = strlen(kFuncs[f]);
          if (i + len >= sql.size()) continue;
          if (0 != lowered.compare(i, len, kFuncs[f])) continue;
          // must be a whole word followed by '('
          if (i > 0) {
            const char p = lowered[i - 1];
            if ((p >= 'a' && p <= 'z') || (p >= '0' && p <= '9') || '_' == p) continue;
          }
          size_t j = i + len;
          while (j < sql.size() && (' ' == sql[j] || '\t' == sql[j] || '\n' == sql[j])) ++j;
          if (j >= sql.size() || '(' != sql[j]) continue;

          size_t close_pos = std::string::npos;
          std::vector<std::string> args;
          CallArgs ca;
          std::vector<Column> cols;
          Rows data;
          if (OB_FAIL(find_call_end(sql, j, close_pos))) { ok = false; break; }
          split_args(sql.substr(j + 1, close_pos - j - 1), args);
          const bool is_glob = (0 == strcmp(kFuncs[f], "glob"));
          const bool is_csv = (0 == strcmp(kFuncs[f], "read_csv"));
          const bool is_parquet = (0 == strcmp(kFuncs[f], "read_parquet"));
          if (OB_FAIL(parse_call_args(args, ca, is_csv))) { ok = false; break; }
          if (is_parquet) { ok = false; break; }  // not implemented yet
          int load_ret = OB_SUCCESS;
          if (is_glob) {
            load_ret = load_glob(ca.path, cols, data);
          } else if (is_csv) {
            if (ca.has_format && "csv" != ca.format) { ok = false; break; }  // invalid format
            if (-1 != ca.sample_size && 0 != ca.sample_size) { ok = false; break; }
            load_ret = load_csv(ca.path, ca.header, ca.delim, cols, data);
          } else {
            if (ca.has_format && "newline_delimited" != ca.format && "jsonl" != ca.format) {
              ok = false;
              break;
            }
            load_ret = load_jsonl(ca.path, cols, data);
          }
          if (OB_FAIL(load_ret)) { ok = false; break; }
          const std::string json = build_json(cols, data, !is_csv && !is_glob);
          built += build_json_table(cols, json);
          if (!has_alias_after(sql, close_pos)) {
            built += " AS __file_sql";
          }
          // remember the schema for a possible DESCRIBE rewrite
          if (schema_names.empty()) {
            for (size_t c = 0; c < cols.size(); ++c) {
              schema_names.push_back(cols[c].name);
              schema_types.push_back(cols[c].type);
              schema_nullable.push_back(cols[c].nullable);
            }
          }
          if (is_describe) {
            // the describe prefix is dropped later, the JSON table is not needed
            built = "SELECT 1";
            // stop scanning, the statement is fully synthesised below
            i = sql.size();
            matched = true;
            break;
          }
          i = close_pos + 1;
          matched = true;
        }
        if (!matched) {
          built += sql[i];
          ++i;
        }
      }
      if (ok && !schema_names.empty()) {
        std::string final_sql;
        if (is_describe) {
          std::vector<Column> cols;
          for (size_t c = 0; c < schema_names.size(); ++c) {
            Column col(schema_names[c]);
            col.type = schema_types[c];
            col.nullable = schema_nullable[c];
            cols.push_back(col);
          }
          final_sql = build_describe(cols);
        } else {
          final_sql = built;
        }
        char *buf = static_cast<char *>(alloc.alloc(final_sql.size()));
        if (OB_ISNULL(buf)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMCPY(buf, final_sql.data(), final_sql.size());
          out.assign_ptr(buf, static_cast<int32_t>(final_sql.size()));
          rewritten = true;
          LOG_TRACE("file sql rewritten", "len", out.length(),
                    "sql", ObString(out.length(), buf));
        }
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
