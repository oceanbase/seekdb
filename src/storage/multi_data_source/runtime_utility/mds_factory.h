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

#ifndef SHARE_STORAGE_MULTI_DATA_SOURCE_UTILITY_MDS_FACTORY_H
#define SHARE_STORAGE_MULTI_DATA_SOURCE_UTILITY_MDS_FACTORY_H
#include "lib/allocator/ob_malloc.h"
#include <type_traits>
#include <typeinfo>
#include "lib/atomic/ob_atomic.h"
#include "ob_tablet_id.h"
#include "share/ob_ls_id.h"
#include "src/share/ob_errno.h"
#include "common/meta_programming/ob_type_traits.h"
#include "mds_tenant_service.h"
#include "common_define.h"

namespace oceanbase
{
namespace transaction
{
enum class ObTxDataSourceType : int64_t;
class ObTransID;
}
namespace storage
{
namespace mds
{

struct MdsFactory
{
  // If type T has an init function, then first construct it using the default constructor, and then call its init function
  template <typename T, typename ...Args, typename std::enable_if<!std::is_base_of<BufferCtx, T>::value, bool>::type = true>
  static int create(T *&p_obj, Args &&...args)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(create_(p_obj, std::forward<Args>(args)...))) {
      MDS_LOG(WARN, "fail to create object", KR(ret), K(typeid(T).name()), KP(p_obj), K(lbt()));
    }
    return ret;
  }
  template <typename T>
  static void destroy(T *obj)
  {
    MDS_LOG(DEBUG, "destroy object", K(typeid(T).name()), KP(obj), K(lbt()));
    if (OB_NOT_NULL(obj)) {
      obj->~T();
      MdsAllocator::get_instance().free(obj);
    }
  }
  // According to the information encoded when constructing the object for runtime reflection
  static int deep_copy_buffer_ctx(const transaction::ObTransID &trans_id,
                                  const BufferCtx &old_ctx,
                                  BufferCtx *&new_ctx,
                                  ObIAllocator &allocator = MTL(ObTenantMdsService*)->get_buffer_ctx_allocator(),
                                  const char *alloc_file = __builtin_FILE(),
                                  const char *alloc_func = __builtin_FUNCTION(),
                                  const int64_t line = __builtin_LINE());
  static int create_buffer_ctx(const transaction::ObTxDataSourceType &data_source_type,
                               const transaction::ObTransID &trans_id,
                               BufferCtx *&buffer_ctx,
                               ObIAllocator &allocator = MTL(ObTenantMdsService*)->get_buffer_ctx_allocator(),
                               const char *alloc_file = __builtin_FILE(),
                               const char *alloc_func = __builtin_FUNCTION(),
                               const int64_t line = __builtin_LINE());
private:
  // If type T has an init function, then first construct it using the default constructor, and then call its init function
  template <typename T, typename ...Args, ENABLE_IF_HAS(T, init, int(Args...))>
  static int create_(T *&p_obj, Args &&...args)
  {
    int ret = common::OB_SUCCESS;
    T *temp_obj = (T *)MdsAllocator::get_instance().alloc(sizeof(T));
    if (OB_ISNULL(temp_obj)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      MDS_LOG(WARN, "alloc memory failed", KR(ret));
    } else {
      new (temp_obj)T();
      if (OB_FAIL(temp_obj->init(std::forward<Args>(args)...))) {
        MDS_LOG(WARN, "init obj failed", KR(ret), K(typeid(T).name()));
        temp_obj->~T();
        MdsAllocator::get_instance().free(temp_obj);
      } else {
        p_obj = temp_obj;
      }
    }
    MDS_LOG(DEBUG, "create object with init", K(typeid(T).name()), KP(p_obj), K(lbt()));
    return ret;
  }
  // If type T does not have an init function, then construct through the constructor
  template <typename T, typename ...Args, ENABLE_IF_NOT_HAS(T, init, int(Args...))>
  static int create_(T *&p_obj, Args &&...args)
  {
    int ret = common::OB_SUCCESS;
    T *temp_obj = (T *)MdsAllocator::get_instance().alloc(sizeof(T));
    if (OB_ISNULL(temp_obj)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      MDS_LOG(WARN, "alloc memory failed", KR(ret));
    } else {
      new (temp_obj)T(std::forward<Args>(args)...);
      p_obj = temp_obj;
    }
    MDS_LOG(DEBUG, "create object with construction", K(typeid(T).name()), KP(p_obj), K(lbt()));
    return ret;
  }
};

}
}
}
#endif
