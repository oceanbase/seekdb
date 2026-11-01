#include "sql/engine/file_sql/csv_reader.h"
#include <array>
#include <cassert>
#include <iostream>
#include <streambuf>
#include <string>

class RepeatedCsv : public std::streambuf {
public:
  explicit RepeatedCsv(size_t bytes) : remaining_(bytes) {
    const std::string line = "1," + std::string(61, 'x') + "\n";
    for (size_t i = 0; i < buffer_.size(); ++i) buffer_[i] = line[i % line.size()];
    setg(buffer_.data(), buffer_.data(), buffer_.data());
  }
protected:
  int_type underflow() override {
    if (remaining_ == 0) return traits_type::eof();
    const size_t amount = std::min(remaining_, buffer_.size());
    remaining_ -= amount;
    setg(buffer_.data(), buffer_.data(), buffer_.data() + amount);
    return traits_type::to_int_type(*gptr());
  }
private:
  size_t remaining_;
  std::array<char, 65536> buffer_;
};

int main(int argc, char **argv) {
  assert(argc == 2);
  const size_t bytes = std::stoull(argv[1]) * 1024 * 1024;
  RepeatedCsv source(bytes);
  std::istream input(&source);
  oceanbase::sql::file_sql::Schema schema;
  const auto result = oceanbase::sql::file_sql::infer_csv(input, false, schema);
  assert(result == oceanbase::sql::file_sql::ReadResult::end);
  assert(schema.size() == 2 && schema[0].type == oceanbase::sql::file_sql::Type::integer);
  assert(schema[1].type == oceanbase::sql::file_sql::Type::text && schema[1].max_bytes == 61);
  std::cout << "Streamed " << bytes << " bytes without retaining input rows\n";
}
