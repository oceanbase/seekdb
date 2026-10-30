/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#pragma once

#include "hipvs_hnsw_bridge.h"
#include <vsag/vsag.h>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <vector>

namespace oceanbase { namespace common { namespace obvsag {

template<class T> class HipvsBufferAllocator {
public:
  using value_type = T;
  vsag::Allocator *allocator;
  explicit HipvsBufferAllocator(vsag::Allocator *a = nullptr) : allocator(a) {}
  template<class U> HipvsBufferAllocator(const HipvsBufferAllocator<U> &a) : allocator(a.allocator) {}
  T *allocate(size_t count) {
    if (count > std::numeric_limits<size_t>::max() / sizeof(T)) throw std::bad_alloc();
    if (!allocator) return std::allocator<T>().allocate(count);
    auto *p = static_cast<T *>(allocator->Allocate(count * sizeof(T)));
    if (!p) throw std::bad_alloc();
    return p;
  }
  void deallocate(T *p, size_t count) {
    if (allocator) allocator->Deallocate(p); else std::allocator<T>().deallocate(p, count);
  }
  template<class U> bool operator==(const HipvsBufferAllocator<U> &a) const { return allocator == a.allocator; }
  template<class U> bool operator!=(const HipvsBufferAllocator<U> &a) const { return !(*this == a); }
};

class HipvsHnswBuilder {
public:
  explicit HipvsHnswBuilder(vsag::Allocator *a)
      : vectors_(HipvsBufferAllocator<float>(a)), ids_(HipvsBufferAllocator<int64_t>(a)) {}

  bool append(const vsag::DatasetPtr &data, int dim) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (finished_) return false;
    if (!error_.empty()) throw std::runtime_error(error_);
    const int64_t count = data->GetNumElements();
    if (count < 0 || dim <= 0 || data->GetDim() != dim ||
        (count && (!data->GetIds() || !data->GetFloat32Vectors())) ||
        static_cast<uint64_t>(count) > (std::numeric_limits<size_t>::max() - vectors_.size()) / dim) {
      throw std::invalid_argument("invalid hipVS input batch");
    }
    if (count) {
      // Reserve both first so allocation failure cannot leave half a batch.
      const size_t n = ids_.size() + count;
      if (n > UINT32_MAX) throw std::invalid_argument("hipVS HNSW exceeds uint32 node limit");
      if (n > ids_.capacity()) ids_.reserve(std::max(n, ids_.capacity() * 2));
      if (n * dim > vectors_.capacity()) vectors_.reserve(std::max(n * dim, vectors_.capacity() * 2));
      vectors_.insert(vectors_.end(), data->GetFloat32Vectors(), data->GetFloat32Vectors() + count * dim);
      ids_.insert(ids_.end(), data->GetIds(), data->GetIds() + count);
    }
    return true;
  }

  int64_t count(int64_t native_count) {
    std::lock_guard<std::mutex> guard(mutex_);
    return finished_ ? native_count : static_cast<int64_t>(ids_.size());
  }

  bool bounds(int64_t &lo, int64_t &hi) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (finished_) return false;
    if (!ids_.empty()) {
      auto pair = std::minmax_element(ids_.begin(), ids_.end());
      lo = *pair.first; hi = *pair.second;
    }
    return true;
  }

  void loaded() {
    std::lock_guard<std::mutex> guard(mutex_);
    finished_ = true;
    release_buffers();
  }

  // Returns 1 for actual GPU construction, 0 for the explicit tiny-index CPU
  // path, and -1 if already finalized. Errors never turn into CPU fallback.
  int finish(vsag::Index &index, int dim, int m, int ef, SeekdbHipvsBuildStats &stats) {
    std::lock_guard<std::mutex> guard(mutex_);
    if (finished_) return -1;
    if (!error_.empty()) throw std::runtime_error(error_);
    try {
      auto sorted_ids = ids_;
      std::sort(sorted_ids.begin(), sorted_ids.end());
      if (std::adjacent_find(sorted_ids.begin(), sorted_ids.end()) != sorted_ids.end()) {
        throw std::invalid_argument("duplicate document IDs in hipVS build input");
      }
      if (ids_.size() <= std::max<size_t>(128, 4 * m)) {
        if (!ids_.empty()) {
          auto data = vsag::Dataset::Make()->NumElements(ids_.size())->Dim(dim)
              ->Float32Vectors(vectors_.data())->Ids(ids_.data())->Owner(false);
          auto result = index.Build(data);
          if (!result.has_value()) throw std::runtime_error(result.error().message);
        }
        finished_ = true; release_buffers(); return 0;
      }
      vsag::Binary blob = run_worker(dim, m, ef, stats);
      validate_and_map(blob, dim, m, ef);
      vsag::BinarySet set;
      set.Set("hnsw_data", blob);
      auto result = index.Deserialize(set);
      if (!result.has_value()) throw std::runtime_error(result.error().message);
      if (index.GetNumElements() != static_cast<int64_t>(ids_.size())) {
        throw std::runtime_error("VSAG imported an incomplete hipVS HNSW");
      }
      finished_ = true; release_buffers(); return 1;
    } catch (const std::exception &e) {
      error_ = e.what();
      throw;
    }
  }

private:
  class AnonymousFile {
  public:
    int fd = -1;
    AnonymousFile() {
      const int temporary = memfd_create("seekdb-hipvs", MFD_CLOEXEC);
      if (temporary < 0) throw std::runtime_error("cannot create hipVS anonymous file");
      fd = fcntl(temporary, F_DUPFD_CLOEXEC, 5);
      close(temporary);
      if (fd < 0) throw std::runtime_error("cannot reserve hipVS worker file descriptor");
    }
    ~AnonymousFile() { if (fd >= 0) close(fd); }
    AnonymousFile(const AnonymousFile &) = delete;
    AnonymousFile &operator=(const AnonymousFile &) = delete;
  };
  static void write_all(int fd, const void *data, size_t size) {
    const char *p = static_cast<const char *>(data);
    while (size) {
      const ssize_t n = write(fd, p, size);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) throw std::runtime_error("cannot write hipVS worker dataset");
      p += n; size -= n;
    }
  }
  static void read_all(int fd, void *data, size_t size, size_t offset) {
    char *p = static_cast<char *>(data);
    while (size) {
      const ssize_t n = pread(fd, p, size, offset);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) throw std::runtime_error("incomplete hipVS worker output");
      p += n; size -= n; offset += n;
    }
  }
  vsag::Binary run_worker(int dim, int m, int ef, SeekdbHipvsBuildStats &stats) {
    AnonymousFile input, output;
    write_all(input.fd, vectors_.data(), vectors_.size() * sizeof(float));
    char executable[4096];
    const ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
    if (length <= 0 || static_cast<size_t>(length) >= sizeof(executable) - 1) {
      throw std::runtime_error("cannot locate the seekdb hipVS build worker");
    }
    executable[length] = 0;
    std::string path(executable);
    path.resize(path.find_last_of('/') + 1);
    path += "seekdb_hipvs_hnsw_worker";
    std::string count_arg = std::to_string(ids_.size()), dim_arg = std::to_string(dim);
    std::string m_arg = std::to_string(m), ef_arg = std::to_string(ef);
    char *args[] = {path.data(), count_arg.data(), dim_arg.data(), m_arg.data(), ef_arg.data(), nullptr};
    struct Actions {
      posix_spawn_file_actions_t actions;
      Actions() { if (posix_spawn_file_actions_init(&actions)) throw std::runtime_error("cannot initialize hipVS worker spawn"); }
      ~Actions() { posix_spawn_file_actions_destroy(&actions); }
    } actions;
    if (posix_spawn_file_actions_adddup2(&actions.actions, input.fd, 3) ||
        posix_spawn_file_actions_adddup2(&actions.actions, output.fd, 4) ||
        posix_spawn_file_actions_addclosefrom_np(&actions.actions, 5)) {
      throw std::runtime_error("cannot isolate hipVS worker file descriptors");
    }
    pid_t pid = -1;
    const int spawn_result = posix_spawn(&pid, path.c_str(), &actions.actions, nullptr, args, ::environ);
    if (spawn_result != 0) {
      throw std::runtime_error("cannot launch hipVS worker: " + std::string(std::strerror(spawn_result)));
    }
    // posix_spawn avoids calling non-async-safe code after fork in the
    // multithreaded database. ROCm and its allocators live only in the child.
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(30);
    for (;;) {
      const pid_t result = waitpid(pid, &status, WNOHANG);
      if (result == pid) break;
      if (result < 0 && errno != EINTR) throw std::runtime_error("cannot reap hipVS worker");
      if (std::chrono::steady_clock::now() >= deadline) {
        kill(pid, SIGKILL);
        while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        throw std::runtime_error("hipVS worker exceeded its 30 minute deadline");
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!WIFEXITED(status)) throw std::runtime_error("hipVS worker terminated by signal " + std::to_string(WTERMSIG(status)));
    SeekdbHipvsWorkerReply reply{};
    read_all(output.fd, &reply, sizeof(reply), 0);
    if (WEXITSTATUS(status) != 0 || reply.status != 0) {
      reply.error[sizeof(reply.error) - 1] = 0;
      throw std::runtime_error(reply.error[0] ? reply.error : "hipVS worker failed");
    }
    struct stat info{};
    if (fstat(output.fd, &info) || reply.output_size < 96 ||
        reply.output_size != static_cast<uint64_t>(info.st_size) - sizeof(reply)) {
      throw std::runtime_error("invalid hipVS worker output size");
    }
    vsag::Binary blob;
    blob.size = reply.output_size;
    blob.data.reset(new int8_t[blob.size]);
    read_all(output.fd, blob.data.get(), blob.size, sizeof(reply));
    stats = reply.stats;
    return blob;
  }

  template<class T> static T read(const int8_t *data, size_t offset) {
    T value; std::memcpy(&value, data + offset, sizeof(T)); return value;
  }
  void validate_and_map(vsag::Binary &blob, int dim, int m, int ef) {
    static_assert(sizeof(size_t) == 8, "hipVS HNSW import requires a 64-bit host");
    // hipVS CPU hierarchy emits the standard 64-bit little-endian hnswlib
    // header. VSAG 1.1 accepts it as its hnsw_data BinarySet entry. Check the
    // exact layout rather than guessing compatibility from the file suffix.
    const auto *b = blob.data.get();
    const size_t stride = 4 + 8 * m + 4 * static_cast<size_t>(dim) + 8;
    if (blob.size < 96 || read<uint64_t>(b, 0) != 0 ||
        read<uint64_t>(b, 8) != ids_.size() || read<uint64_t>(b, 16) != ids_.size() ||
        read<uint64_t>(b, 24) != stride ||
        read<uint64_t>(b, 32) != stride - 8 || read<uint64_t>(b, 40) != 4 + 8 * m ||
        read<uint64_t>(b, 56) != m || read<uint64_t>(b, 64) != 2 * m ||
        read<uint64_t>(b, 72) != m || read<uint64_t>(b, 88) != ef ||
        read<uint32_t>(b, 52) >= ids_.size() || ids_.size() > (blob.size - 96) / stride) {
      throw std::runtime_error("unsupported hipVS HNSW serialization layout");
    }
    std::vector<bool> seen(ids_.size(), false);
    for (size_t i = 0; i < ids_.size(); ++i) {
      int8_t *node = blob.data.get() + 96 + i * stride;
      const auto ordinal = read<uint64_t>(node, stride - 8);
      // Conversion may reorder internal nodes: map its external label, never
      // the serialized row position, to seekdb's arbitrary document/VID ID.
      if (ordinal >= ids_.size() || seen[ordinal] ||
          std::memcmp(node + 4 + 8 * m, vectors_.data() + ordinal * dim, dim * sizeof(float)) != 0 ||
          read<uint32_t>(node, 0) > 2 * m) {
        throw std::runtime_error("hipVS HNSW lost or mislabelled a vector");
      }
      seen[ordinal] = true;
      std::memcpy(node + stride - 8, &ids_[ordinal], sizeof(int64_t));
      for (uint32_t j = 0; j < read<uint32_t>(node, 0); ++j) {
        if (read<uint32_t>(node, 4 + 4 * j) >= ids_.size()) {
          throw std::runtime_error("invalid hipVS HNSW base-layer edge");
        }
      }
    }
    const size_t layer_stride = 4 + 4 * m;
    size_t cursor = 96 + ids_.size() * stride;
    for (size_t i = 0; i < ids_.size(); ++i) {
      if (blob.size - cursor < 4) throw std::runtime_error("truncated hipVS HNSW hierarchy");
      const uint32_t length = read<uint32_t>(b, cursor);
      cursor += 4;
      if (length % layer_stride || length > blob.size - cursor) {
        throw std::runtime_error("invalid hipVS HNSW hierarchy length");
      }
      for (size_t level = 0; level < length; level += layer_stride) {
        const uint32_t degree = read<uint32_t>(b, cursor + level);
        if (degree > m) throw std::runtime_error("invalid hipVS HNSW hierarchy degree");
        for (uint32_t j = 0; j < degree; ++j) {
          if (read<uint32_t>(b, cursor + level + 4 + j * 4) >= ids_.size()) {
            throw std::runtime_error("invalid hipVS HNSW hierarchy edge");
          }
        }
      }
      cursor += length;
    }
    if (cursor != blob.size) throw std::runtime_error("unexpected hipVS HNSW trailing bytes");
  }
  void release_buffers() {
    decltype(vectors_)(vectors_.get_allocator()).swap(vectors_);
    decltype(ids_)(ids_.get_allocator()).swap(ids_);
  }
  std::mutex mutex_;
  bool finished_ = false;
  std::string error_;
  std::vector<float, HipvsBufferAllocator<float>> vectors_;
  std::vector<int64_t, HipvsBufferAllocator<int64_t>> ids_;
};

}}} // namespace oceanbase::common::obvsag
