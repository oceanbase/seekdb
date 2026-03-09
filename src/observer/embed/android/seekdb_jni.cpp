/*
 * SeekDB JNI Bridge
 *
 * Maps Java/Kotlin native methods (SeekDBNative) to the C API.
 */
#include <jni.h>
#include <string>
#include "observer/embed/c/seekdb.h"

static void throw_seekdb_exception(JNIEnv* env, const char* msg)
{
  jclass cls = env->FindClass("com/oceanbase/seekdb/sdk/SeekDBException");
  if (cls) {
    env->ThrowNew(cls, msg ? msg : "unknown error");
  }
}

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeOpen(JNIEnv* env, jclass, jstring jDbDir)
{
  const char* db_dir = env->GetStringUTFChars(jDbDir, nullptr);
  seekdb_handle handle = nullptr;
  int rc = seekdb_open(db_dir, &handle);
  env->ReleaseStringUTFChars(jDbDir, db_dir);
  if (rc != 0) {
    const char* err = handle ? seekdb_error(handle) : "open failed";
    throw_seekdb_exception(env, err);
    return 0;
  }
  return reinterpret_cast<jlong>(handle);
}

JNIEXPORT jlong JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeOpenWithService(JNIEnv* env, jclass, jstring jDbDir, jint port)
{
  const char* db_dir = env->GetStringUTFChars(jDbDir, nullptr);
  seekdb_handle handle = nullptr;
  int rc = seekdb_open_with_service(db_dir, static_cast<int>(port), &handle);
  env->ReleaseStringUTFChars(jDbDir, db_dir);
  if (rc != 0) {
    const char* err = handle ? seekdb_error(handle) : "open failed";
    throw_seekdb_exception(env, err);
    return 0;
  }
  return reinterpret_cast<jlong>(handle);
}

JNIEXPORT void JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeClose(JNIEnv*, jclass, jlong handle)
{
  seekdb_close(reinterpret_cast<seekdb_handle>(handle));
}

JNIEXPORT jlong JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeConnect(JNIEnv* env, jclass, jlong dbHandle, jstring jDbName)
{
  auto db = reinterpret_cast<seekdb_handle>(dbHandle);
  const char* db_name = env->GetStringUTFChars(jDbName, nullptr);
  seekdb_conn_handle conn = nullptr;
  int rc = seekdb_connect(db, db_name, &conn);
  env->ReleaseStringUTFChars(jDbName, db_name);
  if (rc != 0) {
    throw_seekdb_exception(env, seekdb_error(db));
    return 0;
  }
  return reinterpret_cast<jlong>(conn);
}

JNIEXPORT void JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeDisconnect(JNIEnv*, jclass, jlong connHandle)
{
  seekdb_disconnect(reinterpret_cast<seekdb_conn_handle>(connHandle));
}

JNIEXPORT jlong JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeExecute(JNIEnv* env, jclass, jlong dbHandle, jlong connHandle, jstring jSql)
{
  auto db = reinterpret_cast<seekdb_handle>(dbHandle);
  auto conn = reinterpret_cast<seekdb_conn_handle>(connHandle);
  const char* sql = env->GetStringUTFChars(jSql, nullptr);
  seekdb_result_handle result = nullptr;
  int rc = seekdb_execute(conn, sql, &result);
  env->ReleaseStringUTFChars(jSql, sql);
  if (rc != 0) {
    throw_seekdb_exception(env, seekdb_error(db));
    return 0;
  }
  return reinterpret_cast<jlong>(result);
}

JNIEXPORT void JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeResultFree(JNIEnv*, jclass, jlong resultHandle)
{
  seekdb_result_free(reinterpret_cast<seekdb_result_handle>(resultHandle));
}

JNIEXPORT jint JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeResultColumnCount(JNIEnv*, jclass, jlong resultHandle)
{
  return seekdb_result_column_count(reinterpret_cast<seekdb_result_handle>(resultHandle));
}

JNIEXPORT jstring JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeResultColumnName(JNIEnv* env, jclass, jlong resultHandle, jint col)
{
  const char* name = seekdb_result_column_name(reinterpret_cast<seekdb_result_handle>(resultHandle), col);
  return name ? env->NewStringUTF(name) : nullptr;
}

JNIEXPORT jint JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeResultRowCount(JNIEnv*, jclass, jlong resultHandle)
{
  return seekdb_result_row_count(reinterpret_cast<seekdb_result_handle>(resultHandle));
}

JNIEXPORT jstring JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeResultValue(JNIEnv* env, jclass, jlong resultHandle, jint row, jint col)
{
  const char* val = seekdb_result_value(reinterpret_cast<seekdb_result_handle>(resultHandle), row, col);
  return val ? env->NewStringUTF(val) : nullptr;
}

JNIEXPORT jint JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeResultAffectedRows(JNIEnv*, jclass, jlong resultHandle)
{
  return seekdb_result_affected_rows(reinterpret_cast<seekdb_result_handle>(resultHandle));
}

JNIEXPORT jstring JNICALL
Java_com_oceanbase_seekdb_sdk_SeekDBNative_nativeError(JNIEnv* env, jclass, jlong dbHandle)
{
  const char* err = seekdb_error(reinterpret_cast<seekdb_handle>(dbHandle));
  return err ? env->NewStringUTF(err) : nullptr;
}

} // extern "C"
