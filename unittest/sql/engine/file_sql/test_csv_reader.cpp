#include "sql/engine/file_sql/csv_reader.h"
#include <cassert>
#include <iostream>
#include <sstream>
#include <random>
using namespace oceanbase::sql::file_sql;

int main()
{
  std::istringstream input("\xef\xbb\xbfid,name,note,id\r\n1,Alice,\"hello,\nworld\",101\r\n2,Bob,\"say \"\"hi\"\"\",102\n");
  CsvReader reader(input);
  Row row;
  assert(reader.next(row) == ReadResult::row);
  assert(row.size() == 4 && row[0].text == "id");
  assert(reader.next(row) == ReadResult::row);
  assert(row[2].text == "hello,\nworld" && row[2].quoted);
  assert(reader.next(row) == ReadResult::row && row[2].text == "say \"hi\"");
  assert(reader.next(row) == ReadResult::end);
  assert(reader.next(row) == ReadResult::end);

  std::istringstream empty_fields(",\"\",NULL,\"NULL\"\n");
  CsvReader null_reader(empty_fields);
  assert(null_reader.next(row) == ReadResult::row && row.size() == 4);
  assert(row[0].is_null() && !row[1].is_null() && row[2].is_null() && !row[3].is_null());

  for (const std::string bad : {"\"unfinished", "a,\"b\"junk\n", "ab\"cd\n"}) {
    std::istringstream malformed(bad);
    CsvReader parser(malformed);
    assert(parser.next(row) == ReadResult::invalid);
  }
  std::istringstream oversized("abcdefghij\n");
  CsvReader small(oversized, 8);
  assert(small.next(row) == ReadResult::too_large);
  std::istringstream too_many_columns(std::string(5000, ','));
  CsvReader wide(too_many_columns);
  assert(wide.next(row) == ReadResult::too_large);
  std::istringstream trailing("a,b,");
  CsvReader trailing_reader(trailing);
  assert(trailing_reader.next(row) == ReadResult::row && row.size() == 3 && row[2].is_null());

  std::istringstream typed("id,score,active,day,created,id,missing\n1,98.5,true,2026-08-01,2026-08-01T10:11:12,2,\n2,87,false,,,3,\n");
  Schema schema;
  assert(infer_csv(typed, true, schema) == ReadResult::end);
  assert(schema.size() == 7 && schema[0].type == Type::integer && schema[1].type == Type::real);
  assert(schema[2].type == Type::boolean && schema[3].type == Type::date && schema[3].nullable);
  assert(schema[4].type == Type::datetime && schema[5].name == "id__2" && schema[6].type == Type::null);
  assert(classify("9223372036854775808") == Type::real);
  assert(classify("2026-02-29") == Type::text && classify("2024-02-29") == Type::date);
  assert(classify("2026-01-01T25:00:00") == Type::text);
  std::istringstream ragged("a,b\n1\n");
  assert(infer_csv(ragged, true, schema) == ReadResult::invalid);
  std::istringstream collisions("id,id,id__2\n1,2,3\n");
  assert(infer_csv(collisions, true, schema) == ReadResult::end);
  assert(schema[0].name != schema[1].name && schema[1].name != schema[2].name);
  std::istringstream no_header("1,x\n2,y\n");
  assert(infer_csv(no_header, false, schema) == ReadResult::end);
  assert(schema[0].name == "column0" && schema[0].type == Type::integer);
  std::mt19937 random(20261101);
  const std::string alphabet = "abc123,\"\n\r\t 数据库";
  for (int sample = 0; sample < 10000; ++sample) {
    std::vector<std::string> expected;
    std::string encoded;
    const int columns = 1 + random() % 9;
    for (int c = 0; c < columns; ++c) {
      std::string text;
      const int length = random() % 80;
      for (int k = 0; k < length; ++k) text.push_back(alphabet[random() % alphabet.size()]);
      expected.push_back(text);
      if (c) encoded += ',';
      encoded += '"';
      for (char ch : text) { encoded += ch; if (ch == '"') encoded += ch; }
      encoded += '"';
    }
    encoded += sample % 2 ? "\r\n" : "\n";
    std::istringstream source(encoded);
    CsvReader roundtrip(source);
    assert(roundtrip.next(row) == ReadResult::row && row.size() == expected.size());
    for (size_t i = 0; i < row.size(); ++i) assert(row[i].text == expected[i] && row[i].quoted);
    assert(roundtrip.next(row) == ReadResult::end);
  }
  std::cout << "CSV record, quoting, null, schema, overflow, date, width and bound checks passed\n";
}
