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

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <emscripten/wasmfs.h>
#include "memory_backend.h"
#include "wasmfs.h"

namespace wasmfs {
namespace {

class ChunkedMemoryDataFile final : public DataFile {
public:
  ChunkedMemoryDataFile(mode_t mode, backend_t backend) : DataFile(mode, backend) {}

  ~ChunkedMemoryDataFile() override
  {
    for (size_t i = 0; i < page_capacity_; ++i) {
      std::free(pages_[i]);
    }
    std::free(pages_);
  }

private:
  static constexpr size_t CHUNK_SIZE = 64 * 1024;
  static constexpr size_t MAX_PAGES = std::numeric_limits<size_t>::max() / sizeof(uint8_t *);
  static constexpr uint64_t MAX_OFFSET = std::numeric_limits<off_t>::max() - 511;
  static constexpr off_t MAX_FILE_SIZE = static_cast<off_t>(std::min(
      std::min<uint64_t>(MAX_PAGES, MAX_OFFSET / CHUNK_SIZE) * CHUNK_SIZE,
      std::min<uint64_t>(std::numeric_limits<blkcnt_t>::max(), MAX_OFFSET / 512) * 512));

  int open(oflags_t) override { return 0; }
  int close() override { return 0; }
  int flush() override { return 0; }
  off_t getSize() override { return size_; }

  int ensurePage(size_t index)
  {
    if (index >= MAX_PAGES) {
      return -EFBIG;
    }
    if (index >= page_capacity_) {
      const size_t doubled = page_capacity_ > MAX_PAGES / 2 ? MAX_PAGES : page_capacity_ * 2;
      const size_t capacity = std::max(index + 1, std::max(size_t{16}, doubled));
      auto **pages = static_cast<uint8_t **>(std::realloc(pages_, capacity * sizeof(*pages_)));
      if (pages == nullptr) {
        return -ENOMEM;
      }
      std::memset(pages + page_capacity_, 0, (capacity - page_capacity_) * sizeof(*pages_));
      pages_ = pages;
      page_capacity_ = capacity;
    }
    if (pages_[index] == nullptr) {
      pages_[index] = static_cast<uint8_t *>(std::calloc(CHUNK_SIZE, 1));
      if (pages_[index] == nullptr) {
        return -ENOMEM;
      }
    }
    return 0;
  }

  ssize_t write(const uint8_t *buffer, size_t length, off_t offset) override
  {
    if (offset < 0 || length > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
      return -EINVAL;
    }
    if (length == 0) {
      return 0;
    }
    const uint64_t start = static_cast<uint64_t>(offset);
    if (start > static_cast<uint64_t>(MAX_FILE_SIZE)
        || length > static_cast<uint64_t>(MAX_FILE_SIZE) - start) {
      return -EFBIG;
    }
    size_t written = 0;
    while (written < length) {
      const uint64_t position = start + written;
      const size_t index = static_cast<size_t>(position / CHUNK_SIZE);
      const size_t within_page = static_cast<size_t>(position % CHUNK_SIZE);
      const size_t count = std::min(length - written, CHUNK_SIZE - within_page);
      const bool zero = std::all_of(buffer + written, buffer + written + count,
          [](uint8_t value) { return value == 0; });
      if (zero) {
        if (index < page_capacity_ && pages_[index] != nullptr) {
          std::memset(pages_[index] + within_page, 0, count);
          if (count == CHUNK_SIZE || std::all_of(pages_[index], pages_[index] + CHUNK_SIZE,
                  [](uint8_t value) { return value == 0; })) {
            std::free(pages_[index]);
            pages_[index] = nullptr;
          }
        }
      } else {
        const int error = ensurePage(index);
        if (error != 0) {
          return written == 0 ? error : static_cast<ssize_t>(written);
        }
        std::memcpy(pages_[index] + within_page, buffer + written, count);
      }
      written += count;
      size_ = std::max(size_, static_cast<off_t>(start + written));
    }
    return static_cast<ssize_t>(written);
  }

  ssize_t read(uint8_t *buffer, size_t length, off_t offset) override
  {
    if (offset < 0 || length > static_cast<size_t>(std::numeric_limits<ssize_t>::max())) {
      return -EINVAL;
    }
    if (offset >= size_) {
      return 0;
    }
    length = static_cast<size_t>(std::min(static_cast<uint64_t>(length),
        static_cast<uint64_t>(size_ - offset)));
    size_t copied = 0;
    while (copied < length) {
      const uint64_t position = static_cast<uint64_t>(offset) + copied;
      const uint64_t index = position / CHUNK_SIZE;
      const size_t within_page = static_cast<size_t>(position % CHUNK_SIZE);
      const size_t count = std::min(length - copied, CHUNK_SIZE - within_page);
      if (index < page_capacity_ && pages_[static_cast<size_t>(index)] != nullptr) {
        std::memcpy(buffer + copied, pages_[static_cast<size_t>(index)] + within_page, count);
      } else {
        std::memset(buffer + copied, 0, count);
      }
      copied += count;
    }
    return static_cast<ssize_t>(copied);
  }

  int setSize(off_t size) override
  {
    if (size < 0) {
      return -EINVAL;
    }
    if (size > MAX_FILE_SIZE) {
      return -EFBIG;
    }
    if (size == 0) {
      for (size_t i = 0; i < page_capacity_; ++i) {
        std::free(pages_[i]);
      }
      std::free(pages_);
      pages_ = nullptr;
      page_capacity_ = 0;
      size_ = 0;
      return 0;
    }
    if (size < size_) {
      const uint64_t index = static_cast<uint64_t>(size) / CHUNK_SIZE;
      const size_t remainder = static_cast<size_t>(size % CHUNK_SIZE);
      if (remainder != 0 && index < page_capacity_ && pages_[static_cast<size_t>(index)] != nullptr) {
        std::memset(pages_[static_cast<size_t>(index)] + remainder, 0, CHUNK_SIZE - remainder);
      }
      const uint64_t first_removed = index + (remainder != 0);
      for (size_t i = static_cast<size_t>(std::min(first_removed,
               static_cast<uint64_t>(page_capacity_))); i < page_capacity_; ++i) {
        std::free(pages_[i]);
        pages_[i] = nullptr;
      }
    }
    size_ = size;
    return 0;
  }

  uint8_t **pages_ = nullptr;
  size_t page_capacity_ = 0;
  off_t size_ = 0;
};

class ChunkedMemoryBackend final : public Backend {
public:
  std::shared_ptr<DataFile> createFile(mode_t mode) override
  {
    return std::make_shared<ChunkedMemoryDataFile>(mode, this);
  }

  std::shared_ptr<Directory> createDirectory(mode_t mode) override
  {
    return std::make_shared<MemoryDirectory>(mode, this);
  }

  std::shared_ptr<Symlink> createSymlink(std::string target) override
  {
    return std::make_shared<MemorySymlink>(target, this);
  }
};

}
}

extern "C" backend_t seekdb_create_memory_backend()
{
  return reinterpret_cast<backend_t>(
      wasmfs::wasmFS.addBackend(std::make_unique<wasmfs::ChunkedMemoryBackend>()));
}
