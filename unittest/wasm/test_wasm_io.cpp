// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "share/wasm_aio.h"
#include "lib/file/wasm_file.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <thread>
#include <sys/stat.h>

#ifdef __EMSCRIPTEN__
// Only this test executable wraps libc. The production adapter still calls
// pwrite directly. Injection is enabled after all concurrent I/O threads join.
enum class WriteFault { none, interrupted_then_short, zero, partial_then_full };
static WriteFault write_fault = WriteFault::none;
static unsigned fault_calls = 0;
extern "C" ssize_t __real_pwrite(int, const void *, size_t, off_t);
extern "C" ssize_t __wrap_pwrite(int fd, const void *buf, size_t count, off_t offset)
{
  if (write_fault != WriteFault::none) {
    ++fault_calls;
    if (write_fault == WriteFault::zero) { errno = ERANGE; return 0; }
    if (write_fault == WriteFault::interrupted_then_short && fault_calls == 1) {
      errno = EINTR;
      return -1;
    }
    if (write_fault == WriteFault::partial_then_full && fault_calls > 1) {
      errno = ENOSPC;
      return -1;
    }
    if (count > 17) count = 17;
  }
  return __real_pwrite(fd, buf, count, offset);
}

static void test_write_failures(int fd)
{
  char bytes[128];
  memset(bytes, 0x5a, sizeof(bytes));
  assert(pwrite(fd, bytes, sizeof(bytes), 0) == sizeof(bytes));
  write_fault = WriteFault::interrupted_then_short;
  fault_calls = 0;
  assert(oceanbase::common::wasm::zero_file_range(fd, 0, sizeof(bytes)) == 0);
  assert(fault_calls > 2);
  write_fault = WriteFault::none;
  assert(pread(fd, bytes, sizeof(bytes), 0) == sizeof(bytes));
  for (char byte : bytes) assert(byte == 0);

  write_fault = WriteFault::zero;
  fault_calls = 0;
  assert(oceanbase::common::wasm::zero_file_range(fd, 0, 128) == -1 && errno == EIO);
  assert(fault_calls == 1);
  write_fault = WriteFault::none;
  memset(bytes, 0x5a, sizeof(bytes));
  assert(pwrite(fd, bytes, sizeof(bytes), 0) == sizeof(bytes));
  write_fault = WriteFault::partial_then_full;
  fault_calls = 0;
  assert(oceanbase::common::wasm::zero_file_range(fd, 0, 128) == -1 && errno == ENOSPC);
  assert(fault_calls == 2);
  write_fault = WriteFault::none;
  assert(pread(fd, bytes, sizeof(bytes), 0) == sizeof(bytes));
  for (unsigned i = 0; i < sizeof(bytes); ++i) assert(bytes[i] == (i < 17 ? 0 : 0x5a));
}
#endif

static void test_completion_contract(int fd)
{
  io_context_t ctx = nullptr;
  assert(io_setup(0, &ctx) == -EINVAL && ctx == nullptr);
  assert(io_setup(1, &ctx) == 0);
  assert(io_setup(1, &ctx) == -EINVAL);
  char bytes[] = "abc";
  iocb cb{};
  cb.data = bytes;
  io_prep_pwrite(&cb, fd, bytes, 3, 0);
  iocb *requests[] = {&cb, &cb};
  assert(io_submit(ctx, 2, requests) == 1);
  assert(io_submit(ctx, 1, requests) == -EAGAIN);
  io_event event{};
  assert(io_cancel(ctx, &cb, &event) == -EAGAIN);
  timespec poll{};
  assert(io_getevents(ctx, 0, 1, &event, &poll) == 1);
  assert(event.data == bytes && event.obj == &cb && event.res == 3 && event.res2 == 0);
  assert(io_getevents(ctx, 0, 1, &event, &poll) == 0);

  // EOF and short reads preserve the byte count, rather than claiming a full I/O.
  char out[8]{};
  io_prep_pread(&cb, fd, out, sizeof(out), 1);
  assert(io_submit(ctx, 1, requests) == 1);
  assert(io_getevents(ctx, 1, 1, &event, nullptr) == 1 && event.res == 2);
  assert(memcmp(out, "bc", 2) == 0);
  io_prep_pread(&cb, fd, out, sizeof(out), 3);
  assert(io_submit(ctx, 1, requests) == 1);
  assert(io_getevents(ctx, 1, 1, &event, nullptr) == 1 && event.res == 0);
  io_prep_pread(&cb, -1, out, sizeof(out), 0);
  assert(io_submit(ctx, 1, requests) == 1);
  assert(io_getevents(ctx, 1, 1, &event, nullptr) == 1 && event.res == -EBADF);

  cb.aio_lio_opcode = 7;
  assert(io_submit(ctx, 1, requests) == -EINVAL);
  assert(io_getevents(ctx, 0, 1, &event, &poll) == 0);
  timespec invalid{0, 1000000000};
  assert(io_getevents(ctx, 0, 1, &event, &invalid) == -EINVAL);
  assert(io_getevents(ctx, 2, 1, &event, &poll) == -EINVAL);
  timespec timeout{0, 2000000};
  const auto start = std::chrono::steady_clock::now();
  assert(io_getevents(ctx, 1, 1, &event, &timeout) == 0);
  assert(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(1));
  assert(io_destroy(ctx) == 0);
}

static void test_concurrent_file_io(int fd)
{
  constexpr unsigned count = 512;
  std::array<uint64_t, count> data{};
  std::array<iocb, count> requests{};
  for (unsigned i = 0; i < count; ++i) {
    data[i] = UINT64_C(0xabcd000000000000) + i;
    requests[i].data = &data[i];
    io_prep_pwrite(&requests[i], fd, &data[i], sizeof(data[i]), i * sizeof(data[i]));
  }
  io_context_t ctx = nullptr;
  assert(io_setup(8, &ctx) == 0);
  std::thread consumer([&] {
    std::array<bool, count> seen{};
    unsigned reaped = 0;
    while (reaped < count) {
      io_event events[8];
      timespec timeout{5, 0};
      const int n = io_getevents(ctx, 1, 8, events, &timeout);
      assert(n > 0);
      for (int i = 0; i < n; ++i) {
        const auto index = static_cast<uint64_t *>(events[i].data) - data.data();
        assert(index >= 0 && index < count && !seen[index]);
        assert(events[i].res == sizeof(uint64_t) && events[i].obj == &requests[index]);
        seen[index] = true;
        ++reaped;
      }
    }
  });
  auto produce = [&](unsigned parity) {
    for (unsigned i = parity; i < count; i += 2) {
      iocb *cb = &requests[i];
      int rc;
      while ((rc = io_submit(ctx, 1, &cb)) == -EAGAIN) std::this_thread::yield();
      assert(rc == 1);
    }
  };
  std::thread first(produce, 0), second(produce, 1);
  first.join();
  second.join();
  consumer.join();
  assert(io_destroy(ctx) == 0);
  std::array<uint64_t, count> actual{};
  assert(pread(fd, actual.data(), sizeof(actual), 0) == sizeof(actual));
  assert(actual == data);
}

static void test_log_zero_range(int fd)
{
  constexpr size_t size = 150000;
  std::array<unsigned char, size> bytes;
  bytes.fill(0xa5);
  assert(ftruncate(fd, 0) == 0);
  assert(pwrite(fd, bytes.data(), bytes.size(), 0) == bytes.size());
  // More than two buffer iterations, with unchanged leading/trailing bytes.
  assert(oceanbase::common::wasm::zero_file_range(fd, 17, 140000) == 0);
  assert(pread(fd, bytes.data(), bytes.size(), 0) == bytes.size());
  for (size_t i = 0; i < size; ++i) {
    assert(bytes[i] == (i >= 17 && i < 140017 ? 0 : 0xa5));
  }
  // Extending past EOF must zero-fill the requested range.
  assert(oceanbase::common::wasm::zero_file_range(fd, size - 1, 4097) == 0);
  struct stat st{};
  assert(fstat(fd, &st) == 0 && st.st_size == size + 4096);
  assert(oceanbase::common::wasm::zero_file_range(-1, 0, 1) == -1 && errno == EBADF);
  assert(oceanbase::common::wasm::zero_file_range(fd, INT64_MAX, 1) == -1 && errno == EINVAL);
}

static void test_file_growth(int fd)
{
  using oceanbase::common::wasm::extend_file_with_zeros;
  assert(ftruncate(fd, 0) == 0);
  assert(extend_file_with_zeros(fd, 150001) == 0);
  std::array<unsigned char, 150001> bytes;
  bytes.fill(0xa5);
  assert(pread(fd, bytes.data(), bytes.size(), 0) == bytes.size());
  for (auto byte : bytes) assert(byte == 0);
  const char marker[] = "existing data";
  assert(pwrite(fd, marker, sizeof(marker), 19) == sizeof(marker));
  assert(extend_file_with_zeros(fd, 160003) == 0);
  // A smaller request must not truncate a previously allocated file.
  assert(extend_file_with_zeros(fd, 100) == 0);
  struct stat st{};
  assert(fstat(fd, &st) == 0 && st.st_size == 160003);
  char actual[sizeof(marker)]{};
  assert(pread(fd, actual, sizeof(actual), 19) == sizeof(actual));
  assert(memcmp(actual, marker, sizeof(marker)) == 0);
  assert(extend_file_with_zeros(-1, 100) == -1 && errno == EBADF);
  assert(extend_file_with_zeros(fd, 0) == -1 && errno == EINVAL);
#ifdef __EMSCRIPTEN__
  write_fault = WriteFault::partial_then_full;
  fault_calls = 0;
  assert(extend_file_with_zeros(fd, 170003) == -1 && errno == ENOSPC);
  write_fault = WriteFault::none;
  assert(fstat(fd, &st) == 0 && st.st_size == 160020);
  assert(extend_file_with_zeros(fd, 170003) == 0);
  assert(fstat(fd, &st) == 0 && st.st_size == 170003);
  assert(pread(fd, actual, sizeof(actual), 19) == sizeof(actual));
  assert(memcmp(actual, marker, sizeof(marker)) == 0);
  std::array<unsigned char, 10000> tail;
  tail.fill(0xa5);
  assert(pread(fd, tail.data(), tail.size(), 160003) == tail.size());
  for (auto byte : tail) assert(byte == 0);
#endif
}

int main()
{
  char path[] = "/tmp/seekdb-wasm-io-XXXXXX";
  const int fd = mkstemp(path);
  assert(fd >= 0);
  test_completion_contract(fd);
  test_concurrent_file_io(fd);
  test_log_zero_range(fd);
  test_file_growth(fd);
#ifdef __EMSCRIPTEN__
  test_write_failures(fd);
#endif
  assert(close(fd) == 0 && unlink(path) == 0);
  puts("File I/O: bounded completions, concurrency, errors and log zeroing passed");
}
