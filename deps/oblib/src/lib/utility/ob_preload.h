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

#ifndef _OCEABASE_LIB_UTILITY_OB_PRELOAD_H_
#define _OCEABASE_LIB_UTILITY_OB_PRELOAD_H_

#define _GNU_SOURCE 1
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <execinfo.h>

#ifdef __ENABLE_PRELOAD__
inline int pthread_key_create(pthread_key_t *key, void (*destructor)(void *))
{
  int (*real_func)(pthread_key_t *key,
                   void (*destructor)(void *)) = (typeof(real_func))dlsym(RTLD_NEXT, "pthread_key_create");
  return real_func(key, destructor);
}

inline int pthread_key_delete(pthread_key_t key)
{
  int (*real_func)(pthread_key_t key) = (typeof(real_func))dlsym(RTLD_NEXT, "pthread_key_delete");
  return real_func(key);
}
#endif /* __ENABLE_PRELOAD__ */

#endif /* _OCEABASE_LIB_UTILITY_OB_PRELOAD_H_ */
