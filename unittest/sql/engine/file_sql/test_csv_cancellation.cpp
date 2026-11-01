#include "sql/engine/file_sql/csv_reader.h"
#include <cassert>
#include <sstream>

using namespace oceanbase::sql::file_sql;

int main()
{
  std::string content = "id,name\n";
  for (int i = 0; i < 10000; ++i) content += "1,test\n";
  std::istringstream input(content);
  Schema schema;
  int polls = 0;
  const auto result = infer_csv(input, true, schema, [&] { return ++polls < 3; });
  assert(result == ReadResult::cancelled);
  assert(polls == 3);
  assert(input.tellg() > 0 && input.tellg() < static_cast<std::streampos>(content.size()));

  std::string headers = "same";
  for (int i = 1; i < 1024; ++i) headers += ",same";
  std::istringstream header_input(headers + "\n");
  polls = 0;
  assert(infer_csv(header_input, true, schema, [&] { return ++polls < 3; }) == ReadResult::cancelled);
  assert(polls == 3);
}
