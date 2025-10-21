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
#include "sql/engine/connector/ob_java_env.h"
#include "sql/engine/connector/ob_java_helper.h"
#include "sql/engine/connector/ob_jni_connector.h"

namespace oceanbase {

namespace sql {


int ObJniConnector::check_jni_exception_(JNIEnv *env) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(env)) {
    ret = OB_JNI_ENV_ERROR;
    LOG_WARN("failed to get jni env", K(ret), K(lbt()));
  } else {
    jthrowable thr = env->ExceptionOccurred();
    if (!OB_ISNULL(thr)) {
      ret = OB_JNI_JAVA_EXCEPTION_ERROR;
      jclass throwableClass = env->GetObjectClass(thr);

      jmethodID getMessageMethod = env->GetMethodID(
          throwableClass,
          "getMessage",
          "()Ljava/lang/String;"
      );
      
      if (getMessageMethod != nullptr) {
          jstring jmsg = (jstring)env->CallObjectMethod(thr, getMessageMethod);
          if (env->ExceptionCheck()) {
              env->ExceptionClear(); // Prevent new exceptions from being generated during the call process
          }
          
          if (jmsg != nullptr) {
              const char* cmsg = env->GetStringUTFChars(jmsg, nullptr);
              LOG_WARN("Exception Message: ", K(cmsg), K(ret));
              env->ReleaseStringUTFChars(jmsg, cmsg);
              env->DeleteLocalRef(jmsg);
          }
      }
      // Create StringWriter and PrintWriter
      jclass stringWriterClass = env->FindClass("java/io/StringWriter");
      jmethodID stringWriterCtor = env->GetMethodID(stringWriterClass, "<init>", "()V");
      jobject stringWriter = env->NewObject(stringWriterClass, stringWriterCtor);
    
      jclass printWriterClass = env->FindClass("java/io/PrintWriter");
      jmethodID printWriterCtor = env->GetMethodID(printWriterClass, "<init>", "(Ljava/io/Writer;)V");
      jobject printWriter = env->NewObject(printWriterClass, printWriterCtor, stringWriter);
      // Call printStackTrace
      jmethodID printStackTraceMethod = env->GetMethodID(
          throwableClass, 
          "printStackTrace", 
          "(Ljava/io/PrintWriter;)V"
      );
      env->CallVoidMethod(thr, printStackTraceMethod, printWriter);
      // Get stack string
      jmethodID toStringMethod = env->GetMethodID(
          stringWriterClass, 
          "toString", 
          "()Ljava/lang/String;"
      );
      jstring stackTrace = (jstring)env->CallObjectMethod(stringWriter, toStringMethod);
      // Convert to C string
      const char* cStackTrace = env->GetStringUTFChars(stackTrace, nullptr);
      LOG_WARN("Exception Stack Trace: ", K(cStackTrace));
      // Release resources
      env->ReleaseStringUTFChars(stackTrace, cStackTrace);
      env->DeleteLocalRef(stackTrace);
      env->DeleteLocalRef(printWriter);
      env->DeleteLocalRef(stringWriter);
      env->DeleteLocalRef(throwableClass);
      env->DeleteLocalRef(printWriterClass);
      env->DeleteLocalRef(stringWriterClass);
      
      env->ExceptionDescribe();
      env->ExceptionClear(); // clear exception state
      env->DeleteLocalRef(thr);
    }
  }
  return ret;
}

int ObJniConnector::get_jni_env(JNIEnv *&env) {
  int ret = OB_SUCCESS;
  // This entry method is the first time to init jni env
  if (!ObJavaEnv::getInstance().is_env_inited()) {
    ret = OB_JNI_ENV_ERROR;
    LOG_WARN("failed to init env variables", K(ret));
  } else {
    JVMFunctionHelper &helper = JVMFunctionHelper::getInstance();
    if (OB_FAIL(helper.get_env(env))) {
      LOG_WARN("failed to get jni env from helper", K(ret));
    } else if (nullptr == env) {
      ret = OB_JNI_ENV_ERROR;
      LOG_WARN("failed to get null jni env from helper", K(ret));
    } else { /* do nothing */
    }
    if (OB_SUCC(ret) && !helper.is_inited()) {
      // Means helper is not ready.
      ret = OB_JNI_ENV_ERROR;
      LOG_WARN("failed to get jni env", K(ret));
    } else if (OB_SUCC(ret) && OB_FAIL(helper.get_env(env))) {
      // re-get the env
      LOG_WARN("failed to re-get jni env", K(ret));
    }
  }
  return ret;
}




ObJniConnector::OdpsType ObJniConnector::get_odps_type_by_string(ObString type)
{
  OdpsType ret_type = OdpsType::UNKNOWN;
  if (type.prefix_match("BOOLEAN")) {
    ret_type = OdpsType::BOOLEAN;
  } else if (type.prefix_match("TINYINT")) {
    ret_type = OdpsType::TINYINT;
  } else if (type.prefix_match("SMALLINT")) {
    ret_type = OdpsType::SMALLINT;
  } else if (type.prefix_match("INT")) {
    ret_type = OdpsType::INT;
  } else if (type.prefix_match("BIGINT")) {
    ret_type = OdpsType::BIGINT;
  } else if (type.prefix_match("FLOAT")) {
    ret_type = OdpsType::FLOAT;
  } else if (type.prefix_match("DOUBLE")) {
    ret_type = OdpsType::DOUBLE;
  } else if (type.prefix_match("DECIMAL")) {
    ret_type = OdpsType::DECIMAL;
  } else if (type.prefix_match("VARCHAR")) {
    ret_type = OdpsType::VARCHAR;
  } else if (type.prefix_match("CHAR")) {
    ret_type = OdpsType::CHAR;
  } else if (type.prefix_match("STRING")) {
    ret_type = OdpsType::STRING;
  } else if (type.prefix_match("BINARY")) {
    ret_type = OdpsType::BINARY;
  } else if (type.prefix_match("TIMESTAMP_NTZ")) {
    ret_type = OdpsType::TIMESTAMP_NTZ;
  } else if (type.prefix_match("TIMESTAMP")) {
    ret_type = OdpsType::TIMESTAMP;
  } else if (type.prefix_match("DATETIME")) {
    ret_type = OdpsType::DATETIME;
  } else if (type.prefix_match("DATE")) {
    ret_type = OdpsType::DATE;
  } else if (type.prefix_match("MAP")) {
    ret_type = OdpsType::MAP;
  } else if (type.prefix_match("ARRAY")) {
    ret_type = OdpsType::ARRAY;
  } else if (type.prefix_match("STRUCT")) {
    ret_type = OdpsType::STRUCT;
  } else if (type.prefix_match("JSON")) {
    ret_type = OdpsType::JSON;
  } else if (type.prefix_match("ARRAY")) {
    ret_type = OdpsType::ARRAY;
  } else if (type.prefix_match("INTERVAL_YEAR_MONTH")) {
    ret_type = OdpsType::INTERVAL_YEAR_MONTH;
  } else if (type.prefix_match("INTERVAL_DAY_TIME")) {
    ret_type = OdpsType::INTERVAL_DAY_TIME;
  } else if (type.prefix_match("VOID")) {
    ret_type = OdpsType::ARRAY;
  }
  return ret_type;
}

int ObJniConnector::MirrorOdpsJniColumn::assign(const MirrorOdpsJniColumn &other)
{
  int ret = OB_SUCCESS;
  if (this != &other) {
    name_ = other.name_;
    type_ = other.type_;
    precision_ = other.precision_;
    scale_ = other.scale_;
    length_ = other.length_;
    type_size_ = other.type_size_;
    type_expr_ = other.type_expr_;
    if (OB_FAIL(child_columns_.assign(other.child_columns_))) {
      LOG_WARN("failed to assign array");
    }
  }
  return ret;
}

}
}
