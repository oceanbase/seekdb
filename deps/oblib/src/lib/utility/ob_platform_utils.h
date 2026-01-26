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

#ifndef OCEANBASE_LIB_UTILITY_OB_PLATFORM_UTILS_H_
#define OCEANBASE_LIB_UTILITY_OB_PLATFORM_UTILS_H_

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <pthread.h>
#include <cstddef>
#include <cstring>

#ifdef __APPLE__
#include <sys/sysctl.h>
// Note: <mach/mach.h> is NOT included here to avoid macro conflicts (e.g., USEC_PER_SEC).
// Functions requiring mach APIs are implemented in ob_platform_utils.cpp
#include <pthread/qos.h>
#include <sys/resource.h>
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#endif

// ============================================================================
// Platform Detection Macros
// ============================================================================
#if defined(__APPLE__)
#define OB_PLATFORM_MACOS 1
#elif defined(__linux__)
#define OB_PLATFORM_LINUX 1
#elif defined(_WIN32)
#define OB_PLATFORM_WINDOWS 1
#endif

// ============================================================================
// Section 1: GNU Extension Functions
// ============================================================================

// memrchr is a GNU extension, not available on macOS
#ifndef __linux__
inline void *memrchr(const void *s, int c, size_t n)
{
  if (n == 0) return nullptr;
  const unsigned char *p = static_cast<const unsigned char *>(s) + n - 1;
  for (size_t i = 0; i < n; ++i, --p) {
    if (*p == static_cast<unsigned char>(c)) {
      return const_cast<void *>(static_cast<const void *>(p));
    }
  }
  return nullptr;
}
#endif

// ============================================================================
// Section 2: Pthread Spinlock Compatibility
// macOS doesn't support pthread_spinlock_t, use pthread_mutex_t as replacement
// ============================================================================

#ifdef __APPLE__
typedef pthread_mutex_t ob_pthread_spinlock_t;

inline int ob_pthread_spin_init(ob_pthread_spinlock_t *lock, int pshared)
{
  (void)pshared;
  return pthread_mutex_init(lock, NULL);
}

inline int ob_pthread_spin_lock(ob_pthread_spinlock_t *lock)
{
  return pthread_mutex_lock(lock);
}

inline int ob_pthread_spin_unlock(ob_pthread_spinlock_t *lock)
{
  return pthread_mutex_unlock(lock);
}

inline int ob_pthread_spin_destroy(ob_pthread_spinlock_t *lock)
{
  return pthread_mutex_destroy(lock);
}

// For backward compatibility with code that uses pthread_spinlock_t directly
#ifndef OB_PLATFORM_SPINLOCK_DEFINED
#define OB_PLATFORM_SPINLOCK_DEFINED
typedef pthread_mutex_t pthread_spinlock_t;
#define pthread_spin_init(lock, pshared) pthread_mutex_init(lock, NULL)
#define pthread_spin_lock(lock) pthread_mutex_lock(lock)
#define pthread_spin_unlock(lock) pthread_mutex_unlock(lock)
#define pthread_spin_destroy(lock) pthread_mutex_destroy(lock)
#ifndef PTHREAD_PROCESS_PRIVATE
#define PTHREAD_PROCESS_PRIVATE 0
#endif
#endif

#else // __linux__

typedef pthread_spinlock_t ob_pthread_spinlock_t;
#define ob_pthread_spin_init pthread_spin_init
#define ob_pthread_spin_lock pthread_spin_lock
#define ob_pthread_spin_unlock pthread_spin_unlock
#define ob_pthread_spin_destroy pthread_spin_destroy

#endif // __APPLE__

// ============================================================================
// Section 3: File Stat Compatibility
// macOS doesn't have stat64/fstat64/lstat64, use stat/fstat/lstat instead
// ============================================================================

#ifdef __APPLE__
#define ob_stat64 stat
#define ob_fstat64 fstat
#define ob_lstat64 lstat
typedef struct stat ob_stat64_t;
#else
#define ob_stat64 stat64
#define ob_fstat64 fstat64
#define ob_lstat64 lstat64
typedef struct stat64 ob_stat64_t;
#endif

// ============================================================================
// Section 4: strerror_r Compatibility
// macOS strerror_r returns int (0 on success, -1 on error) and writes to buffer
// Linux strerror_r returns char* (pointer to the error string)
// We provide a unified version that always returns const char*
// ============================================================================

inline const char* ob_strerror_r(int errnum, char* buf, size_t buflen)
{
#ifdef __APPLE__
  // macOS: strerror_r returns int (0 on success, -1 on error)
  return (::strerror_r(errnum, buf, buflen) == 0) ? buf : "Unknown error";
#else
  // Linux: strerror_r returns char*
  return ::strerror_r(errnum, buf, buflen);
#endif
}

// Override native strerror_r with our compatible version
#define strerror_r ob_strerror_r

// ============================================================================
// Section 5: Socket Flags Compatibility
// macOS doesn't support SOCK_CLOEXEC and SOCK_NONBLOCK in socket()
// ============================================================================

#ifdef __APPLE__
// Define placeholder values (they won't be used in socket() on macOS)
#ifndef SOCK_CLOEXEC
#define SOCK_CLOEXEC 0
#endif
#ifndef SOCK_NONBLOCK
#define SOCK_NONBLOCK 0
#endif
#endif

namespace oceanbase
{
namespace lib
{

// Create socket with CLOEXEC and NONBLOCK flags (platform-independent)
inline int ob_socket_cloexec_nonblock(int domain, int type, int protocol)
{
#ifdef __APPLE__
  int fd = socket(domain, type, protocol);
  if (fd >= 0) {
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
  }
  return fd;
#else
  return socket(domain, type | SOCK_CLOEXEC | SOCK_NONBLOCK, protocol);
#endif
}

// Create socket with only CLOEXEC flag (platform-independent)
inline int ob_socket_cloexec(int domain, int type, int protocol)
{
#ifdef __APPLE__
  int fd = socket(domain, type, protocol);
  if (fd >= 0) {
    fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC);
  }
  return fd;
#else
  return socket(domain, type | SOCK_CLOEXEC, protocol);
#endif
}

// ============================================================================
// Section 5: Thread Name Operations
// ============================================================================

// Get current thread name (platform-independent)
inline int ob_get_thread_name(char *name, size_t len)
{
  if (name == nullptr || len == 0) {
    return -1;
  }
#ifdef __APPLE__
  return pthread_getname_np(pthread_self(), name, len);
#else
  return prctl(PR_GET_NAME, name);
#endif
}

// Set current thread name (platform-independent)
inline int ob_set_thread_name(const char *name)
{
  if (name == nullptr) {
    return -1;
  }
#ifdef __APPLE__
  return pthread_setname_np(name);
#else
  return prctl(PR_SET_NAME, name);
#endif
}

// ============================================================================
// Section 6: Memory Information
// ============================================================================

// Get system page size
inline ssize_t ob_get_page_size()
{
#ifdef __APPLE__
  static ssize_t ps = sysconf(_SC_PAGESIZE);
#else
  static ssize_t ps = getpagesize();
#endif
  return ps;
}

// Get total physical memory (in bytes)
inline int64_t ob_get_total_memory()
{
  return sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
}

// Get available physical memory (in bytes)
// Returns -1 if unable to determine
// Note: On macOS this function requires mach APIs, implemented in ob_platform_utils.cpp
int64_t ob_get_available_memory();

// ============================================================================
// Section 7: Thread Priority / QoS
// ============================================================================

// Thread QoS levels (abstract, platform-independent)
enum class ObThreadQoS
{
  USER_INTERACTIVE,  // Highest priority - UI updates
  USER_INITIATED,    // High priority - user-initiated tasks
  DEFAULT,           // Normal priority
  UTILITY,           // Low priority - long-running tasks
  BACKGROUND         // Lowest priority - background maintenance
};

// Set current thread's QoS/priority (platform-independent)
inline int ob_set_thread_qos(ObThreadQoS qos)
{
#ifdef __APPLE__
  qos_class_t qos_class;
  switch (qos) {
    case ObThreadQoS::USER_INTERACTIVE:
      qos_class = QOS_CLASS_USER_INTERACTIVE;
      break;
    case ObThreadQoS::USER_INITIATED:
      qos_class = QOS_CLASS_USER_INITIATED;
      break;
    case ObThreadQoS::DEFAULT:
      qos_class = QOS_CLASS_DEFAULT;
      break;
    case ObThreadQoS::UTILITY:
      qos_class = QOS_CLASS_UTILITY;
      break;
    case ObThreadQoS::BACKGROUND:
      qos_class = QOS_CLASS_BACKGROUND;
      break;
    default:
      qos_class = QOS_CLASS_DEFAULT;
      break;
  }
  return pthread_set_qos_class_self_np(qos_class, 0);
#else
  // On Linux, we could use nice() or pthread scheduling, but for now just return success
  (void)qos;
  return 0;
#endif
}

// Set pthread attribute QoS for new thread creation
inline int ob_pthread_attr_set_qos(pthread_attr_t *attr, ObThreadQoS qos)
{
#ifdef __APPLE__
  qos_class_t qos_class;
  switch (qos) {
    case ObThreadQoS::USER_INTERACTIVE:
      qos_class = QOS_CLASS_USER_INTERACTIVE;
      break;
    case ObThreadQoS::USER_INITIATED:
      qos_class = QOS_CLASS_USER_INITIATED;
      break;
    case ObThreadQoS::DEFAULT:
      qos_class = QOS_CLASS_DEFAULT;
      break;
    case ObThreadQoS::UTILITY:
      qos_class = QOS_CLASS_UTILITY;
      break;
    case ObThreadQoS::BACKGROUND:
      qos_class = QOS_CLASS_BACKGROUND;
      break;
    default:
      qos_class = QOS_CLASS_DEFAULT;
      break;
  }
  return pthread_attr_set_qos_class_np(attr, qos_class, 0);
#else
  (void)attr;
  (void)qos;
  return 0;
#endif
}

// ============================================================================
// Section 8: Process Information
// ============================================================================

#ifdef __linux__
inline int ob_get_process_id()
{
  return getpid();
}
inline int ob_get_thread_id()
{
  return static_cast<int>(syscall(__NR_gettid));
}
#else
inline int ob_get_process_id()
{
  return getpid();
}
inline int ob_get_thread_id()
{
  uint64_t tid;
  pthread_threadid_np(NULL, &tid);
  return static_cast<int>(tid);
}
#endif

} // namespace lib
} // namespace oceanbase

#endif // OCEANBASE_LIB_UTILITY_OB_PLATFORM_UTILS_H_
