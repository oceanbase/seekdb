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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_java_native_method.h"
#include "lib/oblog/ob_log.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{

namespace sql
{

jlong JavaNativeMethods::memory_malloc(JNIEnv *env, jclass clazz, jlong bytes) {
  int ret = OB_SUCCESS;
  int64_t lbytes = reinterpret_cast<int64_t>(bytes);
  oceanbase::lib::ObMallocHookAttrGuard guard(ObMemAttr(MTL_ID(), "JniAllocator"));
  long address = reinterpret_cast<long>(malloc(lbytes));
  LOG_TRACE("allocate bytes of memory address", K(ret), K(lbytes), K(address));
  return address;
}

void JavaNativeMethods::memory_free(JNIEnv *env, jclass clazz, jlong address) {
  int ret = OB_SUCCESS;
  free(reinterpret_cast<void *>(address));
  LOG_TRACE("free memory address", K(ret), K(address));
}

} // namespace sql
} // namespace oceanbase
