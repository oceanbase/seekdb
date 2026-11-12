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
#include "sql/engine/basic/file_sql/file_sql_types.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>

using namespace oceanbase::sql::file_sql;
namespace {
size_t checks = 0;
void require(bool value, const char *expression, int line)
{
  ++checks;
  if (!value) { std::cerr << "FAIL line " << line << ": " << expression << '\n'; std::exit(1); }
}
}
#define CHECK(x) require((x),#x,__LINE__)
int main()
{
  const std::vector<std::pair<std::string,ScalarType>> cases = {
    {"0",ScalarType::BIGINT},{"+12",ScalarType::BIGINT},{"-9223372036854775808",ScalarType::BIGINT},
    {"9223372036854775807",ScalarType::BIGINT},{"9223372036854775808",ScalarType::VARCHAR},
    {"-9223372036854775809",ScalarType::VARCHAR},{"99999999999999999999999",ScalarType::VARCHAR},
    {"0.0",ScalarType::DOUBLE},{"-.5",ScalarType::DOUBLE},{"1e-3",ScalarType::DOUBLE},
    {"+1.5E2",ScalarType::DOUBLE},{"1e309",ScalarType::VARCHAR},{"1e-9999",ScalarType::VARCHAR},
    {"NaN",ScalarType::VARCHAR},{"Infinity",ScalarType::VARCHAR},{"1x",ScalarType::VARCHAR},
    {"0x10",ScalarType::VARCHAR},{"1e",ScalarType::VARCHAR},{"1 ",ScalarType::VARCHAR},
    {"true",ScalarType::BOOLEAN},{"false",ScalarType::BOOLEAN},{"TRUE",ScalarType::BOOLEAN},
    {"2024-02-29",ScalarType::DATE},{"2000-02-29",ScalarType::DATE},
    {"1900-02-29",ScalarType::VARCHAR},{"2026-02-29",ScalarType::VARCHAR},
    {"2026-04-31",ScalarType::VARCHAR},{"2026-13-01",ScalarType::VARCHAR},
    {"0000-01-01",ScalarType::VARCHAR},{"2026-08-03T12:13:14",ScalarType::DATETIME},
    {"2026-08-03 23:59:59",ScalarType::DATETIME},{"2026-08-03 24:00:00",ScalarType::VARCHAR},
    {"2026-08-03 12:60:00",ScalarType::VARCHAR},{"2026-08-03 12:00:60",ScalarType::VARCHAR},
    {"",ScalarType::VARCHAR},{"中文🙂",ScalarType::VARCHAR}
  };
  for (const auto &c : cases) { CHECK(infer_scalar_type(c.first) == c.second); }
  CHECK(valid_utf8("中文🙂"));
  for (const std::string &s : {std::string("\xc0\xaf"),std::string("\xed\xa0\x80"),std::string("\xf4\x90\x80\x80"),std::string("a\0b",3),std::string("\xe4\xb8")}) {
    CHECK(!valid_utf8(s));
  }
  const ScalarType types[] = {ScalarType::NULL_VALUE,ScalarType::BIGINT,ScalarType::DOUBLE,
      ScalarType::BOOLEAN,ScalarType::VARCHAR,ScalarType::DATE,ScalarType::DATETIME};
  for (ScalarType a : types) {
    CHECK(widen_scalar_type(a,a) == a);
    CHECK(widen_scalar_type(a,ScalarType::NULL_VALUE) == a);
    for (ScalarType b : types) {
      CHECK(widen_scalar_type(a,b) == widen_scalar_type(b,a));
      for (ScalarType c : types) {
        CHECK(widen_scalar_type(widen_scalar_type(a,b),c) == widen_scalar_type(a,widen_scalar_type(b,c)));
      }
    }
  }
  SchemaInference schema;
  CHECK(schema.init({"n","day","empty","allnull"}).ok());
  CHECK(schema.observe({{"1",false},{"2026-08-01",false},{"",false},{"",true}}).ok());
  CHECK(schema.observe({{"1.5",false},{"2026-08-03T12:13:14",false},{"",true},{"",true}}).ok());
  CHECK(schema.columns()[0].type == ScalarType::DOUBLE);
  CHECK(schema.columns()[1].type == ScalarType::DATETIME);
  CHECK(schema.columns()[2].type == ScalarType::VARCHAR && schema.columns()[2].nullable);
  CHECK(schema.columns()[3].type == ScalarType::NULL_VALUE && schema.columns()[3].nullable);
  CHECK(!schema.observe({{"x",false}},5,7).ok());
  CHECK(schema.row_count() == 2);
  ScalarValue value;
  CHECK(convert_scalar({"2026-08-03T12:13:14",false},ScalarType::DATETIME,value).ok());
  CHECK(value.text == "2026-08-03 12:13:14");
  CHECK(convert_scalar({"2026-08-01",false},ScalarType::DATETIME,value).ok());
  CHECK(value.text == "2026-08-01 00:00:00");
  CHECK(convert_scalar({"+9223372036854775807",false},ScalarType::BIGINT,value).ok());
  CHECK(value.integer == INT64_MAX);
  CHECK(convert_scalar({"bad",false},ScalarType::DOUBLE,value).code == FileCode::INVALID_DATA);
  CHECK(convert_scalar({"",true},ScalarType::BIGINT,value).ok() && value.is_null);
  CHECK(convert_scalar({"false",false},ScalarType::BOOLEAN,value).ok() && value.integer == 0);
  // Reordering a full-file observation must not change schema or nullability.
  std::vector<TextCell> data = {{"1",false},{"2.5",false},{"",true},{"word",false}};
  std::sort(data.begin(),data.end(),[](const TextCell &a,const TextCell &b){return a.text<b.text;});
  do {
    CHECK(schema.init({"v"}).ok());
    for (const auto &c : data) { CHECK(schema.observe({c}).ok()); }
    CHECK(schema.columns()[0].type == ScalarType::VARCHAR && schema.columns()[0].nullable);
  } while (std::next_permutation(data.begin(),data.end(),[](const TextCell &a,const TextCell &b){return a.text<b.text;}));
  std::cout << "PASS type inference: " << checks << " checks\n";
}
