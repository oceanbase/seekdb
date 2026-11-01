#pragma once

#include <cstddef>
#include <functional>
#include <istream>
#include <string>
#include <vector>

namespace oceanbase::sql::file_sql {

enum class ReadResult { row, end, invalid, io_error, too_large, cancelled };
enum class Type { null, integer, real, boolean, date, datetime, text };

struct Field {
  std::string text;
  bool quoted = false;
  bool is_null() const { return !quoted && (text.empty() || text == "NULL"); }
};
using Row = std::vector<Field>;
struct Column {
  std::string name;
  Type type = Type::null;
  bool nullable = false;
  size_t max_bytes = 0;
  std::string source_name = {};
};
using Schema = std::vector<Column>;

// Storage is bounded by one record, independent of the number of records.
// An oversized record fails explicitly rather than exhausting the process.
class CsvReader {
public:
  explicit CsvReader(std::istream &input, size_t max_record_bytes = 64 * 1024 * 1024);
  ReadResult next(Row &row);
private:
  int get();
  int peek();
  std::istream &input_;
  size_t max_record_bytes_;
  std::string prefix_;
  size_t prefix_pos_ = 0;
  bool ended_ = false;
};

Type classify(const std::string &value);
Type widen(Type before, Type after);
ReadResult infer_csv(std::istream &input, bool header, Schema &schema,
                     const std::function<bool()> &keep_running = {});
const char *type_name(Type type);

} // namespace oceanbase::sql::file_sql
