// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Links the production adapter and server Rust archive, with actual allocator
// and array interfaces. Test implementations provide deterministic fault injection.
#include "query/vector/embedding_response_parser.h"
#include "query/vector/embedding_request_builder.h"
#include "query/vector/ob_vector_embedding_handler.h"
#include <chrono>
#include <thread>
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

void request_tests()
{
  using oceanbase::share::EmbeddingRequestBuilder;
  ObArray<ObString> inputs;
  CHECK(inputs.push_back(ObString::make_string("skip")) == 0);
  CHECK(inputs.push_back(ObString::make_string("a\"")) == 0);
  TestAllocator allocator;
  char *body = nullptr;
  int64_t length = 0;
  CHECK(EmbeddingRequestBuilder::build(inputs, 1, 2, ObString::make_string("m"),
      2, false, allocator, body, length) == 0);
  CHECK(std::string(body, length) == R"({"input":["a\""],"model":"m","encoding_format":"float","dimensions":2})");
  CHECK(allocator.calls == 1);
  allocator.fail_at = 2;
  CHECK(EmbeddingRequestBuilder::build(inputs, 0, 2, ObString(), 0,
      true, allocator, body, length) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(body == nullptr && length == 0);
  CHECK(EmbeddingRequestBuilder::build(inputs, -1, 2, ObString(), 0,
      true, allocator, body, length) == OB_INVALID_ARGUMENT);
  CHECK(body == nullptr && length == 0 && allocator.calls == 2);
  CHECK(EmbeddingRequestBuilder::build(inputs, 0, 3, ObString(), 0,
      true, allocator, body, length) == OB_INVALID_ARGUMENT);
  CHECK(EmbeddingRequestBuilder::build(inputs, 2, 1, ObString(), 0,
      true, allocator, body, length) == OB_INVALID_ARGUMENT);
  // Larger callers exercise the descriptor allocation path beyond the stack buffer.
  while (inputs.count() < 20) CHECK(inputs.push_back(ObString::make_string("x")) == 0);
  for (int fail_at : {-1, 1, 2}) {
    TestAllocator large_allocator;
    large_allocator.fail_at = fail_at;
    const int ret = EmbeddingRequestBuilder::build(inputs, 0, 20, ObString(), 0,
        true, large_allocator, body, length);
    CHECK(ret == (fail_at < 0 ? OB_SUCCESS : OB_ALLOCATE_MEMORY_FAILED));
    CHECK(large_allocator.frees == (fail_at == 1 ? 0 : 1));
    if (ret == 0) CHECK(body != nullptr && length > 0);
    else CHECK(body == nullptr && length == 0);
  }
  std::cout << "PASS: request adapter allocation, batch selection and error handling\n";
}

void http_tests(const std::string &base_url)
{
  using namespace oceanbase::share;
  CHECK(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
  const char *names[] = {"float", "base64", "silicon", "zero", "negative", "empty"};
  for (const char *name : names) {
    const std::string scenario(name);
    const int count = scenario == "empty" ? 0 :
                      (scenario == "zero" || scenario == "negative" ? 1 : 23);
    const int dimension = scenario == "zero" ? 0 : (scenario == "negative" ? -1 : 2);
    std::vector<std::string> texts;
    for (int i = 0; i < count; ++i) {
      if (i == 0) texts.emplace_back("");
      else if (i == 1) {
        std::string controls;
        for (int c = 0; c < 32; ++c) controls.push_back(static_cast<char>(c));
        texts.push_back(controls);
      } else if (i == 2) texts.emplace_back(4096, '\0');
      else if (i == 3) texts.emplace_back("中文🙂\"\\/\n");
      else texts.push_back("chunk-" + std::to_string(i));
    }
    ObArray<ObString> inputs;
    for (const auto &text : texts) {
      CHECK(inputs.push_back(ObString(static_cast<int32_t>(text.size()), text.data())) == 0);
    }
    const std::string model = std::string("model\"\\\n") + std::string(4096, 'x');
    const std::string url = base_url + "/" + name;
    const char *provider = scenario == "base64" ? "OPENAI" :
                           (scenario == "silicon" ? "SILICONFLOW" : "OTHER");
    ObEmbeddingTask task;
    CHECK(task.init(ObString::make_string(url.c_str()),
        ObString(static_cast<int32_t>(model.size()), model.data()),
        ObString::make_string(provider), ObString::make_string("mock-key"),
        inputs, dimension, 5000000, 1) == 0);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (!task.is_completed()) {
      CHECK(std::chrono::steady_clock::now() < deadline);
      CHECK(task.do_work(static_cast<ObEmbeddingTaskHandler *>(nullptr)) == 0);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ObArray<float *> vectors;
    CHECK(task.get_async_result(vectors) == 0);
    CHECK(vectors.count() == (dimension > 0 ? count : 0));
    for (int64_t i = 0; i < vectors.count(); ++i) {
      CHECK(vectors.at(i)[0] == 1.25f && vectors.at(i)[1] == -2.5f);
    }
  }
  curl_global_cleanup();
  std::cout << "PASS: production embedding task HTTP and Rust request/response round trip\n";
}

int main(int argc, char **argv)
{
  if (argc == 2 && std::strcmp(argv[1], "--probe") == 0) probe();
  else if (argc == 3 && std::strcmp(argv[1], "--http") == 0) http_tests(argv[2]);
  else { tests(); request_tests(); }
}
