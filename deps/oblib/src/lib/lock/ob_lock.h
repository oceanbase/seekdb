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

#ifndef OB_LOCK_H
#define OB_LOCK_H
#include <assert.h>
namespace obutil
{
template <typename T>
class ObLockT
{
public:

  explicit ObLockT(const T& mutex) :
    mutex_(mutex)
  {
    mutex_.lock();
    acquired_ = true;
  }

  ~ObLockT()
  {
    if (acquired_)
    {
      mutex_.unlock();
    }
  }

  void acquire() const
  {
    if (acquired_)
    {
#ifdef _NO_EXCEPTION
       assert(!"ThreadLockedException");
#else
       throw ThreadLockedException(__FILE__, __LINE__);
#endif
    }
    mutex_.lock();
    acquired_ = true;
  }


  bool try_acquire() const
  {
    if (acquired_)
    {
#ifdef _NO_EXCEPTION
      assert(!"ThreadLockedException");
#else
      throw ThreadLockedException(__FILE__, __LINE__);
#endif
    }
    acquired_ = mutex_.trylock();
    return acquired_;
  }

  void release() const
  {
    if (!acquired_)
    {
#ifdef _NO_EXCEPTION
      assert(!"ThreadLockedException");
#else
      throw ThreadLockedException(__FILE__, __LINE__);
#endif
    }
    mutex_.unlock();
    acquired_ = false;
  }

  bool acquired() const
  {
    return acquired_;
  }

protected:

  ObLockT(const T& mutex, bool) :
    mutex_(mutex)
  {
    acquired_ = mutex_.trylock();
  }

private:

  ObLockT(const ObLockT&);
  ObLockT& operator=(const ObLockT&);

  const T& mutex_;
  mutable bool acquired_;

  friend class Cond;
};

template <typename T>
class ObTryLockT : public ObLockT<T>
{
public:

  ObTryLockT(const T& mutex) :
    ObLockT<T>(mutex, true)
  {}
};
}

#endif
