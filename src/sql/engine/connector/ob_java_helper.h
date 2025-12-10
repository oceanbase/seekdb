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

#ifndef OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JAVA_HELPER_H_
#define OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JAVA_HELPER_H_

#include <utility>
#include <string>
#include <vector>

#include <jni.h>

#include "lib/oblog/ob_log_module.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/page_arena.h"
#include "common/ob_common_utility.h"
#include "ob_java_native_method.h"
#include "lib/lock/ob_mutex.h"

// Copy from `close_modules/dblink/deps/oblib/src/lib/oracleclient/ob_dlsym_loader.h`
#if defined(_AIX)
  #define  LIB_OPEN_FLAGS        (RTLD_NOW | RTLD_GLOBAL | RTLD_MEMBER)
#elif defined(__hpux)
  #define  LIB_OPEN_FLAGS        (BIND_DEFERRED |BIND_VERBOSE| DYNAMIC_PATH)
#elif defined(__GNUC__)
  #define  LIB_OPEN_FLAGS        (RTLD_NOW | RTLD_GLOBAL)
#endif

#if defined(_WINDOWS)

  #include <Windows.h>

  #define LIB_HANDLE               HMODULE
  #define LIB_OPEN(l)              LoadLibraryA(l)
  #define LIB_CLOSE                FreeLibrary
  #define LIB_SYMBOL(h, s, p, t)   p = (t) GetProcAddress(h, s)

#elif defined(__hpux)

  #include <dl.h>

  #define LIB_HANDLE               shl_t
  #define LIB_OPEN(l)              shl_load(l, LIB_OPEN_FLAGS, 0L)
  #define LIB_CLOSE                shl_unload
  #define LIB_SYMBOL(h, s, p, t)   shl_findsym(&h, s, (short) TYPE_PROCEDURE, (void *) &p)

#elif defined(__GNUC__)

  #include <dlfcn.h>

  #define LIB_HANDLE               void *
  #define LIB_OPEN(l)              dlopen(l, LIB_OPEN_FLAGS)
  #define LIB_CLOSE                dlclose
  #define LIB_SYMBOL(h, s, p, t)   p = (t) dlsym(h, s)

#else

  #error Unable to compute how to dynamic libraries

#endif

/**
 * Length of buffer for retrieving created JVMs.  (We only ever create one.)
 */
#define VM_BUF_LENGTH 1

// define function
typedef JNIEnv* (*GETJNIENV)(void);
typedef jint (*DETACHCURRENTTHREAD)(void);
typedef jint (*DESTROYJNIENV)(void);

// desclare function
extern "C" GETJNIENV getJNIEnv;
extern "C" DETACHCURRENTTHREAD detachCurrentThread;
extern "C" DESTROYJNIENV destroyJNIEnv;

namespace oceanbase
{

namespace sql
{

class JVMClass;

typedef lib::ObLockGuard<lib::ObMutex> LockGuard;

struct ObJavaEnvContext {
public:
  bool jvm_loaded_;  /* Java correctly loaded ? */
  uint64_t mem_bytes_jvm_;  /* allocated bytes by OCI client */
  uint64_t jvm_alloc_times_;
  uint64_t jvm_realloc_times_;
  uint64_t jvm_free_times_;
  int64_t jvm_created_time_;  /* get from current_time() of jvm */
  int64_t referece_times_; /* check refrence times of jni env */

public:
  void reset() {
    jvm_loaded_ = false;
    mem_bytes_jvm_ = 0;
    jvm_alloc_times_ = 0;
    jvm_realloc_times_ = 0;
    jvm_free_times_ = 0;
    jvm_created_time_ = 0;
    referece_times_ = 0;
  }

  ObJavaEnvContext() {
    reset();
  }

  bool is_valid() {
    return jvm_loaded_;
  }

  TO_STRING_KV(
    K(jvm_loaded_),
    K(mem_bytes_jvm_),
    K(jvm_alloc_times_),
    K(jvm_realloc_times_),
    K(jvm_free_times_),
    K(jvm_created_time_),
    K(referece_times_)
  );
};

// Restrictions on use:
// can only be used in pthread, not in bthread
// thread local helper
class JVMFunctionHelper {
public:
  static JVMFunctionHelper &getInstance();
  JVMFunctionHelper(const JVMFunctionHelper &) = delete;
  JVMFunctionHelper& operator=(const JVMFunctionHelper&) = delete;

  // Sometimes observer don't need the jni env, so will use a mock jvm symbol
  // for compiling. But may execute jni methods, so we need to check and stop it 
  // before really executing.
  int check_valid_env();
  // Set a api to init or reinit jni env
  int init_jni_env();

  int get_env(JNIEnv *&env);
  int inc_ref();
  int cur_ref();
  int dec_ref();

  // detach_current_thread will detach current thread from vm to collect resources and
  // prepare for destory vm.
  // And this method should be called in close function after every jni execution.
  int detach_current_thread();
  bool is_inited() { return is_inited_; }

private:
  JVMFunctionHelper() {
    int ret = OB_SUCCESS;
    if (is_inited_) {
      // do nothing
    } else if (OB_FAIL(do_init_())) {
      is_inited_ = false;
    } else {
      is_inited_ = true;
    }
  }

  // Only simplify checking jni exception and clear it to avoid jni excution failed.
  int check_jni_exception_(JNIEnv *env);

  // Check loaded jars are valid
  bool is_valid_loaded_jars_();

  int init_classes();
  int do_init_();
  int jni_find_class(const char *clazz, jclass *gen_clazz);
  void *ob_alloc_jni(void* ctxp, int64_t size);

  int search_dir_file(const char *path, const char *file_name, bool &found);
  int get_lib_path(char *path, uint64_t length, const char* lib_name);

  void ob_java_free(void *ctxp, void *ptr);

  int open_java_lib(ObJavaEnvContext &java_env_ctx);

  int load_lib(ObJavaEnvContext &java_env_ctx);

private:
  const char* JAR_VERSION_CLASS = "com/oceanbase/utils/version/JarVersion";

private:
  bool is_inited_;
  static thread_local JNIEnv *jni_env_;

  common::ObArenaAllocator allocator_;

  jclass object_class_;
  jclass object_array_class_;
  jclass string_class_;
  jclass jarrays_class_;
  jclass list_class_;
  jclass exception_util_class_;

  jmethodID string_construct_with_bytes_;
  jobject utf8_charsets_;

  // Env contexts
  ObJavaEnvContext java_env_ctx_;

  // Handle the runtime shared library
  void* jvm_lib_handle_ = nullptr;                  

private:
  lib::ObMutex lock_;
  obsys::ObRWLock load_lib_lock_;
};

// local object reference guard.
// The objects inside are automatically call DeleteLocalRef in the life object.
#define LOCAL_REF_GUARD(lref)                                                  \
  if (lref) {                                                                  \
    JNIEnv *env;                                                               \
    if (OB_FAIL(JVMFunctionHelper::getInstance().get_env(env))) {               \
      LOG_WARN("failed to get jni env", K(ret));                               \
    } else {                                                                   \
      env->DeleteLocalRef(lref);                                               \
    }                                                                          \
    lref = nullptr;                                                            \
  }

// A global ref of the guard, handle can be shared across threads
class JavaGlobalRef {
public:
  JavaGlobalRef(jobject handle) : handle_(handle) {}
  ~JavaGlobalRef();
  JavaGlobalRef(const JavaGlobalRef &) = delete;

  JavaGlobalRef(JavaGlobalRef &&other) noexcept {
    handle_ = other.handle_;
    other.handle_ = nullptr;
  }

  JavaGlobalRef &operator=(JavaGlobalRef &&other) noexcept {
    JavaGlobalRef tmp(std::move(other));
    std::swap(this->handle_, tmp.handle_);
    return *this;
  }

  jobject handle() const { return handle_; }

  jobject &handle() { return handle_; }

  void clear();

private:
  jobject handle_;
};

// A Class object created from the ClassLoader that can be accessed by multiple threads
class JVMClass 
{
public:
  JVMClass(jobject clazz) : clazz_(clazz) {}
  JVMClass(const JVMClass &) = delete;

  JVMClass &operator=(const JVMClass &&) = delete;
  JVMClass &operator=(const JVMClass &other) = delete;

  JVMClass(JVMClass &&other) noexcept : clazz_(nullptr) {
    clazz_ = std::move(other.clazz_);
  }

  JVMClass &operator=(JVMClass &&other) noexcept {
    JVMClass tmp(std::move(other));
    std::swap(this->clazz_, tmp.clazz_);
    return *this;
  }

  jclass clazz() const { return (jclass)clazz_.handle(); }

private:
    JavaGlobalRef clazz_;
};

// Used to get function signatures
class ClassAnalyzer {
public:
  ClassAnalyzer() = default;
  ~ClassAnalyzer() = default;
  int has_method(jclass clazz, const std::string &method, bool *has);
  int get_signature(jclass clazz, const std::string &method, std::string *sign);
  int get_method_object(jclass clazz, const std::string &method_name,
                        jobject &jobj);
};

// Check whether java runtime can work
int detect_java_runtime();

} // namespace sql
} // namespace oceanbase

#endif /* OBDEV_SRC_SQL_ENGINE_CONNECTOR_OB_JAVA_HELPER_H_ */
