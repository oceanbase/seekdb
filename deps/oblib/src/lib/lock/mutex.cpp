#include "lib/lock/mutex.h"
#include "lib/oblog/ob_log.h"
#include "lib/ob_abort.h"
using namespace oceanbase::common;
namespace obutil
{
ObUtilMutex::ObUtilMutex()
{
  const int rt = pthread_mutex_init(&_mutex, NULL);
#ifdef _WIN32
  // On Windows (pthreads4w), pthread_mutex_t is an opaque pointer and init
  // may fail with ENOSPC (errno=28) when the kernel object (CreateEvent)
  // limit is reached. Leaving _mutex uninitialized and continuing would
  // cause later lock/unlock to dereference NULL inside pthreadVC3.dll and
  // produce an unrecoverable AccessViolation. Fast-fail instead so the
  // failure is visible and does not cascade into thread-specific native
  // crashes that corrupt the whole process.
  if (rt != 0) {
    _OB_LOG_RET(ERROR, OB_ERR_SYS, "%s. rt:%d", "ThreadSyscallException", rt);
    ob_abort();
  }
#elif defined(_NO_EXCEPTION)
  if ( rt != 0 ) {
    _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt:%d", "ThreadSyscallException", rt);
  }
  assert( rt == 0 );
#else
  if ( rt != 0 ) {
    throw ThreadSyscallException(__FILE__, __LINE__, rt);
  }
#endif
}

ObUtilMutex::~ObUtilMutex()
{
  const int rt = pthread_mutex_destroy(&_mutex);
#ifndef __APPLE__
  if ( rt != 0 ) {
    _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt: %d", "ThreadSyscallException", rt);
  }
  assert(rt == 0);
#endif
}

bool ObUtilMutex::trylock() const
{
  const int rt = pthread_mutex_trylock(&_mutex);
#ifdef _NO_EXCEPTION
  if ( rt != 0 && rt !=EBUSY ) {
    if ( rt == EDEADLK ) {
      _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt: %d", "ThreadLockedException ", rt);
    } else {
      _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt: %d", "ThreadSyscallException", rt);
    }
    return false;
  }
#else
  if(rt != 0 && rt != EBUSY) {
    if(rt == EDEADLK) {
      throw ThreadLockedException(__FILE__, __LINE__);
    } else {
      throw ThreadSyscallException(__FILE__, __LINE__, rt);
    }
  }
#endif
  return (rt == 0);
}

void ObUtilMutex::lock() const
{
  const int rt = pthread_mutex_lock(&_mutex);
#ifdef _NO_EXCEPTION
  assert( rt == 0 );
  if ( rt != 0 ) {
    if ( rt == EDEADLK ) {
      _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt: %d", "ThreadLockedException ", rt);
    } else {
      _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt: %d", "ThreadSyscallException", rt);
    }
  }
#else
  if( rt != 0 ) {
    if(rt == EDEADLK) {
      throw ThreadLockedException(__FILE__, __LINE__);
    } else {
      throw ThreadSyscallException(__FILE__, __LINE__, rt);
    }
  }
#endif
}

void ObUtilMutex::lock(LockState&) const
{
}

void ObUtilMutex::unlock() const
{
  const int rt = pthread_mutex_unlock(&_mutex);
#ifdef _NO_EXCEPTION
  if ( rt != 0 ) {
    _OB_LOG_RET(ERROR,OB_ERR_SYS, "%s. rt: %d","ThreadSyscallException", rt);
  }
  assert( rt == 0 );
#else
  if ( rt != 0 ) {
    throw ThreadSyscallException(__FILE__, __LINE__, rt);
  }
#endif
}

void ObUtilMutex::unlock(LockState& state) const
{
  state.mutex = &_mutex;
}

bool ObUtilMutex::will_unlock() const
{
  return true;
}
}//end namespace obutil
