// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Links the production adapter and server Rust archive, with actual allocator
// and array interfaces. Test implementations provide deterministic fault injection.
#include "query/vector/embedding_response_parser.h"
#include "lib/alloc/ob_iallocator.h"
#include "lib/container/ob_iarray.h"
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace oceanbase::common;
using oceanbase::share::EmbeddingResponseParser;

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << "failed at line " << __LINE__ << ": " << #condition << '\n'; \
  std::abort(); } } while (false)

class TestAllocator final : public ObIAllocator
{
public:
  int calls = 0;
  int fail_at = -1;
  int frees = 0;
  std::vector<void *> allocations;
  ~TestAllocator() override { for (void *ptr : allocations) std::free(ptr); }
  void *alloc(int64_t bytes) override
  {
    if (++calls == fail_at || bytes <= 0) return nullptr;
    void *ptr = std::malloc(bytes);
    CHECK(ptr != nullptr);
    allocations.push_back(ptr);
    return ptr;
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *ptr) override
  {
    for (void *&owned : allocations) {
      if (owned == ptr) {
        std::free(ptr);
        owned = nullptr;
        ++frees;
        return;
      }
    }
    CHECK(false);
  }
};

class TestOutput final : public ObIArray<float *>
{
public:
  int fail_at = -1;
  int calls = 0;
  float *slots[16] = {};
  TestOutput() { data_ = slots; }
  int push_back(float *const &ptr) override
  {
    if (++calls == fail_at) return OB_SIZE_OVERFLOW;
    CHECK(count_ < 16);
    slots[count_++] = ptr;
    return OB_SUCCESS;
  }
  void extra_access_check() const override {}
  void pop_back() override { --count_; }
  int pop_back(float *&ptr) override { ptr = slots[--count_]; return OB_SUCCESS; }
  int at(int64_t idx, float *&ptr) const override { ptr = slots[idx]; return OB_SUCCESS; }
  int remove(int64_t) override { return OB_NOT_SUPPORTED; }
  void reset() override { count_ = 0; }
  void reuse() override { reset(); }
  void destroy() override { reset(); }
  int reserve(int64_t) override { return OB_NOT_SUPPORTED; }
  int assign(const ObIArray<float *> &) override { return OB_NOT_SUPPORTED; }
  int prepare_allocate(int64_t) override { return OB_NOT_SUPPORTED; }
  float **alloc_place_holder() override { return nullptr; }
  int64_t to_string(char *, int64_t) const override { return 0; }
};

int parse(const std::string &input, TestAllocator &allocator, TestOutput &output,
          int64_t dimension = 1, bool base64 = false)
{
  return EmbeddingResponseParser::parse(input.data(), input.size(), dimension, base64,
                                        allocator, output);
}

void tests()
{
  const std::string two = R"({"data":[{"embedding":[1]},{"embedding":[2]}]})";
  {
    TestAllocator allocator;
    TestOutput output;
    float existing = 42;
    CHECK(output.push_back(&existing) == 0);
    {
      std::string input = two;
      CHECK(parse(input, allocator, output) == 0);
      input.assign(input.size(), 'x');
    }
    CHECK(output.count() == 3 && output.slots[0] == &existing);
    CHECK(output.slots[1][0] == 1 && output.slots[2][0] == 2);
    CHECK(allocator.calls == 2 && allocator.frees == 0);
  }
  {
    TestAllocator allocator;
    TestOutput output;
    allocator.fail_at = 2;
    CHECK(parse(two, allocator, output) == OB_ALLOCATE_MEMORY_FAILED);
    CHECK(output.count() == 1 && output.slots[0][0] == 1);
    CHECK(allocator.calls == 2 && allocator.frees == 0);
  }
  {
    TestAllocator allocator;
    TestOutput output;
    output.fail_at = 2;
    CHECK(parse(two, allocator, output) == OB_SIZE_OVERFLOW);
    CHECK(output.count() == 1 && output.slots[0][0] == 1);
    CHECK(allocator.calls == 2 && allocator.frees == 1);
  }
  {
    TestAllocator allocator;
    TestOutput output;
    allocator.fail_at = 1;
    CHECK(parse(R"({"data":[{"embedding":[1]},{}]})", allocator, output) == OB_ALLOCATE_MEMORY_FAILED);
    CHECK(output.count() == 0 && allocator.calls == 1);
  }
  {
    TestAllocator allocator;
    TestOutput output;
    CHECK(parse(R"({"data":[{"embedding":[1]},{}]})", allocator, output) == OB_SEARCH_NOT_FOUND);
    CHECK(output.count() == 1 && output.slots[0][0] == 1);
    CHECK(parse(R"({"data":[{"embedding":[1]}],"bad":1e999})", allocator, output) == OB_ERR_INVALID_JSON_TEXT);
    CHECK(output.count() == 1 && allocator.calls == 1);
    CHECK(EmbeddingResponseParser::parse(nullptr, 10, 1, false, allocator, output) == OB_INVALID_ARGUMENT);
  }
  {
    TestAllocator allocator;
    TestOutput output;
    CHECK(parse(R"({"data":[{"embedding":"AAAAAA=="}]})", allocator, output, 1, true) == 0);
    CHECK(output.count() == 1 && output.slots[0][0] == 0);
  }
  std::cout << "PASS: production C++ -> Rust adapter, ownership and allocation/append failures\n";
}

// Same wire format as embedding-response/tests/compare_cpp.py. Running that
// corpus here verifies the real adapter, not a second C++ implementation of it.
void probe()
{
  std::string line;
  while (std::getline(std::cin, line)) {
    std::istringstream fields(line);
    int64_t dimension;
    char encoding;
    std::string hex;
    fields >> dimension >> encoding >> hex;
    std::string input;
    for (size_t i = 0; i < hex.size(); i += 2) {
      input.push_back(static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    TestAllocator allocator;
    TestOutput output;
    float existing = 42;
    CHECK(output.push_back(&existing) == 0);
    const int code = parse(input, allocator, output, dimension, encoding == 'b');
    std::cout << std::dec << code << ' ' << output.count();
    for (int64_t i = 0; i < output.count(); ++i) {
      const int64_t length = i == 0 ? 1 : dimension;
      std::cout << ' ' << std::dec << length;
      for (int64_t j = 0; j < length; ++j) {
        uint32_t bits;
        std::memcpy(&bits, output.slots[i] + j, sizeof(bits));
        std::cout << ' ' << std::hex << std::setw(8) << std::setfill('0') << bits;
      }
    }
    std::cout << '\n';
  }
}

int main(int argc, char **argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) probe();
  else tests();
}
