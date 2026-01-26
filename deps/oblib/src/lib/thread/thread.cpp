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
#include "lib/utility/ob_platform_utils.h"  // Platform compatibility layer
#include "lib/rc/context.h"
#include "lib/thread/protected_stack_allocator.h"
#include "lib/utility/ob_hang_fatal_error.h"
#include "lib/signal/ob_signal_struct.h"
#include "lib/ash/ob_active_session_guard.h"
#include "lib/stat/ob_session_stat.h"
#include "lib/resource/ob_affinity_ctrl.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::lib;
thread_local int64_t Thread::loop_ts_ = 0;
thread_local pthread_t Thread::thread_joined_ = 0;
thread_local int64_t Thread::sleep_us_ = 0;
thread_local int64_t Thread::blocking_ts_ = 0;
thread_local bool Thread::is_doing_ddl_ = false;
thread_local ObAddr Thread::rpc_dest_addr_;
thread_local obrpc::ObRpcPacketCode Thread::pcode_ = obrpc::ObRpcPacketCode::OB_INVALID_RPC_CODE;
thread_local uint8_t Thread::wait_event_ = 0;
thread_local Thread* Thread::current_thread_ = nullptr;
int64_t Thread::total_thread_count_ = 0;

Thread &Thread::current()
{
  assert(current_thread_ != nullptr);
  return *current_thread_;
}

Thread::Thread(Threads *threads, int64_t idx, int64_t stack_size, int32_t numa_node)
    : pth_(0),
      threads_(threads),
      idx_(idx),
#ifndef OB_USE_ASAN
      stack_addr_(nullptr),
#endif
      stack_size_(stack_size),
      stop_(true),
      join_concurrency_(0),
      pid_before_stop_(0),
      tid_before_stop_(0),
      tid_(0),
      thread_list_node_(this),
      cpu_time_(0),
      create_ret_(OB_NOT_RUNNING),
      numa_node_(numa_node)
{}

Thread::~Thread()
{
  destroy();
}

int Thread::start()
{
  int ret = OB_SUCCESS;
  const int64_t count = ATOMIC_FAA(&total_thread_count_, 1);
  ObNumaNodeGuard numa_guard(numa_node_);
  if (count >= get_max_thread_num() - OB_RESERVED_THREAD_NUM) {
    ret = OB_SIZE_OVERFLOW;
    LOG_ERROR("thread count reach limit", K(ret), "current count", count);
  } else if (stack_size_ <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid stack_size", K(ret), K(stack_size_));
#ifndef OB_USE_ASAN
  } else if (OB_ISNULL(stack_addr_ = g_stack_allocer.alloc(0 == GET_TENANT_ID() ? OB_SERVER_TENANT_ID : GET_TENANT_ID(), stack_size_ + SIG_STACK_SIZE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("alloc stack memory failed", K(stack_size_));
#endif
  } else {
    pthread_attr_t attr;
    bool need_destroy = false;
    int pret = pthread_attr_init(&attr);
    if (pret == 0) {
      need_destroy = true;
#ifndef OB_USE_ASAN
      // Set high priority QoS for new threads (platform-independent)
      // On macOS, daemon processes get low QoS priority by default, causing scheduling delays.
      int qos_ret = ob_pthread_attr_set_qos(&attr, ObThreadQoS::USER_INITIATED);
      if (qos_ret != 0) {
        LOG_WARN("ob_pthread_attr_set_qos failed", K(qos_ret));
        // Continue even if QoS setting failed
      }
#ifdef __APPLE__
      // On macOS, pthread_attr_setstack often fails with EINVAL if address/size
      // are not perfectly aligned or if the memory is already managed in a way
      // that pthread doesn't like. Use setstacksize instead and let the system
      // allocate the stack, while keeping our stack_addr_ for stack_header logic.
      pret = pthread_attr_setstacksize(&attr, stack_size_);
      if (pret != 0) {
        // Fallback to default if setstacksize fails
        pret = 0;
      } else {
        size_t actual_stack_size = 0;
        pthread_attr_getstacksize(&attr, &actual_stack_size);
        LOG_INFO("successfully set stack size", K_(stack_size), K(actual_stack_size));
      }
#else
      // ProtectedStackAllocator::alloc returns the top of the stack (high address)
      // but pthread_attr_setstack needs the bottom of the stack (low address)
      // Calculate the bottom address: stack_addr_ points to the top, so bottom = stack_addr_ - stack_size_
      pret = pthread_attr_setstack(&attr, stack_addr_, stack_size_);
#endif
#endif
    }
    if (pret == 0) {
      stop_ = false;
      pret = pthread_create(&pth_, &attr, __th_start, this);
      if (pret != 0) {
        LOG_ERROR("pthread create failed", K(pret), K(errno));
        pth_ = 0;
      } else {
        while (ATOMIC_LOAD(&create_ret_) == OB_NOT_RUNNING) {
          sched_yield();
        }
        if (OB_FAIL(create_ret_)) {
          LOG_ERROR("thread create failed", K(create_ret_));
        }
      }
    } else {
      int64_t total_size = stack_size_ + SIG_STACK_SIZE;
      LOG_ERROR("pthread_attr_setstack failed", K(pret), K(total_size), K_(stack_size), KP(stack_addr_));
    }
    if (0 != pret) {
      ret = OB_ERR_SYS;
      stop_ = true;
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
  bool stack_addr_flag = true;
#ifndef OB_USE_ASAN
  stack_addr_flag = (stack_addr_ != NULL);
#endif
#ifdef ERRSIM
  if (!stop_
      && stack_addr_flag
      && 0 != (OB_E(EventTable::EN_THREAD_HANG) 0)) {
    int tid_offset = 720;
    int tid = *(pid_t*)((char*)pth_ + tid_offset);
    LOG_WARN_RET(OB_SUCCESS, "stop was ignored", K(tid));
    return;
  }
#endif
#ifndef OB_USE_ASAN
  if (!stop_ && stack_addr_ != NULL) {
    int tid_offset = 720;
    int pid_offset = 724;
    int len = (char*)stack_addr_ + stack_size_ - (char*)pth_;
    if (len >= (max(tid_offset, pid_offset) + sizeof(pid_t))) {
      tid_before_stop_ = *(pid_t*)((char*)pth_ + tid_offset);
      pid_before_stop_ = *(pid_t*)((char*)pth_ + pid_offset);
    }
  }
#endif
  stop_ = true;
}

uint64_t Thread::get_tenant_id() const
{
  uint64_t tenant_id = OB_SERVER_TENANT_ID;
  IRunWrapper *run_wrapper_ = threads_->get_run_wrapper();
  if (OB_NOT_NULL(run_wrapper_)) {
    tenant_id = run_wrapper_->id();
  }
  return tenant_id;
}

void Thread::run()
{
  if (OB_NUMA_SHARED_INDEX != numa_node_) {
    AFFINITY_CTRL.thread_bind_to_node(numa_node_);
  }
  IRunWrapper *run_wrapper_ = threads_->get_run_wrapper();
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

void Thread::dump_pth() // for debug pthread join faileds
{
#ifndef OB_USE_ASAN
  int ret = OB_SUCCESS;
  int fd = 0;
  int64_t len = 0;
  ssize_t size = 0;
  char path[PATH_SIZE];
  len = (char*)stack_addr_ + stack_size_ - (char*)pth_;
#ifdef __APPLE__
  uint64_t thread_id = 0;
  pthread_threadid_np(NULL, &thread_id);
  snprintf(path, PATH_SIZE, "log/dump_pth.%p.%d", (char*)pth_, static_cast<pid_t>(thread_id));
#else
  snprintf(path, PATH_SIZE, "log/dump_pth.%p.%d", (char*)pth_, static_cast<pid_t>(syscall(__NR_gettid)));
#endif
  LOG_WARN("dump pth start", K(path), K(pth_), K(len), K(stack_addr_), K(stack_size_));
  if (NULL == (char*)pth_ || len >= stack_size_ || len <= 0) {
    LOG_WARN("invalid member", K(pth_), K(stack_addr_), K(stack_size_));
  } else if ((fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC,
                          S_IRUSR  | S_IWUSR | S_IRGRP)) < 0) {
    ret = OB_IO_ERROR;
    LOG_WARN("fail to create file", KERRMSG, K(ret));
  } else if (len != (size = write(fd, (char*)(pth_), len))) {
    ret = OB_IO_ERROR;
    LOG_WARN("dump pth fail", K(errno), KERRMSG, K(len), K(size), K(ret));
    if (0 != close(fd)) {
      LOG_WARN("fail to close file fd", K(fd), K(errno), KERRMSG, K(ret));
    }
  } else if (::fsync(fd) != 0) {
    ret = OB_IO_ERROR;
    LOG_WARN("sync pth fail", K(errno), KERRMSG, K(len), K(size), K(ret));
    if (0 != close(fd)) {
      LOG_WARN("fail to close file fd", K(fd), K(errno), KERRMSG, K(ret));
    }
  } else if (0 != close(fd)) {
    ret = OB_IO_ERROR;
    LOG_WARN("fail to close file fd", K(fd), KERRMSG, K(ret));
  } else {
    LOG_WARN("dump pth done", K(path), K(pth_), K(size));
  }
#endif
}

void Thread::wait()
{
  int ret = 0;
  if (pth_ != 0) {
    if (0 != (ret = pthread_join(pth_, nullptr))) {
      LOG_ERROR("pthread_join failed", K(ret), K(errno));
#ifndef OB_USE_ASAN
      dump_pth();
      ob_abort();
#endif
    }
    destroy_stack();
  }
}

int Thread::try_wait()
{
  int ret = OB_SUCCESS;
  if (pth_ != 0) {
    int pret = 0;
#ifdef __linux__
    if (0 != (pret = pthread_tryjoin_np(pth_, nullptr))) {
      ret = OB_EAGAIN;
      LOG_WARN("pthread_tryjoin_np failed", K(pret), K(errno), K(ret), K(oceanbase::lib::Thread::tid_));
    } else {
      destroy_stack();
    }
#elif defined(__APPLE__)
    // macOS doesn't support pthread_tryjoin_np, use pthread_kill to check if thread is alive
    if (pthread_kill(pth_, 0) == 0) {
      // Thread is still alive
      ret = OB_EAGAIN;
    } else {
      // Thread has terminated, use pthread_join to clean up
      if (0 != (pret = pthread_join(pth_, nullptr))) {
        ret = OB_EAGAIN;
        LOG_WARN("pthread_join failed", K(pret), K(errno), K(ret), K(oceanbase::lib::Thread::tid_));
      } else {
        destroy_stack();
      }
    }
#endif
  }
  return ret;
}

void Thread::destroy()
{
  if (pth_ != 0) {
    /* NOTE: must wait pthread quit before release user_stack
       because the pthread's tcb was allocated from it */
    wait();
  } else {
    destroy_stack();
  }
}

void Thread::destroy_stack()
{
#ifndef OB_USE_ASAN
  if (stack_addr_ != nullptr) {
    g_stack_allocer.dealloc(stack_addr_);
    stack_addr_ = nullptr;
    pth_ = 0;
  }
#else
  pth_ = 0;
#endif
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
  ob_set_thread_tenant_id(th->get_tenant_id());
  current_thread_ = th;
  th->tid_ = gettid();

#ifndef OB_USE_ASAN
  ObStackHeader *stack_header = ProtectedStackAllocator::stack_header(th->stack_addr_);
  abort_unless(stack_header->check_magic());

  #ifndef OB_USE_ASAN
  /**
    signal handler stack
   */
  #ifndef __APPLE__
  // On macOS, sigaltstack may fail with ENOMEM (errno=12) due to system limitations
  // Skip sigaltstack setup on macOS to avoid warnings
  stack_t nss;
  stack_t oss;
  bzero(&nss, sizeof(nss));
  bzero(&oss, sizeof(oss));
  nss.ss_sp = &((char*)th->stack_addr_)[th->stack_size_];
  nss.ss_size = SIG_STACK_SIZE;
  bool restore_sigstack = false;
  if (-1 == sigaltstack(&nss, &oss)) {
    LOG_WARN_RET(OB_ERR_SYS, "sigaltstack failed, ignore it", K(errno));
  } else {
    restore_sigstack = true;
  }
  DEFER(if (restore_sigstack) { sigaltstack(&oss, nullptr); });
  #endif // __APPLE__
  #endif

  stack_header->pth_ = (uint64_t)pthread_self();
#endif

  int ret = OB_SUCCESS;
  if (OB_ISNULL(th)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(th), K(ret));
  } else {
    // pm destructor logically accesses objects bound to other pthread_key, to avoid the impact of destruction order
    // pm does not use TSI but instead does thread-local (__thread)
    // create page manager
    ObPageManager pm;
    ret = pm.set_tenant_ctx(common::OB_SERVER_TENANT_ID, common::ObCtxIds::GLIBC);
    if (OB_FAIL(ret)) {
      LOG_ERROR("set tenant ctx failed", K(ret));
    } else {
      ObPageManager::set_thread_local_instance(pm);
      MemoryContext *mem_context = GET_TSI0(MemoryContext);
      if (OB_ISNULL(mem_context)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("null ptr", K(ret));
      } else if (OB_FAIL(ROOT_CONTEXT->CREATE_CONTEXT(*mem_context,
                         ContextParam().set_properties(RETURN_MALLOC_DEFAULT)
                                       .set_label("ThreadRoot")))) {
        LOG_ERROR("create memory context failed", K(ret));
      } else {
        WITH_CONTEXT(*mem_context) {
          try {
            in_try_stmt = true;
#ifndef __APPLE__
            // On Linux, signal thread started here. On macOS, this was already done
            // earlier (right after QoS setup) to prevent spin-wait delays.
            ATOMIC_STORE(&th->create_ret_, OB_SUCCESS);
#endif
            th->run();
            in_try_stmt = false;
          } catch (OB_BASE_EXCEPTION &except) {
            // we don't catch other exception because we don't know how to handle it
            _LOG_ERROR("Exception caught!!! errno = %d, exception info = %s", except.get_errno(), except.what());
            ret = OB_ERR_UNEXPECTED;
            in_try_stmt = false;
            if (1 == th->threads_->get_thread_count() && !th->has_set_stop()) {
              LOG_WARN("thread exit by itself without set_stop", K(ret));
              th->threads_->stop();
            }
          }
        }
      }
      if (mem_context != nullptr && *mem_context != nullptr) {
        DESTROY_CONTEXT(*mem_context);
      }
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

#ifdef __APPLE__
  // macOS doesn't have /proc, use mach APIs
  thread_port_t mach_thread = pthread_mach_thread_np(pth_);
  thread_basic_info_data_t basic_info;
  mach_msg_type_number_t count = THREAD_BASIC_INFO_COUNT;
  if (KERN_SUCCESS != thread_info(mach_thread, THREAD_BASIC_INFO, (thread_info_t)&basic_info, &count)) {
    ret = OB_ERR_SYS;
    LOG_WARN("thread_info failed", K(ret), K(tid));
  } else {
    cpu_time = (int64_t)basic_info.user_time.seconds * 1000000 + basic_info.user_time.microseconds
             + (int64_t)basic_info.system_time.seconds * 1000000 + basic_info.system_time.microseconds;
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
    snprintf(stat_path, PATH_BUFSIZE, "/proc/%d/task/%ld/stat", pid, tid);
    if ((fd = ::open(stat_path, O_RDONLY)) < 0) {
      ret = OB_IO_ERROR;
      LOG_WARN("open file error", K((const char *)stat_path), K(errno), KERRMSG, K(ret));
    } else if ((read_size = read(fd, stat_content, MAX_LINE_LENGTH)) < 0) {
      ret = OB_IO_ERROR;
      LOG_WARN("read file error",
          K((const char *)stat_path),
          K((const char *)stat_content),
          K(ret),
          K(errno),
          KERRMSG,
          K(ret));
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
int __attribute__((weak)) get_max_thread_num()
{
  return 4096;
}
}
}
