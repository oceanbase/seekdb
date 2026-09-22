/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cassert>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <string>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <emscripten/wasmfs.h>
#include "memory_backend.h"
#include "wasmfs.h"
#include "../../src/wasm/wasmfs_adapter.cpp"

namespace {

constexpr const char *JOURNAL = "/seekdb/.move";
int file_moves = 0;
int failures_remaining = 0;
int move_error = EIO;
bool fail_write = false;
bool fail_flush = false;
int fail_unlinks = 0;

class MoveDirectory final : public wasmfs::MemoryDirectory {
public:
  MoveDirectory(mode_t mode, wasmfs::backend_t backend) : MemoryDirectory(mode, backend) {}

private:
  int insertMove(const std::string &name, std::shared_ptr<wasmfs::File> file) override
  {
    if (file->is<wasmfs::Directory>()) return -EBUSY;
    ++file_moves;
    if (name == "second" && failures_remaining > 0) {
      --failures_remaining;
      return -move_error;
    }
    return MemoryDirectory::insertMove(name, file);
  }
};

class MoveBackend final : public wasmfs::Backend {
public:
  std::shared_ptr<wasmfs::DataFile> createFile(mode_t mode) override
  {
    return std::make_shared<wasmfs::MemoryDataFile>(mode, this);
  }

  std::shared_ptr<wasmfs::Directory> createDirectory(mode_t mode) override
  {
    return std::make_shared<MoveDirectory>(mode, this);
  }

  std::shared_ptr<wasmfs::Symlink> createSymlink(std::string target) override
  {
    return std::make_shared<wasmfs::MemorySymlink>(target, this);
  }
};

bool exists(const std::string &path)
{
  struct stat info;
  return stat(path.c_str(), &info) == 0;
}

void directory(const std::string &path)
{
  assert(mkdir(path.c_str(), 0755) == 0);
}

void create(const std::string &path, const std::string &value)
{
  const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  assert(fd >= 0);
  assert(write(fd, value.data(), value.size()) == static_cast<ssize_t>(value.size()));
  assert(close(fd) == 0);
}

std::string contents(const std::string &path)
{
  const int fd = open(path.c_str(), O_RDONLY);
  assert(fd >= 0);
  char buffer[1024];
  const ssize_t length = read(fd, buffer, sizeof(buffer));
  assert(length >= 0 && length < sizeof(buffer));
  assert(close(fd) == 0);
  return std::string(buffer, static_cast<size_t>(length));
}

std::string record(const std::string &from, const std::string &to)
{
  return from + "\n" + to + "\n";
}

void expect_error(const std::string &from, const std::string &to, int error)
{
  errno = 0;
  assert(rename(from.c_str(), to.c_str()) == -1);
  assert(errno == error);
}

void test_repeated_retry()
{
  const std::string from = "/seekdb/retry-source";
  const std::string to = "/seekdb/retry-target";
  directory(from);
  create(from + "/first", "first");
  create(from + "/second", "second");
  failures_remaining = 2;
  file_moves = 0;
  expect_error(from, to, EIO);
  assert(file_moves == 2 && contents(JOURNAL) == record(from, to));
  assert(contents(to + "/first") == "first" && contents(from + "/second") == "second");
  expect_error(from, to, EIO);
  assert(file_moves == 3 && contents(JOURNAL) == record(from, to));
  assert(rename(from.c_str(), to.c_str()) == 0);
  assert(file_moves == 4 && !exists(from) && !exists(JOURNAL));
  assert(contents(to + "/first") == "first" && contents(to + "/second") == "second");
  expect_error(from, to, ENOENT);
}

void test_nested_retry()
{
  const std::string from = "/seekdb/nested-source";
  const std::string to = "/seekdb/nested-target";
  directory(from);
  directory(from + "/outer");
  create(from + "/outer/first", "first");
  directory(from + "/outer/deep");
  create(from + "/outer/deep/second", "second");
  failures_remaining = 1;
  move_error = EBUSY;
  expect_error(from, to, EBUSY);
  assert(contents(JOURNAL) == record(from, to));
  assert(contents(to + "/outer/first") == "first");
  assert(rename(from.c_str(), to.c_str()) == 0);
  assert(!exists(from) && !exists(JOURNAL));
  assert(contents(to + "/outer/deep/second") == "second");
  move_error = EIO;
}

void test_unrelated_destinations()
{
  const std::string from = "/seekdb/unrelated-source";
  const std::string to = "/seekdb/unrelated-target";
  directory(from);
  directory(to);
  create(from + "/first", "source");
  create(to + "/second", "target");
  const int moves = file_moves;
  expect_error(from, to, ENOTEMPTY);
  assert(!exists(JOURNAL) && file_moves == moves);
  const std::string unrelated = record("/seekdb/another-source", "/seekdb/another-target");
  create(JOURNAL, unrelated);
  expect_error(from, to, ENOTEMPTY);
  assert(contents(JOURNAL) == unrelated && file_moves == moves);
  expect_error(from, "/seekdb/unused-target", EBUSY);
  assert(contents(JOURNAL) == unrelated && !exists("/seekdb/unused-target"));
  assert(contents(from + "/first") == "source" && contents(to + "/second") == "target");
  assert(unlink(JOURNAL) == 0);
  create(JOURNAL, record(from, to) + "unexpected");
  expect_error(from, to, ENOTEMPTY);
  assert(finish_recorded_move() == -EINVAL);
  assert(contents(JOURNAL) == record(from, to) + "unexpected");
  assert(unlink(JOURNAL) == 0);
}

void test_file_collision()
{
  const std::string from = "/seekdb/collision-source";
  const std::string to = "/seekdb/collision-target";
  directory(from);
  directory(to);
  create(from + "/first", "source");
  create(to + "/first", "target");
  create(JOURNAL, record(from, to));
  const int moves = file_moves;
  expect_error(from, to, EEXIST);
  assert(finish_recorded_move() == -EEXIST);
  assert(contents(JOURNAL) == record(from, to) && file_moves == moves);
  assert(contents(from + "/first") == "source" && contents(to + "/first") == "target");
  assert(unlink((to + "/first").c_str()) == 0);
  assert(rename(from.c_str(), to.c_str()) == 0);
  assert(!exists(JOURNAL) && contents(to + "/first") == "source");
}

void test_journal_failures()
{
  const std::string from = "/seekdb/journal-source";
  const std::string to = "/seekdb/journal-target";
  directory(from);
  create(from + "/first", "source");
  const int moves = file_moves;
  fail_write = true;
  expect_error(from, to, EBUSY);
  fail_write = false;
  assert(!exists(JOURNAL) && !exists(to) && file_moves == moves);
  fail_flush = true;
  expect_error(from, to, EBUSY);
  fail_flush = false;
  assert(!exists(JOURNAL) && !exists(to) && file_moves == moves);
  assert(contents(from + "/first") == "source");
  assert(rename(from.c_str(), to.c_str()) == 0);
  assert(!exists(JOURNAL) && contents(to + "/first") == "source");
}

void test_startup_recovery()
{
  const std::string from = "/seekdb/startup-source";
  const std::string to = "/seekdb/startup-target";
  directory(from);
  create(from + "/first", "first");
  create(from + "/second", "second");
  failures_remaining = 2;
  expect_error(from, to, EIO);
  assert(finish_recorded_move() == -EIO);
  assert(contents(JOURNAL) == record(from, to));
  assert(contents(to + "/first") == "first" && contents(from + "/second") == "second");
  assert(finish_recorded_move() == 0);
  assert(!exists(from) && !exists(JOURNAL));
  assert(contents(to + "/first") == "first" && contents(to + "/second") == "second");
  assert(finish_recorded_move() == 0);
}

void test_startup_missing_source()
{
  const std::string from = "/seekdb/missing-source";
  const std::string to = "/seekdb/missing-target";
  create(JOURNAL, record(from, to));
  assert(finish_recorded_move() == -ENOENT);
  assert(contents(JOURNAL) == record(from, to));
  create(to, "file");
  assert(finish_recorded_move() == -ENOTDIR);
  assert(contents(JOURNAL) == record(from, to));
  assert(unlink(to.c_str()) == 0);
  directory(to);
  create(from, "file");
  assert(finish_recorded_move() == -ENOTDIR);
  assert(contents(JOURNAL) == record(from, to));
  assert(unlink(from.c_str()) == 0);
  assert(finish_recorded_move() == 0);
  assert(!exists(JOURNAL) && exists(to));
}

void test_completed_journal_cleanup()
{
  for (const bool persistent : {false, true}) {
    const std::string prefix = persistent ? "/seekdb/cleanup-persistent" : "/seekdb/cleanup-once";
    const std::string from = prefix + "-source";
    const std::string to = prefix + "-target";
    const std::string next_from = prefix + "-next-source";
    const std::string next_to = prefix + "-next-target";
    directory(from);
    directory(next_from);
    create(from + "/first", "first");
    create(next_from + "/first", "next");
    fail_unlinks = persistent ? -1 : 1;
    assert(rename(from.c_str(), to.c_str()) == 0);
    assert(!exists(from) && contents(to + "/first") == "first");
    assert(contents(JOURNAL) == record(from, to));
    if (persistent) {
      const int moves = file_moves;
      expect_error(next_from, next_to, EBUSY);
      assert(file_moves == moves && !exists(next_to));
      assert(contents(next_from + "/first") == "next");
      assert(contents(JOURNAL) == record(from, to));
      fail_unlinks = 0;
    }
    assert(rename(next_from.c_str(), next_to.c_str()) == 0);
    assert(!exists(next_from) && !exists(JOURNAL));
    assert(contents(next_to + "/first") == "next");
  }
}

}

extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" ssize_t __wrap_write(int fd, const void *buffer, size_t length)
{
  if (fail_write) {
    errno = ENOSPC;
    return -1;
  }
  return __real_write(fd, buffer, length);
}

extern "C" int __real_fsync(int);
extern "C" int __wrap_fsync(int fd)
{
  if (fail_flush) {
    errno = EIO;
    return -1;
  }
  return __real_fsync(fd);
}

extern "C" int __real_unlink(const char *);
extern "C" int __wrap_unlink(const char *path)
{
  if (std::strcmp(path, JOURNAL) == 0 && fail_unlinks != 0) {
    if (fail_unlinks > 0) --fail_unlinks;
    errno = EIO;
    return -1;
  }
  return __real_unlink(path);
}

int main()
{
  auto backend = wasmfs::wasmFS.addBackend(std::make_unique<MoveBackend>());
  assert(wasmfs_create_directory("/seekdb", 0755, reinterpret_cast<backend_t>(backend)) == 0);
  test_repeated_retry();
  test_nested_retry();
  test_unrelated_destinations();
  test_file_collision();
  test_journal_failures();
  test_startup_recovery();
  test_startup_missing_source();
  test_completed_journal_cleanup();
  std::puts("WasmFS directory move retries, journal ownership and collision checks passed");
}
