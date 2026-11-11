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

#pragma once

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <mutex>
#include <new>
#include "lib/checksum/ob_crc64.h"

namespace oceanbase {
namespace common {

// Diagnostic-only, fixed-size records separate from the rotating server log.
// No original data bytes are persisted. A full or failed journal is NOT complete evidence.
class BlockIoProvenance
{
public:
  enum Kind { OPEN = 1, READ = 2, WRITE = 3, RESIZE = 4, ALLOC = 5, FREE = 6, CLOSE = 7, LOST = 8, MAP = 9 };
  struct Record {
    uint64_t magic = 0x314f494b4c42ULL; // BLKIO1, little-endian uint64 fields
    uint64_t kind = 0, phase = 0, sequence = 0, generation = 0;
    uint64_t pid = 0, tid = 0, fd = 0, handle = 0;
    uint64_t begin = 0, end = 0, offset = 0, requested = 0;
    uint64_t buffer = 0, digest = 0, digest_ticks = 0;
    uint64_t completed = 0, ok = 0, error = 0, file_size = UINT64_MAX;
    uint64_t detail0 = 0, detail1 = 0, detail2 = 0, detail3 = 0;
  };
  struct Ticket { Record record; int slot = -1; };
  struct CloseToken { uint64_t generation = 0; int slot = -1; };
  class LogicalScope {
  public:
    explicit LogicalScope(uint64_t key) : previous_(scope_key()) { scope_key() = key; }
    ~LogicalScope() { scope_key() = previous_; }
    LogicalScope(const LogicalScope &) = delete;
    LogicalScope &operator=(const LogicalScope &) = delete;
  private:
    uint64_t previous_;
  };

  static BlockIoProvenance &instance()
  {
    // Process lifetime: device destruction may run after ordinary static destructors.
    alignas(BlockIoProvenance) static unsigned char storage[sizeof(BlockIoProvenance)];
    static BlockIoProvenance *journal = new (storage) BlockIoProvenance;
    return *journal;
  }

  static void os_begin(Ticket &ticket)
  {
    ErrorScope errors;
    if (ticket.slot >= 0) ticket.record.detail0 = ticks();
  }

  bool attach(int fd, const char *path, int64_t block_size)
  {
    ErrorScope errors;
    std::lock_guard<std::mutex> guard(lock_);
    HANDLE data = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    BY_HANDLE_FILE_INFORMATION info = {};
    LARGE_INTEGER frequency = {};
    if (data == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(data, &info)
        || !QueryPerformanceFrequency(&frequency)) return false;
    // Bound retained files over the entire process, not merely concurrent handles.
    if (generation_ >= SLOT_COUNT) return false;
    for (Slot &slot : slots_) {
      if (slot.output != INVALID_HANDLE_VALUE) continue;
      const uint64_t generation = ++generation_;
      char name[4096];
      const int n = snprintf(name, sizeof(name), "%s.io-%lu-%llu.bin", path,
                            static_cast<unsigned long>(GetCurrentProcessId()),
                            static_cast<unsigned long long>(ticks()));
      if (n < 0 || static_cast<size_t>(n) >= sizeof(name)) return false;
      HANDLE output = CreateFileA(name, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (output == INVALID_HANDLE_VALUE) return false;
      slot.fd = fd; slot.data = data; slot.output = output; slot.generation = generation;
      slot.bytes = 0; slot.lost = 0; slot.pending = 0; slot.failed = false;
      Record record;
      record.kind = OPEN; record.generation = generation; record.pid = GetCurrentProcessId();
      record.fd = fd; record.handle = reinterpret_cast<uintptr_t>(data);
      record.begin = ticks(); record.requested = block_size;
      record.detail0 = frequency.QuadPart; record.detail1 = info.dwVolumeSerialNumber;
      record.detail2 = (uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
      record.detail3 = (uint64_t(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime;
      append(slot, record);
      return !slot.failed;
    }
    return false;
  }

  uint64_t mapping(int fd, int64_t first, int64_t second, int64_t third,
                   int64_t offset, int64_t size, const void *buffer, uint64_t cookie = 0)
  {
    ErrorScope errors;
    std::lock_guard<std::mutex> guard(lock_);
    const HANDLE data = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    for (Slot &slot : slots_) {
      if (slot.output == INVALID_HANDLE_VALUE || slot.fd != fd || slot.data != data) continue;
      Record r;
      r.kind = MAP; r.sequence = ++sequence_; r.generation = slot.generation;
      r.pid = GetCurrentProcessId(); r.tid = GetCurrentThreadId(); r.fd = fd;
      r.handle = reinterpret_cast<uintptr_t>(data); r.begin = ticks();
      r.offset = offset; r.requested = size; r.buffer = reinterpret_cast<uintptr_t>(buffer);
      r.detail0 = first; r.detail1 = second; r.detail2 = third; r.detail3 = cookie;
      append(slot, r);
      return r.sequence;
    }
    return 0;
  }

  Ticket begin(int fd, Kind kind, int64_t offset, int64_t size, const void *buffer = nullptr,
               uint64_t mapping_key = UINT64_MAX)
  {
    ErrorScope errors;
    Ticket ticket;
    {
      std::lock_guard<std::mutex> guard(lock_);
      const HANDLE data = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
      for (int i = 0; i < SLOT_COUNT; ++i) {
        Slot &slot = slots_[i];
        if (slot.output != INVALID_HANDLE_VALUE && slot.fd == fd && slot.data == data) {
          ticket.slot = i; ticket.record.generation = slot.generation;
          ticket.record.sequence = ++sequence_; ticket.record.handle = reinterpret_cast<uintptr_t>(data);
          ++slot.pending;
          break;
        }
      }
    }
    if (ticket.slot < 0) return ticket;
    Record &r = ticket.record;
    r.kind = kind; r.phase = 1; r.fd = fd; r.pid = GetCurrentProcessId(); r.tid = GetCurrentThreadId();
    r.begin = ticks(); r.offset = offset; r.requested = size; r.buffer = reinterpret_cast<uintptr_t>(buffer);
    r.detail3 = mapping_key != UINT64_MAX ? mapping_key : scope_key();
    if (kind == WRITE && size > 0 && buffer != nullptr) digest(r, buffer, size);
    LARGE_INTEGER length;
    if (GetFileSizeEx(reinterpret_cast<HANDLE>(r.handle), &length)) r.file_size = length.QuadPart;
    std::lock_guard<std::mutex> guard(lock_);
    if (slots_[ticket.slot].generation == r.generation
        && slots_[ticket.slot].output != INVALID_HANDLE_VALUE) {
      append(slots_[ticket.slot], r);
    } else {
      ticket.slot = -1;
    }
    return ticket;
  }

  void end(Ticket ticket, bool ok, uint32_t error, uint64_t bytes, const void *read_buffer = nullptr)
  {
    ErrorScope errors;
    if (ticket.slot < 0) return;
    Record &r = ticket.record;
    r.end = ticks(); r.phase = 2; r.ok = ok; r.error = error; r.completed = bytes;
    r.detail1 = static_cast<int64_t>(errors.crt);
    if (r.kind == READ && ok && bytes > 0 && bytes <= r.requested && read_buffer != nullptr) {
      digest(r, read_buffer, bytes);
    }
    LARGE_INTEGER length;
    r.file_size = UINT64_MAX;
    if (GetFileSizeEx(reinterpret_cast<HANDLE>(r.handle), &length)) r.file_size = length.QuadPart;
    std::lock_guard<std::mutex> guard(lock_);
    Slot &slot = slots_[ticket.slot];
    if (slot.generation == r.generation && slot.output != INVALID_HANDLE_VALUE) {
      append(slot, r);
      if (slot.pending > 0) --slot.pending;
    }
  }

  CloseToken before_close(int fd)
  {
    ErrorScope errors;
    std::lock_guard<std::mutex> guard(lock_);
    const HANDLE data = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    for (int i = 0; i < SLOT_COUNT; ++i) {
      if (slots_[i].output != INVALID_HANDLE_VALUE && slots_[i].fd == fd && slots_[i].data == data) {
        return {slots_[i].generation, i};
      }
    }
    return {};
  }

  void detach(CloseToken token, int close_result)
  {
    ErrorScope errors;
    std::lock_guard<std::mutex> guard(lock_);
    if (token.slot >= 0 && token.slot < SLOT_COUNT) {
      Slot &slot = slots_[token.slot];
      if (slot.output == INVALID_HANDLE_VALUE || slot.generation != token.generation) return;
      Record r; r.kind = CLOSE; r.generation = slot.generation; r.end = ticks();
      r.fd = slot.fd; r.handle = reinterpret_cast<uintptr_t>(slot.data);
      r.ok = close_result == 0; r.error = static_cast<int64_t>(errors.crt);
      r.completed = slot.bytes; r.detail0 = slot.lost; r.detail1 = slot.failed;
      r.detail2 = slot.pending;
      if (!FlushFileBuffers(slot.output)) {
        r.detail1 = 1;
        r.detail3 = GetLastError();
      }
      write(slot, r);
      if (!FlushFileBuffers(slot.output)) {
        Record lost; lost.kind = LOST; lost.generation = slot.generation;
        lost.detail0 = GetLastError(); write(slot, lost);
      }
      CloseHandle(slot.output); slot.output = INVALID_HANDLE_VALUE;
    }
  }

private:
  static constexpr int SLOT_COUNT = 8;
  static constexpr uint64_t BUDGET = 64ULL * 1024 * 1024;
  static uint64_t &scope_key() { static thread_local uint64_t key = 0; return key; }
  struct ErrorScope {
    int crt = errno; DWORD win = GetLastError();
    ~ErrorScope() { errno = crt; SetLastError(win); }
  };
  struct Slot {
    int fd = -1; HANDLE data = INVALID_HANDLE_VALUE, output = INVALID_HANDLE_VALUE;
    uint64_t generation = 0, bytes = 0, lost = 0, pending = 0; bool failed = false;
  };
  static uint64_t ticks() { LARGE_INTEGER now = {}; QueryPerformanceCounter(&now); return now.QuadPart; }
  static void digest(Record &r, const void *buffer, uint64_t size)
  {
    const uint64_t start = ticks();
    r.digest = ob_crc64(buffer, size);
    r.digest_ticks = ticks() - start;
  }
  static void write(Slot &slot, const Record &record)
  {
    DWORD written = 0;
    if (!WriteFile(slot.output, &record, sizeof(record), &written, nullptr) || written != sizeof(record)) {
      slot.failed = true;
    }
    slot.bytes += written;
  }
  static void append(Slot &slot, const Record &record)
  {
    if (slot.failed || slot.bytes + sizeof(record) > BUDGET) {
      if (slot.lost++ == 0 && !slot.failed) {
        Record lost; lost.kind = LOST; lost.generation = slot.generation; lost.begin = ticks();
        write(slot, lost);
      }
      return;
    }
    write(slot, record);
  }
  Slot slots_[SLOT_COUNT];
  std::mutex lock_;
  uint64_t sequence_ = 0, generation_ = 0;
};
static_assert(sizeof(BlockIoProvenance::Record) == 24 * sizeof(uint64_t), "journal record layout");

} // namespace common
} // namespace oceanbase
#endif
