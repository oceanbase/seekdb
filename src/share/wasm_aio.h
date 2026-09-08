// Copyright (c) 2026 OceanBase.
// SPDX-License-Identifier: Apache-2.0
#ifndef OCEANBASE_SHARE_WASM_AIO_H_
#define OCEANBASE_SHARE_WASM_AIO_H_

#include <cerrno>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <new>
#include <unistd.h>

// The device submits from its I/O threads. File operations are synchronous;
// completion delivery retains the libaio contract used by ObAsyncIOChannel.
// This does not add durability to the underlying Emscripten filesystem.
struct iocb {
  void *data;
  short aio_lio_opcode;
  int aio_fildes;
  void *aio_buf;
  size_t aio_nbytes;
  long long aio_offset;
};
struct io_event { void *data; iocb *obj; long res; long res2; };

namespace oceanbase::share::wasm {
class IOContext {
public:
  explicit IOContext(unsigned capacity) : capacity_(capacity) {}
  ~IOContext() { delete[] events_; }
  std::mutex mutex_;
  std::condition_variable ready_;
  io_event *events_ = nullptr;
  const unsigned capacity_;
  unsigned head_ = 0;
  unsigned size_ = 0;
  unsigned flying_ = 0;
};
} // namespace oceanbase::share::wasm
using io_context_t = oceanbase::share::wasm::IOContext *;

inline int io_setup(unsigned maxevents, io_context_t *ctxp)
{
  const size_t requested = maxevents;
  if (ctxp == nullptr || *ctxp != nullptr || maxevents == 0
      || maxevents > INT_MAX || requested > SIZE_MAX / sizeof(io_event)) {
    return -EINVAL;
  }
  auto *ctx = new (std::nothrow) oceanbase::share::wasm::IOContext(maxevents);
  if (ctx == nullptr) return -ENOMEM;
  ctx->events_ = new (std::nothrow) io_event[maxevents];
  if (ctx->events_ == nullptr) {
    delete ctx;
    return -ENOMEM;
  }
  *ctxp = ctx;
  return 0;
}

// As with the owning device channel, callers must stop submitters and join
// event consumers before destroying the context.
inline int io_destroy(io_context_t ctx)
{
  if (ctx == nullptr) return -EINVAL;
  {
    std::lock_guard<std::mutex> lock(ctx->mutex_);
    if (ctx->flying_ != 0) return -EBUSY;
  }
  delete ctx;
  return 0;
}

inline void io_prep_pwrite(iocb *cb, int fd, void *buf, size_t count, long long offset)
{
  cb->aio_fildes = fd;
  cb->aio_buf = buf;
  cb->aio_nbytes = count;
  cb->aio_offset = offset;
  cb->aio_lio_opcode = 1;
}
inline void io_prep_pread(iocb *cb, int fd, void *buf, size_t count, long long offset)
{
  io_prep_pwrite(cb, fd, buf, count, offset);
  cb->aio_lio_opcode = 0;
}

inline int io_submit(io_context_t ctx, long nr, iocb **requests)
{
  if (ctx == nullptr || nr < 0 || (nr > 0 && requests == nullptr)) return -EINVAL;
  int submitted = 0;
  for (long i = 0; i < nr; ++i) {
    iocb *cb = requests[i];
    int error = 0;
    if (cb == nullptr || cb->aio_lio_opcode < 0 || cb->aio_lio_opcode > 1
        || cb->aio_offset < 0 || cb->aio_nbytes > SSIZE_MAX) {
      error = EINVAL;
    } else {
      std::lock_guard<std::mutex> lock(ctx->mutex_);
      if (ctx->size_ + ctx->flying_ == ctx->capacity_) error = EAGAIN;
      else ++ctx->flying_;
    }
    if (error != 0) return submitted > 0 ? submitted : -error;

    ssize_t result;
    do {
      result = cb->aio_lio_opcode == 1
          ? pwrite(cb->aio_fildes, cb->aio_buf, cb->aio_nbytes, cb->aio_offset)
          : pread(cb->aio_fildes, cb->aio_buf, cb->aio_nbytes, cb->aio_offset);
    } while (result < 0 && errno == EINTR);
    // Capture errno before taking a lock. Short I/O is a completion with the
    // actual byte count; the engine owns its retry policy.
    const long completion = result < 0 ? -errno : result;
    {
      std::lock_guard<std::mutex> lock(ctx->mutex_);
      const unsigned tail = (ctx->head_ + ctx->size_) % ctx->capacity_;
      ctx->events_[tail] = {cb->data, cb, completion, 0};
      --ctx->flying_;
      ++ctx->size_;
    }
    ctx->ready_.notify_all();
    ++submitted;
  }
  return submitted;
}

inline int io_cancel(io_context_t ctx, iocb *cb, io_event *result)
{
  if (ctx == nullptr || cb == nullptr || result == nullptr) return -EINVAL;
  // A synchronous syscall cannot be interrupted here. Its event must still
  // be reaped, so the channel releases the request's filesystem reference once.
  return -EAGAIN;
}

inline int io_getevents(io_context_t ctx, long min_nr, long nr,
                       io_event *events, timespec *timeout)
{
  if (ctx == nullptr || min_nr < 0 || nr < min_nr || (nr > 0 && events == nullptr)
      || static_cast<unsigned long>(min_nr) > ctx->capacity_) return -EINVAL;
  using Nanoseconds = std::chrono::nanoseconds;
  if (timeout != nullptr && (timeout->tv_sec < 0 || timeout->tv_nsec < 0
      || timeout->tv_nsec >= 1000000000
      || timeout->tv_sec > (INT64_MAX - timeout->tv_nsec) / 1000000000)) return -EINVAL;
  std::unique_lock<std::mutex> lock(ctx->mutex_);
  auto ready = [&] { return ctx->size_ >= static_cast<unsigned long>(min_nr); };
  if (!ready()) {
    if (timeout == nullptr) ctx->ready_.wait(lock, ready);
    else {
      // Avoid overflowing steady_clock's absolute deadline for huge timeouts.
      const auto remaining = std::chrono::steady_clock::time_point::max()
          - std::chrono::steady_clock::now();
      auto duration = Nanoseconds(timeout->tv_sec * INT64_C(1000000000) + timeout->tv_nsec);
      if (duration > remaining / 2) duration = remaining / 2;
      ctx->ready_.wait_for(lock, duration, ready);
    }
  }
  int count = 0;
  while (count < nr && ctx->size_ > 0) {
    events[count++] = ctx->events_[ctx->head_];
    ctx->head_ = (ctx->head_ + 1) % ctx->capacity_;
    --ctx->size_;
  }
  return count;
}
#endif
