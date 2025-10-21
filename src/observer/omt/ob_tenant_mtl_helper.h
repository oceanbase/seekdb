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

#ifndef OCEANBASE_SHARE_OB_TENANT_MTL_HELPER_H_
#define OCEANBASE_SHARE_OB_TENANT_MTL_HELPER_H_

#include "lib/allocator/ob_malloc.h"
#include "lib/ob_errno.h"
#include "share/rc/ob_tenant_base.h"

template<class T>
typename std::enable_if<std::is_pointer<T>::value, int>::type mtl_new_default(T &m)
{
  int ret = oceanbase::common::OB_SUCCESS;
  oceanbase::ObMemAttr attr(oceanbase::share::ObTenantEnv::get_tenant()->id(), oceanbase::ObModIds::OMT_TENANT);
  void *buf = oceanbase::ob_malloc(sizeof(typename std::remove_pointer<T>::type), attr);
  if (OB_ISNULL(buf)) {
    ret = oceanbase::common::OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(m = new(buf) typename std::remove_pointer<T>::type)) {
    ret = oceanbase::common::OB_ERR_UNEXPECTED;
  }
  return ret;
}

template<class T>
typename std::enable_if<!std::is_pointer<T>::value, int>::type mtl_new_default(T &m)
{
  return oceanbase::common::OB_SUCCESS;
}

template<typename T>
typename std::enable_if<std::is_pointer<T>::value, int>::type mtl_init_default(T &m)
{
  return m->init();
}

template<typename T>
typename std::enable_if<!std::is_pointer<T>::value, int>::type mtl_init_default(T &m)
{
  return m.init();
}

template<typename T>
typename std::enable_if<std::is_pointer<T>::value, int>::type mtl_start_default(T &m)
{
  return m->start();
}

template<typename T>
typename std::enable_if<!std::is_pointer<T>::value, int>::type mtl_start_default(T &m)
{
  return m.start();
}

template<typename T>
typename std::enable_if<std::is_pointer<T>::value>::type mtl_stop_default(T &m)
{
  if (m != nullptr) {
    m->stop();
  }
}

template<typename T>
typename std::enable_if<!std::is_pointer<T>::value>::type mtl_stop_default(T &m)
{
  m.stop();
}

template<typename T>
typename std::enable_if<std::is_pointer<T>::value>::type mtl_wait_default(T &m)
{
  if (m != nullptr) {
    m->wait();
  }
}

template<typename T>
typename std::enable_if<!std::is_pointer<T>::value>::type mtl_wait_default(T &m)
{
  m.wait();
}

template<typename T>
typename std::enable_if<std::is_pointer<T>::value>::type mtl_destroy_default(T &m)
{
  if (m != nullptr) {
    m->destroy();
    oceanbase::common::ob_delete(m);
    m = nullptr;
  }
}

template<typename T>
typename std::enable_if<!std::is_pointer<T>::value>::type mtl_destroy_default(T &m)
{
  m.destroy();
}

template<typename T>
typename std::enable_if<std::is_pointer<T>::value, bool>::type get_mtl_ptr(T &m, void *&ptr)
{
  ptr = reinterpret_cast<void*>(m);
  return true;
}
template<typename T>
typename std::enable_if<!std::is_pointer<T>::value, bool>::type get_mtl_ptr(T &m, void *&ptr)
{
  ptr = reinterpret_cast<void*>(&m);
  return false;
}

#endif
