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

#define USING_LOG_PREFIX LIB

#include "thread.h"
#include <inttypes.h>
#include <pthread.h>
#ifdef _WIN32
#include <windows.h>
#ifdef ERROR
#undef ERROR
#endif
#include <io.h>
#include <process.h>
#endif
#include "lib/utility/ob_platform_utils.h"  // Platform compatibility layer
#include "lib/rc/context.h"
#include "lib/utility/ob_hang_fatal_error.h"
#include "lib/thread_local/ob_tsi_factory.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;
thread_local Thread* Thread::current_thread_ = nullptr;
int64_t Thread::total_thread_count_ = 0;

Thread &Thread::current()
{
  assert(current_thread_ != nullptr);
  return *current_thread_;
}

Thread::Thread(Threads *threads, int64_t idx)
#ifdef _WIN32
    : pth_(pthread_null()),
#else
    : pth_(0),
#endif
      threads_(threads),
      idx_(idx),
      stop_(true),
      join_concurrency_(0),
      tid_(0),
      thread_list_node_(this),
      cpu_time_(0),
      create_ret_(OB_NOT_RUNNING)
{}

Thread::~Thread()
{
  destroy();
}

int Thread::start()
{
  int ret = OB_SUCCESS;
  const int64_t count = ATOMIC_FAA(&total_thread_count_, 1);
  if (count >= get_max_thread_num() - OB_RESERVED_THREAD_NUM) {
    ret = OB_SIZE_OVERFLOW;
    LOG_ERROR("thread count reach limit", K(ret), "current count", count);
  } else {
    pthread_attr_t attr;
    bool need_destroy = false;
    int pret = pthread_attr_init(&attr);
    if (pret == 0) {
      need_destroy = true;
      // Set high priority QoS for new threads (platform-independent)
      // On macOS, daemon processes get low QoS priority by default, causing scheduling delays.
      int qos_ret = ob_pthread_attr_set_qos(&attr, ObThreadQoS::USER_INITIATED);
      if (qos_ret != 0) {
        LOG_WARN("ob_pthread_attr_set_qos failed", K(qos_ret));
        // Continue even if QoS setting failed
      }
      // Threads use the stack provided by the platform's default thread attributes.
      // SMART_CALL based stack checks keep working: they query the real stack
      // bounds with pthread_getattr_np()/GetCurrentThreadStackLimits(), so they
      // now see the larger default stack.
    }
    if (pret == 0) {
      ATOMIC_STORE(&stop_, false);
      pret = pthread_create(&pth_, &attr, __th_start, this);
      if (pret != 0) {
        LOG_ERROR("pthread create failed", K(pret), K(errno));
#ifdef _WIN32
        pth_ = pthread_null();
#else
        pth_ = 0;
#endif
      } else {
        while (ATOMIC_LOAD(&create_ret_) == OB_NOT_RUNNING) {
          sched_yield();
        }
        if (OB_FAIL(create_ret_)) {
        }
      }
    }
    if (0 != pret) {
      ret = OB_ERR_SYS;
      ATOMIC_STORE(&stop_, true);
    }
    if (need_destroy) {
      pthread_attr_destroy(&attr);
    }
  }
  if (OB_FAIL(ret)) {
    ATOMIC_FAA(&total_thread_count_, -1);
    destroy();
  }
  return ret;
}

void Thread::stop()
{
  ATOMIC_STORE(&stop_, true);
}

void Thread::run()
{
  IRunWrapper *run_wrapper_ = threads_->get_effective_run_wrapper();
  if (OB_NOT_NULL(run_wrapper_)) {
    {
      ObDisableDiagnoseGuard disable_guard;
      run_wrapper_->pre_run();
    }
    threads_->run(idx_);
    {
      ObDisableDiagnoseGuard disable_guard;
      run_wrapper_->end_run();
    }
  } else {
    threads_->run(idx_);
  }
}

void Thread::wait()
{
  int ret = 0;
#ifdef _WIN32
  if (!pthread_is_null(pth_)) {
#else
  if (pth_ != 0) {
#endif
    if (0 != (ret = pthread_join(pth_, nullptr))) {
      LOG_ERROR("pthread_join failed", K(ret), K(errno));
      ob_abort();
    } else {
      reset_pth();
    }
  }
}

void Thread::reset_pth()
{
#ifdef _WIN32
  pth_ = pthread_null();
#else
  pth_ = 0;
#endif
}

int Thread::try_wait()
{
  int ret = OB_SUCCESS;
#ifdef _WIN32
  if (!pthread_is_null(pth_)) {
#else
  if (pth_ != 0) {
#endif
    int pret = 0;
#if defined(__APPLE__) || defined(__ANDROID__)
    if (pthread_kill(pth_, 0) == 0) {
      ret = OB_EAGAIN;
    } else {
      if (0 != (pret = pthread_join(pth_, nullptr))) {
        ret = OB_EAGAIN;
        LOG_WARN("pthread_join failed", K(pret), K(errno), K(ret), K(oceanbase::lib::Thread::tid_));
      } else {
        reset_pth();
      }
    }
#elif defined(_WIN32)
    {
      HANDLE hThread = pth_.p;
      DWORD wait_ret = WaitForSingleObject(hThread, 0);
      if (wait_ret == WAIT_OBJECT_0) {
        if (0 != (pret = pthread_join(pth_, nullptr))) {
          ret = OB_EAGAIN;
          LOG_WARN("pthread_join failed", K(pret), K(errno), K(ret), K(oceanbase::lib::Thread::tid_));
        } else {
          reset_pth();
        }
      } else {
        ret = OB_EAGAIN;
      }
    }
#elif defined(__linux__) || defined(__EMSCRIPTEN__)
    if (0 != (pret = pthread_tryjoin_np(pth_, nullptr))) {
      ret = OB_EAGAIN;
      if (EBUSY != pret) {
        LOG_WARN("pthread_tryjoin_np failed", K(pret), K(errno), K(ret), K(oceanbase::lib::Thread::tid_));
      }
    } else {
      reset_pth();
    }
#endif
  }
  return ret;
}

void Thread::destroy()
{
#ifdef _WIN32
  if (!pthread_is_null(pth_)) {
#else
  if (pth_ != 0) {
#endif
    wait();
  }
}

void* Thread::__th_start(void *arg)
{
  Thread * const th = reinterpret_cast<Thread*>(arg);
  // Set high QoS for this thread (platform-independent)
  // On macOS, threads in a daemon process inherit low QoS priority which causes scheduling delays.
  ob_set_thread_qos(ObThreadQoS::USER_INITIATED);
#ifdef __APPLE__
  // On macOS, also remove background state explicitly and signal thread start early
  // to prevent the parent thread from spin-waiting with low scheduling priority.
  setpriority(PRIO_DARWIN_THREAD, 0, 0);
  ATOMIC_STORE(&th->create_ret_, OB_SUCCESS);
#endif
  current_thread_ = th;
  th->tid_ = gettid();

  int ret = OB_SUCCESS;
  if (OB_ISNULL(th)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(th), K(ret));
  } else {
    MemoryContext *mem_context = GET_TSI0(MemoryContext);
    if (OB_ISNULL(mem_context)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("null ptr", K(ret));
    } else if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(*mem_context,
                       ContextParam().set_properties(RETURN_MALLOC_DEFAULT)
                                     .set_label("ThreadRoot")))) {
    } else {
      WITH_CONTEXT(*mem_context) {
#if !defined(__APPLE__)
        ATOMIC_STORE(&th->create_ret_, OB_SUCCESS);
#endif
        th->run();
      }
    }
    if (mem_context != nullptr && *mem_context != nullptr) {
      DESTROY_CONTEXT(*mem_context);
    }
  }
  if (OB_FAIL(ret)) {
    ATOMIC_STORE(&th->create_ret_, ret);
  }
  ATOMIC_FAA(&total_thread_count_, -1);
  return nullptr;
}

#ifdef __APPLE__
#include <mach/thread_info.h>
#include <mach/mach.h>
#endif

int Thread::get_cpu_time_inc(int64_t &cpu_time_inc)
{
  int ret = OB_SUCCESS;
  const pid_t pid = getpid();
  const int64_t tid = tid_;
  int64_t cpu_time = 0;
  cpu_time_inc = 0;

#ifdef __EMSCRIPTEN__
  return OB_NOT_SUPPORTED;
#elif defined(__APPLE__)
  // macOS doesn't have /proc, use mach APIs
  thread_port_t mach_thread = pthread_mach_thread_np(pth_);
  thread_basic_info_data_t basic_info;
  mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
  if (KERN_SUCCESS != thread_info(mach_thread, THREAD_BASIC_INFO, (thread_info_t)&basic_info, &count)) {
    ret = OB_ERR_SYS;
  } else {
    cpu_time = (int64_t)basic_info.user_time.seconds * 1000000 + basic_info.user_time.microseconds
             + (int64_t)basic_info.system_time.seconds * 1000000 + basic_info.system_time.microseconds;
  }
#elif defined(_WIN32)
  HANDLE hThread = OpenThread(THREAD_QUERY_INFORMATION, FALSE, (DWORD)tid);
  if (hThread != NULL) {
    FILETIME creation_time, exit_time, kernel_time, user_time;
    if (GetThreadTimes(hThread, &creation_time, &exit_time, &kernel_time, &user_time)) {
      ULARGE_INTEGER utime, ktime;
      utime.LowPart = user_time.dwLowDateTime;
      utime.HighPart = user_time.dwHighDateTime;
      ktime.LowPart = kernel_time.dwLowDateTime;
      ktime.HighPart = kernel_time.dwHighDateTime;
      cpu_time = (int64_t)((utime.QuadPart + ktime.QuadPart) / 10);
    } else {
      ret = OB_ERR_SYS;
    }
    CloseHandle(hThread);
  } else {
    ret = OB_ERR_SYS;
  }
#else
  int fd = -1;
  int64_t read_size = -1;
  int32_t PATH_BUFSIZE = 512;
  int32_t MAX_LINE_LENGTH = 1024;
  int32_t VALUE_BUFSIZE = 32;
  char stat_path[PATH_BUFSIZE];
  char stat_content[MAX_LINE_LENGTH];

  if (tid == 0) {
    ret = OB_NOT_INIT;
  } else {
    snprintf(stat_path, PATH_BUFSIZE, "/proc/%d/task/%" PRId64 "/stat", pid, tid);
    if ((fd = ::open(stat_path, O_RDONLY)) < 0) {
      ret = OB_IO_ERROR;
    } else if ((read_size = read(fd, stat_content, MAX_LINE_LENGTH)) < 0) {
      ret = OB_IO_ERROR;
    } else {
      // do nothing
    }
    if (fd >= 0) {
      close(fd);
    }
  }

  if (OB_SUCC(ret)) {
    const int USER_TIME_FIELD_INDEX = 13;
    const int SYSTEM_TIME_FIELD_INDEX = 14;
    int field_index = 0;
    char *save_ptr = nullptr;
    char *field_ptr = strtok_r(stat_content, " ", &save_ptr);
    while (field_ptr != NULL) {
      if (field_index == USER_TIME_FIELD_INDEX) {
        cpu_time += strtoul(field_ptr, NULL, 10) * 1000000 / sysconf(_SC_CLK_TCK);
      }
      if (field_index == SYSTEM_TIME_FIELD_INDEX) {
        cpu_time += strtoul(field_ptr, NULL, 10) * 1000000 / sysconf(_SC_CLK_TCK);
        break;
      }
      field_ptr = strtok_r(NULL, " ", &save_ptr);
      field_index++;
    }
  }
#endif

  if (OB_SUCC(ret)) {
    cpu_time_inc = cpu_time - cpu_time_;
    cpu_time_ = cpu_time;
  }
  return ret;
}

namespace oceanbase
{
namespace lib
{
int OB_WEAK_SYMBOL get_max_thread_num()
{
  return 4096;
}
}
}
