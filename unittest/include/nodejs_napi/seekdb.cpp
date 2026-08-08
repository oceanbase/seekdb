/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */

#define NODE_ADDON_API_DISABLE_DEPRECATED
#include "napi.h"

#include <cstring>
#include <memory>
#include <string>
#include <vector>

// Include seekdb C API header
#include "seekdb.h"

// Type tags for external objects
static const napi_type_tag ConnectionTypeTag = { 0x2345678901234567ULL, 0x8901234567890123ULL };
static const napi_type_tag ResultTypeTag = { 0x4567890123456789ULL, 0x0123456789012345ULL };

// Connection wrapper
struct SeekdbConnectionWrapper {
  SeekdbHandle handle;
  std::string db_name;
  bool autocommit;
  
  SeekdbConnectionWrapper(SeekdbHandle h, const std::string& name, bool ac)
    : handle(h), db_name(name), autocommit(ac) {}
  
  ~SeekdbConnectionWrapper() {
    if (handle) {
      seekdb_connect_close(handle);
      handle = nullptr;
    }
  }
};

// Result wrapper
struct SeekdbResultWrapper {
  SeekdbResult result;
  int64_t row_count;
  int32_t column_count;
  std::vector<std::string> column_names;
  
  SeekdbResultWrapper(SeekdbResult r) 
    : result(r), row_count(0), column_count(0) {
    if (result) {
      row_count = static_cast<int64_t>(seekdb_num_rows(result));
      column_count = static_cast<int32_t>(seekdb_num_fields(result));
      
      // Get column names
      for (int32_t i = 0; i < column_count; i++) {
        std::vector<char> name_buf(256, 0);
        int ret = seekdb_result_column_name(result, i, name_buf.data(), name_buf.size());
        if (ret == SEEKDB_SUCCESS) {
          size_t actual_len = strlen(name_buf.data());
          if (actual_len > 0) {
            column_names.push_back(std::string(name_buf.data(), actual_len));
            continue;
          }
        }
        
        // Fallback: use default name
        char default_name[64];
        snprintf(default_name, sizeof(default_name), "col_%d", i);
        column_names.push_back(std::string(default_name));
      }
    }
  }
  
  ~SeekdbResultWrapper() {
    if (result) {
      seekdb_result_free(result);
      result = nullptr;
    }
  }
};

// Helper function to get connection from external
SeekdbConnectionWrapper* GetConnectionFromExternal(Napi::Env env, Napi::Value value) {
  if (!value.IsExternal()) {
    Napi::TypeError::New(env, "Expected connection object").ThrowAsJavaScriptException();
    return nullptr;
  }
  auto external = value.As<Napi::External<SeekdbConnectionWrapper>>();
  if (!external.CheckTypeTag(&ConnectionTypeTag)) {
    Napi::TypeError::New(env, "Invalid connection type").ThrowAsJavaScriptException();
    return nullptr;
  }
  return external.Data();
}

// Helper function to get result from external
SeekdbResultWrapper* GetResultFromExternal(Napi::Env env, Napi::Value value) {
  if (!value.IsExternal()) {
    Napi::TypeError::New(env, "Expected result object").ThrowAsJavaScriptException();
    return nullptr;
  }
  auto external = value.As<Napi::External<SeekdbResultWrapper>>();
  if (!external.CheckTypeTag(&ResultTypeTag)) {
    Napi::TypeError::New(env, "Invalid result type").ThrowAsJavaScriptException();
    return nullptr;
  }
  return external.Data();
}

// Create external with finalizer
template<typename T>
Napi::External<T> CreateExternalWithFinalizer(Napi::Env env, const napi_type_tag& type_tag, T* data) {
  auto external = Napi::External<T>::New(env, data, [](Napi::Env, T* data) {
    delete data;
  });
  external.TypeTag(&type_tag);
  return external;
}

// seekdb_open binding
Napi::Value SeekdbOpen(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  std::string db_dir = info[0].As<Napi::String>().Utf8Value();
  int ret = seekdb_open(db_dir.c_str());
  
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_last_error();
    Napi::Error::New(env, error ? error : "seekdb_open failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  return env.Undefined();
}

// seekdb_close binding
Napi::Value SeekdbClose(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  seekdb_close();
  return env.Undefined();
}

// seekdb_connect binding
Napi::Value SeekdbConnect(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected at least 2 arguments").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  std::string database = info[0].As<Napi::String>().Utf8Value();
  // JS uses seekdb.connect(database, autocommit) — second arg is the boolean (see test.js / index.js).
  // Legacy call shape connect(database, _, autocommit) kept when a third boolean is provided.
  bool autocommit = false;
  if (info.Length() >= 2 && info[1].IsBoolean()) {
    autocommit = info[1].As<Napi::Boolean>().Value();
  } else if (info.Length() >= 3 && info[2].IsBoolean()) {
    autocommit = info[2].As<Napi::Boolean>().Value();
  }
  
  SeekdbHandle handle = nullptr;
  int ret = seekdb_connect(&handle, database.c_str(), autocommit);
  
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_last_error();
    Napi::Error::New(env, error ? error : "seekdb_connect failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = new SeekdbConnectionWrapper(handle, database, autocommit);
  return CreateExternalWithFinalizer<SeekdbConnectionWrapper>(env, ConnectionTypeTag, conn);
}

// seekdb_connect_close binding
Napi::Value SeekdbConnectClose(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  if (conn->handle) {
    seekdb_connect_close(conn->handle);
    conn->handle = nullptr;
  }
  
  return env.Undefined();
}

// seekdb_query binding
Napi::Value SeekdbQuery(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected 2 arguments").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  std::string sql = info[1].As<Napi::String>().Utf8Value();
  
  SeekdbResult result = nullptr;
  int ret = seekdb_query(conn->handle, sql.c_str(), &result);
  
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_error(conn->handle);
    Napi::Error::New(env, error ? error : "seekdb_query failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  // Get stored result
  result = seekdb_store_result(conn->handle);
  if (!result) {
    Napi::Error::New(env, "Result is null").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto result_wrapper = new SeekdbResultWrapper(result);
  auto result_obj = Napi::Object::New(env);
  
  // Set row count and column count
  result_obj.Set("rowCount", Napi::Number::New(env, result_wrapper->row_count));
  result_obj.Set("columnCount", Napi::Number::New(env, result_wrapper->column_count));
  
  // Set column names
  auto columns = Napi::Array::New(env, result_wrapper->column_names.size());
  for (size_t i = 0; i < result_wrapper->column_names.size(); i++) {
    columns.Set(i, Napi::String::New(env, result_wrapper->column_names[i]));
  }
  result_obj.Set("columns", columns);
  
  // Fetch all rows
  auto rows = Napi::Array::New(env, result_wrapper->row_count);
  for (int64_t i = 0; i < result_wrapper->row_count; i++) {
    SeekdbRow row = seekdb_fetch_row(result_wrapper->result);
    if (row) {
      auto row_obj = Napi::Array::New(env, result_wrapper->column_count);
      
      for (int32_t j = 0; j < result_wrapper->column_count; j++) {
        if (seekdb_row_is_null(row, j)) {
          row_obj.Set(j, env.Null());
        } else {
          std::vector<char> buf(4096, 0);
          int ret = seekdb_row_get_string(row, j, buf.data(), buf.size());
          if (ret == SEEKDB_SUCCESS) {
            row_obj.Set(j, Napi::String::New(env, buf.data()));
          } else {
            row_obj.Set(j, env.Null());
          }
        }
      }
      
      rows.Set(i, row_obj);
    }
  }
  result_obj.Set("rows", rows);
  
  // Store result wrapper for cleanup
  result_obj.Set("_result", CreateExternalWithFinalizer<SeekdbResultWrapper>(env, ResultTypeTag, result_wrapper));
  
  // Add fetchAll method that returns the rows array from 'this'
  result_obj.Set("fetchAll", Napi::Function::New(env, [](const Napi::CallbackInfo& info) -> Napi::Value {
    // Get 'this' object and return its 'rows' property
    Napi::Object self = info.This().As<Napi::Object>();
    return self.Get("rows");
  }));
  
  return result_obj;
}

// seekdb_begin binding
Napi::Value SeekdbBegin(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  int ret = seekdb_begin(conn->handle);
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_error(conn->handle);
    Napi::Error::New(env, error ? error : "seekdb_begin failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  return env.Undefined();
}

// seekdb_commit binding
Napi::Value SeekdbCommit(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  int ret = seekdb_commit(conn->handle);
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_last_error();
    Napi::Error::New(env, error ? error : "seekdb_commit failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  return env.Undefined();
}

// seekdb_rollback binding
Napi::Value SeekdbRollback(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  int ret = seekdb_rollback(conn->handle);
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_last_error();
    Napi::Error::New(env, error ? error : "seekdb_rollback failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  return env.Undefined();
}

// seekdb_query for update (returns affected rows)
Napi::Value SeekdbExecuteUpdate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected 2 arguments").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  std::string sql = info[1].As<Napi::String>().Utf8Value();
  
  SeekdbResult result = nullptr;
  int ret = seekdb_query(conn->handle, sql.c_str(), &result);
  
  if (ret != SEEKDB_SUCCESS) {
    const char* error = seekdb_error(conn->handle);
    Napi::Error::New(env, error ? error : "seekdb_query failed").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  if (result) {
    seekdb_result_free(result);
  }
  
  my_ulonglong affected_rows = seekdb_affected_rows(conn->handle);
  return Napi::Number::New(env, static_cast<int64_t>(affected_rows));
}

// seekdb_error binding
Napi::Value SeekdbGetLastError(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected 1 argument").ThrowAsJavaScriptException();
    return env.Null();
  }
  
  auto conn = GetConnectionFromExternal(env, info[0]);
  if (!conn) {
    return env.Null();
  }
  
  const char* error = seekdb_error(conn->handle);
  if (error) {
    return Napi::String::New(env, error);
  }
  return env.Null();
}
  

// Module initialization
Napi::Object Init(Napi::Env env, Napi::Object exports) {
  // Database operations
  exports.Set("open", Napi::Function::New(env, SeekdbOpen));
  exports.Set("close", Napi::Function::New(env, SeekdbClose));
  exports.Set("connect", Napi::Function::New(env, SeekdbConnect));
  exports.Set("connectClose", Napi::Function::New(env, SeekdbConnectClose));
  exports.Set("query", Napi::Function::New(env, SeekdbQuery));
  exports.Set("begin", Napi::Function::New(env, SeekdbBegin));
  exports.Set("commit", Napi::Function::New(env, SeekdbCommit));
  exports.Set("rollback", Napi::Function::New(env, SeekdbRollback));
  exports.Set("executeUpdate", Napi::Function::New(env, SeekdbExecuteUpdate));
  exports.Set("getLastError", Napi::Function::New(env, SeekdbGetLastError));
  
  // Export error codes
  auto error_codes = Napi::Object::New(env);
  error_codes.Set("SUCCESS", Napi::Number::New(env, SEEKDB_SUCCESS));
  error_codes.Set("ERROR_INVALID_PARAM", Napi::Number::New(env, SEEKDB_ERROR_INVALID_PARAM));
  error_codes.Set("ERROR_CONNECTION_FAILED", Napi::Number::New(env, SEEKDB_ERROR_CONNECTION_FAILED));
  error_codes.Set("ERROR_QUERY_FAILED", Napi::Number::New(env, SEEKDB_ERROR_QUERY_FAILED));
  exports.Set("ERROR_CODES", error_codes);
  
  return exports;
}

NODE_API_MODULE(seekdb, Init)
