// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#include "logservice/ob_server_log_block_mgr.h"
#include "lib/resource/achunk_mgr.h"
#include <array>
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace oceanbase;
using namespace oceanbase::common;

// Only this executable injects quota failure into the real write syscall.
static bool fail_write = false;
static unsigned write_calls = 0;
extern "C" ssize_t __real_pwrite(int, const void *, size_t, off_t);
extern "C" ssize_t __wrap_pwrite(int fd, const void *buffer, size_t count, off_t offset)
{
  if (fail_write) {
    if (++write_calls > 1) { errno = ENOSPC; return -1; }
    count = count > 17 ? 17 : count;
  }
  return __real_pwrite(fd, buffer, count, offset);
}

static int disk_config(int64_t &size, int64_t &percentage, int64_t &total)
{
  size = total = 256 * 1024 * 1024;
  percentage = 0;
  return OB_SUCCESS;
}

int main()
{
  // Absolute paths put a pointer before the 64-bit vararg, which exposes the
  // wasm32 alignment hole that a relative-name-only test would miss.
  char name[128];
  constexpr palf::block_id_t high_id = UINT64_C(4294967419);
  assert(palf::block_id_to_string(high_id, name, sizeof(name)) == OB_SUCCESS);
  assert(std::strcmp(name, "4294967419") == 0);
  assert(palf::block_id_to_tmp_string(high_id, name, sizeof(name)) == OB_SUCCESS);
  assert(std::strcmp(name, "4294967419.tmp") == 0);
  assert(palf::construct_absolute_block_path("/meta", high_id, sizeof(name), name) == OB_SUCCESS);
  assert(std::strcmp(name, "/meta/4294967419") == 0);
  assert(palf::construct_absolute_tmp_block_path("/meta", high_id, sizeof(name), name) == OB_SUCCESS);
  assert(std::strcmp(name, "/meta/4294967419.tmp") == 0);
  assert(palf::construct_absolute_block_path("/meta", 0, sizeof(name), name) == OB_SUCCESS);
  assert(std::strcmp(name, "/meta/0") == 0);
  lib::AChunkMgr::instance().set_limit(64 * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(64 * 1024 * 1024);
  char path[] = "/tmp/seekdb-log-block-XXXXXX";
  assert(mkdtemp(path) != nullptr);
  const int dir = open(path, O_RDONLY | O_DIRECTORY);
  assert(dir >= 0);
  logservice::ObServerLogBlockMgr manager;
  assert(manager.init(path, disk_config) == OB_SUCCESS);
  const int64_t size = palf::PALF_PHY_BLOCK_SIZE;
  fail_write = true;
  const int failed = manager.create_block_at(dir, "1", size);
  fail_write = false;
  assert(failed != OB_SUCCESS && write_calls == 2);
  struct stat st{};
  assert(fstatat(dir, "1", &st, 0) == -1 && errno == ENOENT);
  int64_t usage = -1;
  assert(manager.get_disk_usage(usage) == OB_SUCCESS && usage == 0);

  // Retry through the production entry point after the host frees space.
  assert(manager.create_block_at(dir, "1", size) == OB_SUCCESS);
  assert(fstatat(dir, "1", &st, 0) == 0 && st.st_size == size);
  assert(manager.get_disk_usage(usage) == OB_SUCCESS && usage == size);
  int fd = openat(dir, "1", O_RDWR);
  assert(fd >= 0);
  std::array<unsigned char, 4096> bytes;
  for (int64_t offset = 0; offset < size; offset += bytes.size()) {
    const size_t count = std::min<int64_t>(bytes.size(), size - offset);
    bytes.fill(0xa5);
    assert(pread(fd, bytes.data(), count, offset) == count);
    for (size_t i = 0; i < count; ++i) assert(bytes[i] == 0);
  }
  const char marker[] = "preserve existing block";
  assert(pwrite(fd, marker, sizeof(marker), 0) == sizeof(marker));
  assert(manager.create_block_at(dir, "1", size) != OB_SUCCESS);
  assert(pread(fd, bytes.data(), sizeof(marker), 0) == sizeof(marker));
  assert(memcmp(bytes.data(), marker, sizeof(marker)) == 0);
  assert(manager.get_disk_usage(usage) == OB_SUCCESS && usage == size);
  assert(close(fd) == 0);
  assert(unlinkat(dir, "1", 0) == 0);
  manager.destroy();
  assert(close(dir) == 0 && rmdir(path) == 0);
  puts("PASS: production log block quota failure, cleanup, retry and exclusive creation");
}
