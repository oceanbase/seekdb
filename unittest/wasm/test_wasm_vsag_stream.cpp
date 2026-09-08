// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "storage/stream_reader.h"
#include "vsag_test_allocator.h"
#include <array>
#include <cassert>
#include <cstring>
#include <limits>

vsag::Allocator *make_vsag_default_allocator();

namespace {
class TestReader : public StreamReader {
public:
  explicit TestReader(uint64_t count, const void *payload = nullptr, size_t size = 0)
      : header_(count), payload_(static_cast<const char *>(payload)), size_(size) {}
  void Read(char *data, uint64_t size) override {
    ++reads;
    if (cursor_ == 0) {
      assert(size == sizeof(header_));
      std::memcpy(data, &header_, sizeof(header_));
    } else {
      if (!payload_ || size > size_) { throw std::out_of_range("short test payload"); }
      std::memcpy(data, payload_, static_cast<size_t>(size));
    }
    cursor_ += size;
  }
  void Seek(uint64_t cursor) override { cursor_ = cursor; }
  uint64_t GetCursor() const override { return cursor_; }
  unsigned reads = 0;
private:
  uint64_t header_, cursor_ = 0;
  const char *payload_;
  size_t size_;
};

template <typename Vector>
void vectors(Vector &value)
{
  const uint64_t bad_counts[] = {UINT64_MAX, static_cast<uint64_t>(value.max_size()) + 1};
  for (auto count : bad_counts) {
    value.assign({11, 22});
    TestReader reader(count);
    bool caught = false;
    try { StreamReader::ReadVector(reader, value); }
    catch (const std::length_error &) { caught = true; }
    assert(caught && reader.reads == 1 && value.size() == 2 && value[0] == 11 && value[1] == 22);
  }
  const std::array<uint64_t, 3> payload{0, UINT64_MAX, UINT64_C(1) << 40};
  TestReader reader(payload.size(), payload.data(), sizeof(payload));
  StreamReader::ReadVector(reader, value);
  assert(reader.reads == 2 && value.size() == payload.size());
  for (size_t i = 0; i < payload.size(); ++i) { assert(value[i] == payload[i]); }
  TestReader empty(0);
  StreamReader::ReadVector(empty, value);
  assert(value.empty() && empty.reads == 1);
}
} // namespace

int main()
{
  const uint64_t bad_lengths[] = {UINT64_MAX, static_cast<uint64_t>(std::string{}.max_size()) + 1};
  for (auto length : bad_lengths) {
    TestReader reader(length);
    bool caught = false;
    try { (void)StreamReader::ReadString(reader); }
    catch (const std::length_error &) { caught = true; }
    assert(caught && reader.reads == 1);
  }
  const std::string text("向量\0index", 12);
  TestReader string_reader(text.size(), text.data(), text.size());
  assert(StreamReader::ReadString(string_reader) == text && string_reader.reads == 2);
  TestReader empty(0);
  assert(StreamReader::ReadString(empty).empty() && empty.reads == 1);
  TestReader truncated(4, "ab", 2);
  bool caught = false;
  try { (void)StreamReader::ReadString(truncated); }
  catch (const std::out_of_range &) { caught = true; }
  assert(caught);
  std::vector<uint64_t> standard;
  vectors(standard);
  VsagTestAllocator allocator;
  vsag::Vector<uint64_t> custom(&allocator);
  vectors(custom);
  std::unique_ptr<vsag::Allocator> actual_allocator(make_vsag_default_allocator());
  auto *block = static_cast<unsigned char *>(actual_allocator->Allocate(32));
  assert(block != nullptr);
  std::memset(block, 0x5a, 32);
#if SIZE_MAX < UINT64_MAX
  const uint64_t too_large = (UINT64_C(1) << 32) + 32;
  assert(actual_allocator->Allocate(too_large) == nullptr);
  assert(actual_allocator->Reallocate(block, too_large) == nullptr);
  for (size_t i = 0; i < 32; ++i) { assert(block[i] == 0x5a); }
#endif
  block = static_cast<unsigned char *>(actual_allocator->Reallocate(block, 64));
  assert(block != nullptr);
  for (size_t i = 0; i < 32; ++i) { assert(block[i] == 0x5a); }
  actual_allocator->Deallocate(block);
  std::puts("VSAG stream target-size bounds, payload and allocation checks passed");
}
