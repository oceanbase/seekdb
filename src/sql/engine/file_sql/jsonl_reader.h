#pragma once

#include "sql/engine/file_sql/csv_reader.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase::sql::file_sql {

struct NamedField {
  std::string name;
  Field value;
  Type type = Type::null;
};
using NamedRow = std::vector<NamedField>;

// Bound parser allocations as well as input bytes: dense nested arrays can
// otherwise allocate far more tree nodes than their encoded size suggests.
class JsonlArena : public common::ObIAllocator {
public:
  JsonlArena() : arena_("FileSqlJsonl") {}
  void *alloc(const int64_t size) override;
  void *alloc(const int64_t size, const common::ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}
  void reuse() { arena_.reuse(); requested_ = 0; exhausted_ = false; }
  bool exhausted() const { return exhausted_; }
private:
  common::ObArenaAllocator arena_;
  int64_t requested_ = 0;
  bool exhausted_ = false;
};

class JsonlReader {
public:
  explicit JsonlReader(std::istream &input) : input_(input) {}
  int next(NamedRow &row);
  int infer(Schema &schema, const std::function<bool()> &keep_running);
private:
  std::istream &input_;
  JsonlArena arena_;
  std::string line_;
  bool first_line_ = true;
  bool ended_ = false;
};

} // namespace oceanbase::sql::file_sql
