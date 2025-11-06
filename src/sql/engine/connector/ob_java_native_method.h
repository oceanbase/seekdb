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

#ifndef OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JAVA_NATIVE_METHOD_H_
#define OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JAVA_NATIVE_METHOD_H_

#include <jni.h>
#include "lib/alloc/alloc_struct.h"
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{

namespace sql
{

// Without the Java UDF then only support mem malloc and free api
struct JavaNativeMethods {
private:
  static common::ObMemAttr mem_attr_;
  static common::ObMalloc malloc_;

public:
  static jlong memory_malloc(JNIEnv *env, jclass clazz, jlong bytes);
  static void memory_free(JNIEnv *env, jclass clazz, jlong address);
};

} // namespace sql
} // namespace oceanbase

#endif /* OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JAVA_NATIVE_METHOD_H_ */
