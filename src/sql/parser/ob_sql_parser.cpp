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

#define USING_LOG_PREFIX SQL

#include "ob_sql_parser.h"
#include "sql/parser/parse_node.h"
#include "share/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/oblog/ob_log.h"
#include "sql/parser/parse_malloc.h"
#include "sql/parser/ob_file_sql_rewrite.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/worker.h"
#include <limits.h>
#include <cstdlib>
#include <cstring>
#include <string>


namespace oceanbase
{
using namespace common;
namespace sql
{
static bool file_sql_session_authorized(std::string &root,std::string &reason)
{
  ObSQLSessionInfo *session = THIS_WORKER.get_session();
  if (OB_ISNULL(session)) {
    reason="No SQL session available for file SQL";
    return false;
  }
  const ObObj *flag=session->get_user_variable_value(
      ObString::make_string("__seekdb_file_sql_enabled"));
  if (OB_ISNULL(flag) || !flag->is_int() || flag->get_int()!=1) {
    reason="enable_file_sql is OFF in this SQL session";
    return false;
  }
  ObObj secure_value;
  const ObString name=ObString::make_string("secure_file_priv");
  const int check=session->get_sys_variable_by_name(name,secure_value);
  if (check!=OB_SUCCESS || secure_value.is_null() ||
      !secure_value.is_string_type()) {
    reason="secure_file_priv is empty, unavailable, or disabled";
    return false;
  }
  const ObString allowed=secure_value.get_string();
  if(allowed.length()==0 || allowed.ptr()==nullptr) {
    reason="secure_file_priv prohibits file access";
    return false;
  }
  const std::string raw(allowed.ptr(),static_cast<size_t>(allowed.length()));
  if(native_file_sql::lower(raw)=="null") {
    reason="secure_file_priv is disabled";
    return false;
  }
  char real[PATH_MAX];
  if(::realpath(raw.c_str(),real)==nullptr) {
    reason="secure_file_priv path does not exist";
    return false;
  }
  struct stat st;
  if(::stat(real,&st)!=0 || !S_ISDIR(st.st_mode)) {
    reason="secure_file_priv is not a directory";
    return false;
  }
  root=real;
  return true;
}
int ObSQLParser::parse(const char * str_ptr, const int64_t str_len, ParseResult &result)
{
  int ret=OB_SUCCESS;
  if (OB_FAIL(parse_init(&result))) {
    // Standard parser initialization failed.
  } else if (OB_UNLIKELY(str_len < 0 || OB_ISNULL(str_ptr))) {
    ret=OB_INVALID_ARGUMENT;
  } else {
    const std::string original(str_ptr,static_cast<size_t>(str_len));
    std::string rewritten,error;
    bool valid=true;
    if(native_file_sql::file_sql_set_statement(original,rewritten)) {
      // Per-connection compatibility setting; cannot enable global access.
    } else {
      std::string shown;
      const bool replaced_variable=
          native_file_sql::file_sql_show_statement(original,shown);
      const std::string &to_rewrite=replaced_variable?shown:original;
      if (native_file_sql::file_sql_statement_detected(to_rewrite)) {
        std::string allowed_root;
        if(!file_sql_session_authorized(allowed_root,error)) {
          ret=OB_NOT_SUPPORTED;
          valid=false;
          LOG_WARN("file SQL is disabled or violates secure_file_priv",
                   "reason",error.c_str());
        } else {
          // Defense in depth: canonical real paths must remain under
          // secure_file_priv, *and* under SEEKDB_FILE_SQL_ROOT if set.
          native_file_sql::ScopedFileSqlSecurity scope(allowed_root);
          if(!native_file_sql::rewrite(to_rewrite,rewritten,error)) {
            // Preserve MySQL-compatible error classes for public File SQL:
            // positional option syntax -> 1064; unsupported sample_size -> 1235.
            if(error.find("file table function options must be named")
               !=std::string::npos) {
              ret=OB_ERR_PARSE_SQL;
            } else if(error.find("sample_size")!=std::string::npos) {
              ret=OB_NOT_SUPPORTED;
            } else {
              ret=OB_INVALID_ARGUMENT;
            }
            valid=false;
            LOG_WARN("file SQL rewrite rejected","detail",error.c_str());
          }
        }
      } else {
        rewritten=to_rewrite;
      }
    }
    if(valid && OB_SUCC(ret)) {
      const char *sql_ptr=str_ptr;
      size_t sql_len=static_cast<size_t>(str_len);
      if(rewritten!=original) {
        char *allocated=static_cast<char *>(
            parse_malloc(rewritten.size()+1,result.malloc_pool_));
        if(OB_ISNULL(allocated)) {
          ret=OB_ALLOCATE_MEMORY_FAILED;
        } else {
          std::memcpy(allocated,rewritten.data(),rewritten.size());
          allocated[rewritten.size()]='\0';
          sql_ptr=allocated;
          sql_len=rewritten.size();
        }
      }
      if(OB_SUCC(ret) && OB_FAIL(parse_sql(&result,sql_ptr,sql_len))) {
        // Preserve default parse errors, including SQL syntax diagnostics.
      }
    }
  }
  return ret;
}

}
}
