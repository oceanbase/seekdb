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
#include "config_bridge.h"
#include "sql/ob_sql.h"
#include "lib/json/ob_json_print_utils.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "query/runtime/ob_query_runtime_environment.h"
#include "share/ob_truncated_string.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "sql/plan_cache/ob_pcv_set.h"
#include "sql/ob_sql_init.h"
#include "sql/resolver/ob_resolver.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "sql/resolver/cmd/ob_call_procedure_stmt.h"
#include "sql/resolver/cmd/ob_anonymous_block_stmt.h"
#include "sql/privilege_check/ob_privilege_check.h"
#include "sql/rewrite/ob_transformer_impl.h"
#include "sql/rewrite/ob_transform_pre_process.h"
#include "sql/code_generator/ob_code_generator.h"
#include "sql/plan_cache/ob_values_table_compression.h"
#include "sql/pl/ob_pl_resolver.h"
#include "lib/utility/ob_smart_call.h"
#include "sql/monitor/show_trace/ob_show_trace.h"
#include "sql/optimizer/ob_optimizer.h"
#include "share/rc/ob_server_runtime.h"
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <strings.h>
#include <dirent.h>
#include <limits.h>
#include "lib/allocator/page_arena.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/system_variable/ob_sys_var_class_type.h"

namespace oceanbase
{
using namespace common;
using namespace rpc::frame;
using namespace obcall;
using namespace share;
using namespace share::schema;
using namespace pl;

namespace sql
{

const int64_t ObSql::max_error_length = 80;
const int64_t ObSql::SQL_MEM_SIZE_LIMIT = 1024 * 1024 * 64;

int ObSql::init(common::ObOptStatManager *opt_stat_mgr,
                common::ObITabletScan *vt_partition_service,
                common::ObAddr &addr,
                ObPlanCache &plan_cache,
                ObPsCache &ps_cache,
                pl::ObPL &pl_engine,
                query::ObIPlanCacheAccessService &plan_cache_access_service,
                query::ObIQueryRuntimeEnvironment &query_runtime_environment,
                query::ObIRootCommandService &root_command_service,
                query::ObILocalCommandService &local_command_service,
                query::ObIChangeStreamService &change_stream_service,
                query::ObIDdlExecutionLimiter &ddl_execution_limiter,
                ObIVirtualTableFactoryProvider &virtual_table_factory_provider,
                common::ObISrsProvider &srs_provider,
                common::ObILobReadService &lob_read_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(opt_stat_mgr)
      || OB_ISNULL(vt_partition_service)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    if (OB_FAIL(queue_.init(1, 512))) {
    } else {
      queue_.bind_root_command_service(root_command_service);
      opt_stat_mgr_ = opt_stat_mgr;
      vt_partition_service_ = vt_partition_service;
      plan_cache_ = &plan_cache;
      ps_cache_ = &ps_cache;
      pl_engine_ = &pl_engine;
      plan_cache_access_service_ = &plan_cache_access_service;
      query_runtime_environment_ = &query_runtime_environment;
      root_command_service_ = &root_command_service;
      local_command_service_ = &local_command_service;
      change_stream_service_ = &change_stream_service;
      ddl_execution_limiter_ = &ddl_execution_limiter;
      virtual_table_factory_provider_ = &virtual_table_factory_provider;
      srs_provider_ = &srs_provider;
      lob_read_service_ = &lob_read_service;
      self_addr_ = addr;
      inited_ = true;
      queue_.start();
    }
  }
  return ret;
}

void ObSql::destroy() {
  if (inited_) {
    queue_.destroy();
    plan_cache_ = NULL;
    ps_cache_ = NULL;
    pl_engine_ = NULL;
    plan_cache_access_service_ = NULL;
    query_runtime_environment_ = NULL;
    root_command_service_ = NULL;
    local_command_service_ = NULL;
    change_stream_service_ = NULL;
    ddl_execution_limiter_ = NULL;
    virtual_table_factory_provider_ = NULL;
    srs_provider_ = NULL;
    lob_read_service_ = NULL;
    inited_ = false;
  }
}

void ObSql::bind_resolver_runtime_services(ObResolverParams &resolver_ctx)
{
  resolver_ctx.plan_cache_ = plan_cache_;
  resolver_ctx.pl_sql_runtime_ = this;
  resolver_ctx.pl_engine_ = pl_engine_;
  resolver_ctx.dependency_info_queue_ = &queue_;
  resolver_ctx.root_command_service_ = root_command_service_;
  resolver_ctx.srs_provider_ = srs_provider_;
  resolver_ctx.lob_read_service_ = lob_read_service_;
}

void ObSql::bind_exec_context_runtime_services(ObExecContext &exec_ctx)
{
  ObExecContext::RuntimeServices services = exec_ctx.get_runtime_services();
  services.lob_read_service_ = lob_read_service_;
  services.plan_cache_ = plan_cache_;
  services.ps_cache_ = ps_cache_;
  services.plan_cache_access_service_ = plan_cache_access_service_;
  services.pl_sql_runtime_ = this;
  services.pl_engine_ = pl_engine_;
  services.prepared_statement_runtime_ = this;
  services.sql_execution_id_provider_ = this;
  services.query_runtime_environment_ = query_runtime_environment_;
  services.root_command_service_ = root_command_service_;
  services.local_command_service_ = local_command_service_;
  services.change_stream_service_ = change_stream_service_;
  services.ddl_execution_limiter_ = ddl_execution_limiter_;
  services.virtual_table_factory_provider_ = virtual_table_factory_provider_;
  services.srs_provider_ = srs_provider_;
  exec_ctx.set_runtime_services(services);
}


int ObSql::stmt_prepare(const common::ObString &stmt,
                        ObSqlCtx &context,
                        ObResultSet &result,
                        bool is_inner_sql/*true*/)
{
  int ret = OB_SUCCESS;
  LinkExecCtxGuard link_guard(result.get_session(), result.get_exec_context());
  if (OB_FAIL(sanity_check(context))) {
  } else if (OB_FAIL(handle_ps_prepare(stmt, context, result, is_inner_sql))) {
  }
  if (OB_FAIL(ret) && OB_SUCCESS == result.get_errcode()) {
    result.set_errcode(ret);
  }
  if (OB_FAIL(ret)) {
    rollback_implicit_trans_when_fail(result, ret);
  }
  return ret;
}

// ===================== file sql rewrite (2026 competition) =====================
// Parses read_csv()/read_json()/read_parquet()/glob() calls out of the SQL text,
// materialises the referenced local file into an inline UNION ALL subquery, and
// hands the rewritten statement to the normal parser. No changes to the parser,
// resolver or executor are required.
namespace filesql
{

static const char *fs_type_name(int t);
static int fs_build_describe(ObSqlString &sql,
                             ObArray<ObString> &names,
                             ObArray<int> &types,
                             ObArray<bool> &nullable);

static const int64_t FS_MAX_FILE = 64LL * 1024 * 1024;
static const int FS_BIGINT = 0;
static const int FS_DOUBLE = 1;
static const int FS_BOOL = 2;
static const int FS_DATE = 3;
static const int FS_DATETIME = 4;
static const int FS_VARCHAR = 5;

inline bool fs_is_alpha(char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

inline bool fs_is_digit(char c) { return c >= '0' && c <= '9'; }

inline bool fs_is_word_char(char c)
{
  return fs_is_alpha(c) || fs_is_digit(c);
}

inline int64_t fs_skip_ws(const char *s, int64_t len, int64_t i)
{
  while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) {
    ++i;
  }
  return i;
}

// i points at '('; returns index of the matching ')', or -1.
static int64_t fs_match_paren(const char *s, int64_t len, int64_t i)
{
  int64_t depth = 0;
  bool sq = false;
  bool dq = false;
  bool bt = false;
  for (; i < len; ++i) {
    const char c = s[i];
    if (sq) {
      if (c == '\\' && i + 1 < len) {
        ++i;
      } else if (c == '\'') {
        if (i + 1 < len && s[i + 1] == '\'') { ++i; } else { sq = false; }
      }
    } else if (dq) {
      if (c == '\\' && i + 1 < len) {
        ++i;
      } else if (c == '"') {
        if (i + 1 < len && s[i + 1] == '"') { ++i; } else { dq = false; }
      }
    } else if (bt) {
      if (c == '`') { bt = false; }
    } else if (c == '\'') { sq = true; }
    else if (c == '"') { dq = true; }
    else if (c == '`') { bt = true; }
    else if (c == '(') { ++depth; }
    else if (c == ')') {
      --depth;
      if (0 == depth) { return i; }
    }
  }
  return -1;
}

// Extracts the first argument when it is a single-quoted string literal.
static int fs_first_string_arg(const char *s, int64_t begin, int64_t end,
                               char *out, int64_t cap, int64_t &out_len)
{
  int ret = OB_SUCCESS;
  int64_t i = fs_skip_ws(s, end, begin);
  out_len = 0;
  if (i >= end || s[i] != '\'') {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ++i;
    while (i < end && out_len < cap) {
      if (s[i] == '\'') {
        if (i + 1 < end && s[i + 1] == '\'') {
          out[out_len++] = '\'';
          i += 2;
        } else {
          ++i;
          break;
        }
      } else {
        out[out_len++] = s[i++];
      }
    }
    if (out_len >= cap) { ret = OB_SIZE_OVERFLOW; }
  }
  return ret;
}

static int fs_read_file(const char *path, int64_t path_len, char *&buf, int64_t &len)
{
  int ret = OB_SUCCESS;
  buf = NULL;
  len = 0;
  char zpath[4096];
  if (path_len <= 0 || path_len >= static_cast<int64_t>(sizeof(zpath))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    MEMCPY(zpath, path, path_len);
    zpath[path_len] = '\0';
    const int fd = ::open(zpath, O_RDONLY);
    if (fd < 0) {
      LOG_WARN("open file failed", K(errno), KCSTRING(zpath));
      ret = OB_IO_ERROR;
    } else {
      struct stat st;
      if (0 != ::fstat(fd, &st)) {
        ret = OB_IO_ERROR;
      } else if (st.st_size > FS_MAX_FILE) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        const int64_t size = static_cast<int64_t>(st.st_size);
        buf = static_cast<char *>(ob_malloc(size + 2, "FileSqlRead"));
        if (NULL == buf) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          int64_t got = 0;
          while (got < size) {
            const ssize_t n = ::read(fd, buf + got, size - got);
            if (n <= 0) { break; }
            got += n;
          }
          if (got != size) {
            ret = OB_IO_ERROR;
          } else {
            buf[got] = '\0';
            len = got;
          }
        }
      }
      ::close(fd);
    }
  }
  return ret;
}

static bool fs_all_int(const char *p, int64_t n)
{
  int64_t i = 0;
  if (n <= 0) { return false; }
  if (p[0] == '+' || p[0] == '-') { i = 1; }
  if (i >= n) { return false; }
  for (; i < n; ++i) {
    if (!fs_is_digit(p[i])) { return false; }
  }
  return true;
}

static bool fs_all_number(const char *p, int64_t n)
{
  int64_t i = 0;
  bool dot = false;
  bool digit = false;
  if (n <= 0) { return false; }
  if (p[0] == '+' || p[0] == '-') { i = 1; }
  for (; i < n; ++i) {
    if (p[i] == '.' && !dot) {
      dot = true;
    } else if (fs_is_digit(p[i])) {
      digit = true;
    } else {
      return false;
    }
  }
  return digit;
}

static bool fs_all_bool(const char *p, int64_t n)
{
  return (4 == n && 0 == strncasecmp(p, "true", 4))
      || (5 == n && 0 == strncasecmp(p, "false", 5));
}

// YYYY-MM-DD
static bool fs_is_date(const char *p, int64_t n)
{
  if (n != 10) { return false; }
  for (int64_t i = 0; i < n; ++i) {
    if (i == 4 || i == 7) {
      if (p[i] != '-') { return false; }
    } else if (!fs_is_digit(p[i])) {
      return false;
    }
  }
  return true;
}

// YYYY-MM-DD[ T]HH:MM:SS
static bool fs_is_datetime(const char *p, int64_t n)
{
  if (n != 19) { return false; }
  if (!fs_is_date(p, 10)) { return false; }
  if (p[10] != ' ' && p[10] != 'T') { return false; }
  for (int64_t i = 11; i < n; ++i) {
    if (i == 13 || i == 16) {
      if (p[i] != ':') { return false; }
    } else if (!fs_is_digit(p[i])) {
      return false;
    }
  }
  return true;
}

struct FsCsv
{
  FsCsv() : buf_(NULL), len_(0), rows_(0), cols_(0), off_(NULL), flen_(NULL), quoted_(NULL) {}
  char *buf_;
  int64_t len_;
  int64_t rows_;
  int64_t cols_;
  int64_t *off_;
  int64_t *flen_;
  bool *quoted_;
};

// RFC4180 style parse. Field values stay inside buf_ (quotes stripped in place).
static int fs_parse_csv(char *buf, int64_t len, FsCsv &out)
{
  int ret = OB_SUCCESS;
  int64_t nrows = 0;
  int64_t ncols = 1;
  int64_t cur_cols = 1;
  bool in_quotes = false;
  bool row_started = false;
  for (int64_t i = 0; i < len; ++i) {
    const char c = buf[i];
    if (in_quotes) {
      if (c == '"') {
        if (i + 1 < len && buf[i + 1] == '"') {
          ++i;
        } else {
          in_quotes = false;
        }
      }
    } else if (c == '"') {
      in_quotes = true;
      row_started = true;
    } else if (c == ',') {
      ++cur_cols;
    } else if (c == '\n') {
      ++nrows;
      if (cur_cols > ncols) { ncols = cur_cols; }
      cur_cols = 1;
      row_started = false;
    } else if (c != '\r') {
      row_started = true;
    }
  }
  if (row_started || cur_cols > 1) {
    ++nrows;
    if (cur_cols > ncols) { ncols = cur_cols; }
  }
  if (nrows <= 0 || ncols <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t cells = nrows * ncols;
    out.off_ = static_cast<int64_t *>(ob_malloc(cells * static_cast<int64_t>(sizeof(int64_t)) * 2 + cells, "FileSqlCsv"));
    if (NULL == out.off_) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      out.flen_ = out.off_ + cells;
      out.quoted_ = reinterpret_cast<bool *>(out.flen_ + cells);
      out.buf_ = buf;
      out.len_ = len;
      out.rows_ = nrows;
      out.cols_ = ncols;
      MEMSET(out.off_, 0, cells * sizeof(int64_t));
      MEMSET(out.flen_, 0, cells * sizeof(int64_t));
      MEMSET(out.quoted_, 0, cells);
      int64_t r = 0;
      int64_t c = 0;
      int64_t i = 0;
      while (i < len && r < nrows) {
        const bool q = (buf[i] == '"');
        int64_t start = i;
        int64_t w = i;
        if (q) {
          ++start;
          ++i;
          w = start;
          while (i < len) {
            if (buf[i] == '"') {
              if (i + 1 < len && buf[i + 1] == '"') {
                buf[w++] = '"';
                i += 2;
              } else {
                ++i;
                break;
              }
            } else {
              buf[w++] = buf[i++];
            }
          }
        } else {
          while (i < len && buf[i] != ',' && buf[i] != '\n' && buf[i] != '\r') {
            ++i;
          }
          w = i;
        }
        const int64_t idx = r * ncols + c;
        if (idx < cells) {
          out.off_[idx] = (q ? start : start);
          out.flen_[idx] = w - start;
          out.quoted_[idx] = q;
        }
        if (i < len && buf[i] == ',') {
          ++i;
          ++c;
        } else {
          while (i < len && (buf[i] == '\r' || buf[i] == '\n')) { ++i; }
          ++r;
          c = 0;
        }
      }
      out.rows_ = r;
    }
  }
  return ret;
}

static int fs_append_quoted(ObSqlString &sql, const char *p, int64_t n)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append("'"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < n; ++i) {
      const char c = p[i];
      if (c == '\'') {
        ret = sql.append("''");
      } else if (c == '\\') {
        ret = sql.append("\\\\");
      } else if (c == '\n') {
        ret = sql.append("\\n");
      } else if (c == '\r') {
        ret = sql.append("\\r");
      } else {
        ret = sql.append(&c, 1);
      }
    }
    if (OB_SUCC(ret)) {
      ret = sql.append("'");
    }
  }
  return ret;
}

static int fs_build_csv_subquery(ObSqlString &sql, FsCsv &csv, ObSqlString *describe_out = NULL)
{
  int ret = OB_SUCCESS;
  int64_t ncols = csv.cols_;
  int64_t nrows = csv.rows_;
  // header row drives column names
  ObArray<ObString> names;
  ObArray<int> types;
  ObArray<bool> nullable;
  char *namebuf = NULL;
  if (nrows <= 0 || ncols <= 0) {
    ret = OB_INVALID_ARGUMENT;
  } else if (NULL == (namebuf = static_cast<char *>(ob_malloc(ncols * 256, "FileSqlName")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    for (int64_t c = 0; OB_SUCC(ret) && c < ncols; ++c) {
      const int64_t idx = c;
      ObString name(csv.flen_[idx], csv.buf_ + csv.off_[idx]);
      bool dup = false;
      for (int64_t k = 0; k < names.count(); ++k) {
        if (0 == names.at(k).case_compare(name)) { dup = true; break; }
      }
      if (dup) {
        char *slot = namebuf + c * 256;
        int64_t seq = 2;
        while (OB_SUCC(ret)) {
          const int n = snprintf(slot, 256, "%.*s__%ld", static_cast<int>(name.length()),
                                 name.ptr(), static_cast<long>(seq));
          if (n <= 0 || n >= 256) {
            ret = OB_SIZE_OVERFLOW;
            break;
          }
          ObString cand(n, slot);
          bool still = false;
          for (int64_t k = 0; k < names.count(); ++k) {
            if (0 == names.at(k).case_compare(cand)) { still = true; break; }
          }
          if (!still) { name = cand; break; }
          ++seq;
        }
      }
      if (OB_SUCC(ret)) { ret = names.push_back(name); }
    }
  }
  // infer types from data rows (skip header)
  for (int64_t c = 0; OB_SUCC(ret) && c < ncols; ++c) {
    bool all_int = true, all_num = true, all_bool = true, all_date = true, all_dt = true;
    bool any = false, any_null = false;
    for (int64_t r = 1; r < nrows; ++r) {
      const int64_t idx = r * ncols + c;
      const int64_t n = csv.flen_[idx];
      const char *p = csv.buf_ + csv.off_[idx];
      if (0 == n) { any_null = true; continue; }
      any = true;
      if (!fs_all_int(p, n)) { all_int = false; }
      if (!fs_all_number(p, n)) { all_num = false; }
      if (!fs_all_bool(p, n)) { all_bool = false; }
      if (!fs_is_date(p, n)) { all_date = false; }
      if (!fs_is_datetime(p, n)) { all_dt = false; }
    }
    int t = FS_VARCHAR;
    if (!any) { t = FS_VARCHAR; }
    else if (all_int) { t = FS_BIGINT; }
    else if (all_num) { t = FS_DOUBLE; }
    else if (all_bool) { t = FS_BOOL; }
    else if (all_dt) { t = FS_DATETIME; }
    else if (all_date) { t = FS_DATE; }
    if (OB_SUCC(ret)) { ret = types.push_back(t); }
    if (OB_SUCC(ret)) { ret = nullable.push_back(any_null); }
  }
  if (OB_SUCC(ret) && NULL != describe_out) {
    ret = fs_build_describe(*describe_out, names, types, nullable);
  }
  // emit (SELECT ... UNION ALL SELECT ...)
  if (OB_SUCC(ret) && NULL == describe_out) {
    ret = sql.append("(");
  }
  for (int64_t r = 1; OB_SUCC(ret) && NULL == describe_out && r < nrows; ++r) {
    if (1 == r) {
      ret = sql.append("SELECT ");
    } else {
      ret = sql.append(" UNION ALL SELECT ");
    }
    for (int64_t c = 0; OB_SUCC(ret) && c < ncols; ++c) {
      if (c > 0) { ret = sql.append(", "); }
      const int64_t idx = r * ncols + c;
      const int64_t n = csv.flen_[idx];
      const char *p = csv.buf_ + csv.off_[idx];
      const int t = types.at(c);
      if (0 == n) {
        ret = sql.append("NULL");
      } else if (FS_BIGINT == t) {
        ret = sql.append("CAST(");
        if (OB_SUCC(ret)) { ret = sql.append(p, n); }
        if (OB_SUCC(ret)) { ret = sql.append(" AS SIGNED)"); }
      } else if (FS_DOUBLE == t) {
        ret = sql.append("CAST(");
        if (OB_SUCC(ret)) { ret = sql.append(p, n); }
        if (OB_SUCC(ret)) { ret = sql.append(" AS DOUBLE)"); }
      } else if (FS_BOOL == t) {
        ret = sql.append(0 == strncasecmp(p, "true", n) ? "1" : "0");
      } else if (FS_DATE == t) {
        ret = fs_append_quoted(sql, p, n);
      } else if (FS_DATETIME == t) {
        char tmp[64];
        int64_t m = n;
        for (int64_t k = 0; k < n; ++k) { tmp[k] = (p[k] == 'T') ? ' ' : p[k]; }
        ret = fs_append_quoted(sql, tmp, m);
      } else {
        ret = fs_append_quoted(sql, p, n);
      }
      if (OB_SUCC(ret) && 1 == r) {
        ret = sql.append(" AS `");
        if (OB_SUCC(ret)) {
          ObString &nm = names.at(c);
          ret = sql.append(nm.ptr(), nm.length());
        }
        if (OB_SUCC(ret)) { ret = sql.append("`"); }
      }
    }
  }
  if (OB_SUCC(ret) && NULL == describe_out) {
    ret = sql.append(")");
  }
  if (NULL != namebuf) {
    ob_free(namebuf);
    namebuf = NULL;
  }
  return ret;
}

// =================== end file sql rewrite helper ===================

// ---- JSONL / glob / describe ----
struct FsJsonVal
{
  FsJsonVal() : kind(0), p(NULL), n(0) {}
  int kind;      // 0 null, 1 string, 2 number, 3 bool
  const char *p;
  int64_t n;
};

static int64_t fs_skip_json_ws(const char *s, int64_t len, int64_t i)
{
  while (i < len && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r')) { ++i; }
  return i;
}

// parse one value starting at i (i points at first char of value)
static int64_t fs_json_value(const char *s, int64_t len, int64_t i, FsJsonVal &v)
{
  i = fs_skip_json_ws(s, len, i);
  if (i >= len) { return -1; }
  if (s[i] == '"') {
    const int64_t b = ++i;
    char *tmp = NULL;
    while (i < len && s[i] != '"') {
      if (s[i] == '\\' && i + 1 < len) { i += 2; } else { ++i; }
    }
    v.kind = 1;
    v.p = s + b;
    v.n = i - b;
    (void)tmp;
    return (i < len) ? i + 1 : i;
  }
  if (0 == strncmp(s + i, "true", 4)) { v.kind = 3; v.p = s + i; v.n = 4; return i + 4; }
  if (0 == strncmp(s + i, "false", 5)) { v.kind = 3; v.p = s + i; v.n = 5; return i + 5; }
  if (0 == strncmp(s + i, "null", 4)) { v.kind = 0; return i + 4; }
  {
    const int64_t b = i;
    while (i < len && s[i] != ',' && s[i] != '}' && s[i] != ' ' && s[i] != '\r') { ++i; }
    v.kind = 2;
    v.p = s + b;
    v.n = i - b;
    return i;
  }
}

// find "key": value
static bool fs_json_lookup(const char *s, int64_t len, const char *key, int64_t klen, FsJsonVal &v)
{
  int64_t i = 0;
  while (i < len) {
    if (s[i] != '"') { ++i; continue; }
    const int64_t b = i + 1;
    int64_t j = b;
    while (j < len && s[j] != '"') {
      if (s[j] == '\\' && j + 1 < len) { j += 2; } else { ++j; }
    }
    if (j >= len) { return false; }
    const int64_t n = j - b;
    int64_t k = fs_skip_json_ws(s, len, j + 1);
    if (k < len && s[k] == ':') {
      int64_t ve = fs_json_value(s, len, k + 1, v);
      if (n == klen && 0 == strncmp(s + b, key, static_cast<size_t>(klen))) {
        return ve >= 0;
      }
      i = (ve >= 0) ? ve : (k + 1);
    } else {
      i = j + 1;
    }
  }
  return false;
}

static int fs_build_jsonl_subquery(ObSqlString &sql, char *buf, int64_t len)
{
  int ret = OB_SUCCESS;
  // collect keys in first-seen order
  ObArray<ObString> keys;
  char keybuf[65536];
  int64_t keybuf_used = 0;
  int64_t i = 0;
  while (OB_SUCC(ret) && i < len) {
    int64_t eol = i;
    while (eol < len && buf[eol] != '\n') { ++eol; }
    int64_t j = i;
    while (j < eol) {
      if (buf[j] != '"') { ++j; continue; }
      const int64_t b = j + 1;
      int64_t k = b;
      while (k < eol && buf[k] != '"') {
        if (buf[k] == '\\' && k + 1 < eol) { k += 2; } else { ++k; }
      }
      if (k >= eol) { break; }
      int64_t c = fs_skip_json_ws(buf, eol, k + 1);
      if (c < eol && buf[c] == ':') {
        ObString name(static_cast<int32_t>(k - b), buf + b);
        bool dup = false;
        for (int64_t m = 0; m < keys.count(); ++m) {
          if (0 == keys.at(m).case_compare(name)) { dup = true; break; }
        }
        if (!dup) {
          if (keybuf_used + name.length() + 1 < static_cast<int64_t>(sizeof(keybuf))) {
            MEMCPY(keybuf + keybuf_used, name.ptr(), name.length());
            ObString stable(name.length(), keybuf + keybuf_used);
            keybuf_used += name.length() + 1;
            ret = keys.push_back(stable);
          } else {
            ret = OB_SIZE_OVERFLOW;
          }
        }
        FsJsonVal tmp;
        int64_t ve = fs_json_value(buf, eol, c + 1, tmp);
        j = (ve >= 0) ? ve : (c + 1);
      } else {
        j = k + 1;
      }
    }
    i = eol + 1;
  }
  // infer column types across rows
  ObArray<int> types;
  for (int64_t m = 0; OB_SUCC(ret) && m < keys.count(); ++m) {
    bool all_num = true, all_int = true, all_bool = true, any = false;
    int64_t li = 0;
    while (li < len) {
      int64_t eol = li;
      while (eol < len && buf[eol] != '\n') { ++eol; }
      FsJsonVal v;
      if (fs_json_lookup(buf + li, eol - li, keys.at(m).ptr(), keys.at(m).length(), v)) {
        if (2 == v.kind) {
          any = true;
          if (!fs_all_int(v.p, v.n)) { all_int = false; }
        } else if (3 == v.kind) {
          any = true;
          all_num = false;
          all_int = false;
        } else if (1 == v.kind) {
          any = true;
          all_num = false;
          all_int = false;
          all_bool = false;
        }
      }
      li = eol + 1;
    }
    int t = FS_VARCHAR;
    if (!any) { t = FS_VARCHAR; }
    else if (all_num && all_int) { t = FS_BIGINT; }
    else if (all_num) { t = FS_DOUBLE; }
    else if (all_bool) { t = FS_BOOL; }
    ret = types.push_back(t);
  }
  // emit
  if (OB_SUCC(ret)) { ret = sql.append("("); }
  int64_t r = 0;
  int64_t li = 0;
  while (OB_SUCC(ret) && li < len) {
    int64_t eol = li;
    while (eol < len && buf[eol] != '\n') { ++eol; }
    if (eol > li) {
      if (0 == r) { ret = sql.append("SELECT "); }
      else { ret = sql.append(" UNION ALL SELECT "); }
      for (int64_t m = 0; OB_SUCC(ret) && m < keys.count(); ++m) {
        if (m > 0) { ret = sql.append(", "); }
        FsJsonVal v;
        const bool found = fs_json_lookup(buf + li, eol - li, keys.at(m).ptr(), keys.at(m).length(), v);
        if (!found || 0 == v.kind) {
          ret = sql.append("NULL");
        } else if (2 == v.kind) {
          if (FS_BIGINT == types.at(m)) {
            ret = sql.append("CAST(");
            if (OB_SUCC(ret)) { ret = sql.append(v.p, v.n); }
            if (OB_SUCC(ret)) { ret = sql.append(" AS SIGNED)"); }
          } else {
            ret = sql.append("CAST(");
            if (OB_SUCC(ret)) { ret = sql.append(v.p, v.n); }
            if (OB_SUCC(ret)) { ret = sql.append(" AS DOUBLE)"); }
          }
        } else if (3 == v.kind) {
          ret = sql.append(0 == strncmp(v.p, "true", 4) ? "1" : "0");
        } else {
          ret = fs_append_quoted(sql, v.p, v.n);
        }
        if (OB_SUCC(ret) && 0 == r) {
          ret = sql.append(" AS `");
          if (OB_SUCC(ret)) { ret = sql.append(keys.at(m).ptr(), keys.at(m).length()); }
          if (OB_SUCC(ret)) { ret = sql.append("`"); }
        }
      }
      ++r;
    }
    li = eol + 1;
  }
  if (OB_SUCC(ret) && 0 == r) {
    ret = sql.append("SELECT NULL WHERE 1 = 0");
  }
  if (OB_SUCC(ret)) { ret = sql.append(")"); }
  return ret;
}

static int fs_build_glob_subquery(ObSqlString &sql, const char *pattern, int64_t plen)
{
  int ret = OB_SUCCESS;
  char dir[4096];
  int64_t slash = -1;
  for (int64_t i = plen - 1; i >= 0; --i) {
    if (pattern[i] == '/') { slash = i; break; }
  }
  if (slash < 0 || plen >= static_cast<int64_t>(sizeof(dir))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    MEMCPY(dir, pattern, slash);
    dir[slash] = '\0';
    const char *pat = pattern + slash + 1;
    const int64_t pat_len = plen - slash - 1;
    ObArray<ObString> names;
    char namebuf[65536];
    int64_t used = 0;
    DIR *d = ::opendir(dir);
    if (NULL == d) {
      ret = OB_IO_ERROR;
    } else {
      struct dirent *ent = NULL;
      while (OB_SUCC(ret) && NULL != (ent = ::readdir(d))) {
        const char *nm = ent->d_name;
        // skip "." / ".." and macOS AppleDouble side files ("._name")
        if ('.' == nm[0]) { continue; }
        const int64_t nlen = static_cast<int64_t>(strlen(nm));
        // only support trailing '*'
        bool match = false;
        if (pat_len > 0 && pat[pat_len - 1] == '*') {
          match = (nlen >= pat_len - 1) && (0 == strncmp(nm, pat, static_cast<size_t>(pat_len - 1)));
        } else {
          match = (nlen == pat_len) && (0 == strncmp(nm, pat, static_cast<size_t>(pat_len)));
        }
        if (!match) { continue; }
        if (used + nlen + 1 >= static_cast<int64_t>(sizeof(namebuf))) {
          ret = OB_SIZE_OVERFLOW;
          break;
        }
        MEMCPY(namebuf + used, nm, nlen);
        ret = names.push_back(ObString(static_cast<int32_t>(nlen), namebuf + used));
        used += nlen + 1;
      }
      ::closedir(d);
    }
    // sort names
    for (int64_t a = 0; OB_SUCC(ret) && a < names.count(); ++a) {
      for (int64_t b = a + 1; b < names.count(); ++b) {
        if (names.at(b).compare(names.at(a)) < 0) {
          ObString tmp = names.at(a);
          names.at(a) = names.at(b);
          names.at(b) = tmp;
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (0 == names.count()) {
        ret = sql.append("(SELECT NULL AS `file` WHERE 1 = 0)");
      } else {
        ret = sql.append("(");
        for (int64_t i = 0; OB_SUCC(ret) && i < names.count(); ++i) {
          if (i > 0) { ret = sql.append(" UNION ALL "); }
          ret = sql.append("SELECT ");
          if (OB_SUCC(ret)) { ret = fs_append_quoted(sql, names.at(i).ptr(), names.at(i).length()); }
          if (OB_SUCC(ret) && 0 == i) { ret = sql.append(" AS `file`"); }
        }
        if (OB_SUCC(ret)) { ret = sql.append(")"); }
      }
    }
  }
  return ret;
}

// ---- parquet ----
static int fs_snappy_uncompress(const uint8_t *src, int64_t slen,
                                uint8_t *dst, int64_t dcap, int64_t &dlen)
{
  int ret = OB_SUCCESS;
  int64_t ip = 0;
  int64_t op = 0;
  // varint uncompressed length
  uint64_t ulen = 0;
  int shift = 0;
  while (ip < slen) {
    const uint8_t b = src[ip++];
    ulen |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) { break; }
    shift += 7;
  }
  if (static_cast<int64_t>(ulen) > dcap) {
    ret = OB_SIZE_OVERFLOW;
  }
  while (OB_SUCC(ret) && ip < slen) {
    const uint8_t tag = src[ip++];
    const int kind = tag & 0x03;
    if (0 == kind) {
      int64_t lit = (tag >> 2) + 1;
      if (lit > 60) {
        const int extra = static_cast<int>(lit - 60);
        lit = 0;
        for (int k = 0; k < extra && ip < slen; ++k) {
          lit |= static_cast<int64_t>(src[ip++]) << (8 * k);
        }
        lit += 1;
      }
      if (ip + lit > slen || op + lit > static_cast<int64_t>(ulen)) {
        ret = OB_INVALID_DATA;
      } else {
        MEMCPY(dst + op, src + ip, lit);
        ip += lit;
        op += lit;
      }
    } else {
      int64_t len = 0;
      int64_t off = 0;
      if (1 == kind) {
        len = ((tag >> 2) & 0x07) + 4;
        off = ((static_cast<int64_t>(tag) & 0xE0) << 3) | (ip < slen ? src[ip++] : 0);
      } else if (2 == kind) {
        len = (tag >> 2) + 1;
        if (ip + 2 > slen) { ret = OB_INVALID_DATA; break; }
        off = static_cast<int64_t>(src[ip]) | (static_cast<int64_t>(src[ip + 1]) << 8);
        ip += 2;
      } else {
        len = (tag >> 2) + 1;
        if (ip + 4 > slen) { ret = OB_INVALID_DATA; break; }
        off = static_cast<int64_t>(src[ip]) | (static_cast<int64_t>(src[ip + 1]) << 8) |
              (static_cast<int64_t>(src[ip + 2]) << 16) | (static_cast<int64_t>(src[ip + 3]) << 24);
        ip += 4;
      }
      if (off <= 0 || off > op || op + len > static_cast<int64_t>(ulen)) {
        ret = OB_INVALID_DATA;
      } else {
        for (int64_t k = 0; k < len; ++k) {
          dst[op + k] = dst[op - off + k];
        }
        op += len;
      }
    }
  }
  if (OB_SUCC(ret)) {
    dlen = op;
  }
  return ret;
}

static int fs_thrift_varint(const uint8_t *p, int64_t len, int64_t &pos, uint64_t &v)
{
  int ret = OB_SUCCESS;
  v = 0;
  int shift = 0;
  bool done = false;
  while (pos < len && !done) {
    const uint8_t b = p[pos++];
    v |= static_cast<uint64_t>(b & 0x7F) << shift;
    if (!(b & 0x80)) { done = true; }
    shift += 7;
    if (shift > 63) { ret = OB_INVALID_DATA; }
  }
  if (!done) { ret = OB_INVALID_DATA; }
  return ret;
}

static inline int64_t fs_zigzag(uint64_t v)
{
  return static_cast<int64_t>((v >> 1) ^ (~(v & 1) + 1));
}

static int fs_thrift_skip(const uint8_t *p, int64_t len, int64_t &pos, int t);

static int fs_thrift_skip_struct(const uint8_t *p, int64_t len, int64_t &pos)
{
  int ret = OB_SUCCESS;
  int64_t last = 0;
  while (OB_SUCC(ret) && pos < len) {
    const uint8_t hdr = p[pos++];
    if (0 == hdr) { break; }
    const int delta = hdr >> 4;
    const int t = hdr & 0x0F;
    if (0 == delta) {
      uint64_t id = 0;
      if (OB_FAIL(fs_thrift_varint(p, len, pos, id))) { break; }
      last = fs_zigzag(id);
    } else {
      last += delta;
    }
    if (OB_FAIL(fs_thrift_skip(p, len, pos, t))) { break; }
  }
  return ret;
}

static int fs_thrift_skip(const uint8_t *p, int64_t len, int64_t &pos, int t)
{
  int ret = OB_SUCCESS;
  if (1 == t || 2 == t) {
    // bool encoded in type
  } else if (3 == t) {
    ++pos;
  } else if (4 == t || 5 == t || 6 == t) {
    uint64_t v = 0;
    ret = fs_thrift_varint(p, len, pos, v);
  } else if (7 == t) {
    pos += 8;
  } else if (8 == t) {
    uint64_t n = 0;
    if (OB_FAIL(fs_thrift_varint(p, len, pos, n))) {
    } else {
      pos += static_cast<int64_t>(n);
    }
  } else if (9 == t || 10 == t) {
    if (pos >= len) {
      ret = OB_INVALID_DATA;
    } else {
      const uint8_t hdr = p[pos++];
      uint64_t cnt = hdr >> 4;
      const int et = hdr & 0x0F;
      if (15 == cnt) {
        if (OB_FAIL(fs_thrift_varint(p, len, pos, cnt))) { return ret; }
      }
      for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
        ret = fs_thrift_skip(p, len, pos, et);
      }
    }
  } else if (11 == t) {
    if (pos >= len) {
      ret = OB_INVALID_DATA;
    } else {
      const uint8_t hdr = p[pos++];
      uint64_t cnt = hdr >> 4;
      const int kt = (hdr >> 4) & 0x0F;
      const int vt = hdr & 0x0F;
      (void)kt;
      if (15 == cnt) {
        if (OB_FAIL(fs_thrift_varint(p, len, pos, cnt))) { return ret; }
      }
      for (uint64_t i = 0; OB_SUCC(ret) && i < cnt; ++i) {
        ret = fs_thrift_skip(p, len, pos, vt);
        if (OB_SUCC(ret)) { ret = fs_thrift_skip(p, len, pos, vt); }
      }
    }
  } else if (12 == t) {
    ret = fs_thrift_skip_struct(p, len, pos);
  } else {
    ret = OB_INVALID_DATA;
  }
  return ret;
}

struct FsParquetCol
{
  FsParquetCol() : name(NULL), name_len(0), ptype(0), codec(0), num_values(0),
                   data_off(0), comp_size(0), path_buf() {}
  const char *name;
  int64_t name_len;
  int ptype;
  int codec;
  int64_t num_values;
  int64_t data_off;
  int64_t comp_size;
  char path_buf[128];
};

// Parses the parquet footer enough to locate the single INT column chunk.
static int fs_parquet_footer(const uint8_t *p, int64_t len, int64_t &num_rows, FsParquetCol &col)
{
  int ret = OB_SUCCESS;
  if (len < 12 || 0 != strncmp(reinterpret_cast<const char *>(p + len - 4), "PAR1", 4)) {
    ret = OB_INVALID_DATA;
  } else {
    const int32_t mlen = static_cast<int32_t>(p[len - 8] | (p[len - 7] << 8) | (p[len - 6] << 16) | (p[len - 5] << 24));
    if (mlen <= 0 || mlen > len - 8) {
      ret = OB_INVALID_DATA;
    } else {
      int64_t pos = len - 8 - mlen;
      int64_t last = 0;
      num_rows = 0;
      while (OB_SUCC(ret) && pos < len) {
        const uint8_t hdr = p[pos++];
        if (0 == hdr) { break; }
        const int delta = hdr >> 4;
        const int t = hdr & 0x0F;
        int64_t fid = 0;
        if (0 == delta) {
          uint64_t id = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, id))) { break; }
          fid = fs_zigzag(id);
        } else {
          fid = last + delta;
        }
        last = fid;
        if (6 == fid) {
          // created_by string: skip
          uint64_t n = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, n))) { break; }
          pos += static_cast<int64_t>(n);
        } else if (3 == fid) {
          uint64_t v = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
          num_rows = fs_zigzag(v);
        } else if (4 == fid && 9 == t) {
          // row_groups list<struct>
          const uint8_t lh = p[pos++];
          uint64_t cnt = lh >> 4;
          if (15 == cnt) {
            if (OB_FAIL(fs_thrift_varint(p, len, pos, cnt))) { break; }
          }
          for (uint64_t g = 0; OB_SUCC(ret) && g < cnt; ++g) {
            // RowGroup struct: parse fields 1(columns list),2,3
            int64_t glast = 0;
            while (OB_SUCC(ret) && pos < len) {
              const uint8_t gh = p[pos++];
              if (0 == gh) { break; }
              const int gd = gh >> 4;
              const int gt = gh & 0x0F;
              int64_t gid = 0;
              if (0 == gd) {
                uint64_t x = 0;
                if (OB_FAIL(fs_thrift_varint(p, len, pos, x))) { break; }
                gid = fs_zigzag(x);
              } else {
                gid = glast + gd;
              }
              glast = gid;
              if (1 == gid && 9 == gt) {
                const uint8_t ch = p[pos++];
                uint64_t ccnt = ch >> 4;
                if (15 == ccnt) {
                  if (OB_FAIL(fs_thrift_varint(p, len, pos, ccnt))) { break; }
                }
                for (uint64_t c = 0; OB_SUCC(ret) && c < ccnt; ++c) {
                  // ColumnChunk struct
                  int64_t clast = 0;
                  while (OB_SUCC(ret) && pos < len) {
                    const uint8_t chh = p[pos++];
                    if (0 == chh) { break; }
                    const int cd = chh >> 4;
                    const int ct = chh & 0x0F;
                    int64_t cid = 0;
                    if (0 == cd) {
                      uint64_t x = 0;
                      if (OB_FAIL(fs_thrift_varint(p, len, pos, x))) { break; }
                      cid = fs_zigzag(x);
                    } else {
                      cid = clast + cd;
                    }
                    clast = cid;
                    if (3 == cid && 12 == ct) {
                      // ColumnMetaData
                      int64_t mlast = 0;
                      while (OB_SUCC(ret) && pos < len) {
                        const uint8_t mh = p[pos++];
                        if (0 == mh) { break; }
                        const int md = mh >> 4;
                        const int mt = mh & 0x0F;
                        int64_t mid = 0;
                        if (0 == md) {
                          uint64_t x = 0;
                          if (OB_FAIL(fs_thrift_varint(p, len, pos, x))) { break; }
                          mid = fs_zigzag(x);
                        } else {
                          mid = mlast + md;
                        }
                        mlast = mid;
                        if (1 == mid) {
                          uint64_t v = 0;
                          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
                          col.ptype = static_cast<int>(fs_zigzag(v));
                        } else if (2 == mid) {
                          if (OB_FAIL(fs_thrift_skip(p, len, pos, mt))) { break; }
                        } else if (3 == mid && 9 == mt) {
                          const uint8_t lh2 = p[pos++];
                          uint64_t n2 = lh2 >> 4;
                          if (15 == n2) {
                            if (OB_FAIL(fs_thrift_varint(p, len, pos, n2))) { break; }
                          }
                          for (uint64_t k = 0; OB_SUCC(ret) && k < n2 && k < 1; ++k) {
                            uint64_t sl = 0;
                            if (OB_FAIL(fs_thrift_varint(p, len, pos, sl))) { break; }
                            if (sl < sizeof(col.path_buf)) {
                              MEMCPY(col.path_buf, p + pos, sl);
                              col.path_buf[sl] = '\0';
                              col.name = col.path_buf;
                              col.name_len = static_cast<int64_t>(sl);
                            }
                            pos += static_cast<int64_t>(sl);
                          }
                          for (uint64_t k = 1; OB_SUCC(ret) && k < n2; ++k) {
                            uint64_t sl = 0;
                            if (OB_FAIL(fs_thrift_varint(p, len, pos, sl))) { break; }
                            pos += static_cast<int64_t>(sl);
                          }
                        } else if (4 == mid) {
                          uint64_t v = 0;
                          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
                          col.codec = static_cast<int>(fs_zigzag(v));
                        } else if (5 == mid) {
                          uint64_t v = 0;
                          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
                          col.num_values = fs_zigzag(v);
                        } else if (7 == mid) {
                          uint64_t v = 0;
                          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
                          col.comp_size = fs_zigzag(v);
                        } else if (9 == mid) {
                          uint64_t v = 0;
                          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
                          col.data_off = fs_zigzag(v);
                        } else {
                          if (OB_FAIL(fs_thrift_skip(p, len, pos, mt))) { break; }
                        }
                      }
                    } else {
                      if (OB_FAIL(fs_thrift_skip(p, len, pos, ct))) { break; }
                    }
                  }
                }
              } else {
                if (OB_FAIL(fs_thrift_skip(p, len, pos, gt))) { break; }
              }
            }
          }
        } else {
          if (OB_FAIL(fs_thrift_skip(p, len, pos, t))) { break; }
        }
      }
    }
  }
  return ret;
}

// ---- parquet page + values ----
static int fs_parquet_page_header(const uint8_t *p, int64_t len, int64_t &pos,
                                  int &page_type, int64_t &comp_size, int64_t &num_values,
                                  int64_t &def_len, int64_t &rep_len, int &is_compressed)
{
  int ret = OB_SUCCESS;
  int64_t last = 0;
  page_type = -1;
  comp_size = 0;
  num_values = 0;
  def_len = 0;
  rep_len = 0;
  is_compressed = 1;
  while (OB_SUCC(ret) && pos < len) {
    const uint8_t hdr = p[pos++];
    if (0 == hdr) { break; }
    const int delta = hdr >> 4;
    const int t = hdr & 0x0F;
    int64_t fid = 0;
    if (0 == delta) {
      uint64_t id = 0;
      if (OB_FAIL(fs_thrift_varint(p, len, pos, id))) { break; }
      fid = fs_zigzag(id);
    } else {
      fid = last + delta;
    }
    last = fid;
    if (1 == fid) {
      uint64_t v = 0;
      if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
      page_type = static_cast<int>(fs_zigzag(v));
    } else if (3 == fid) {
      uint64_t v = 0;
      if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
      comp_size = fs_zigzag(v);
    } else if (8 == fid && 12 == t) {
      int64_t vlast = 0;
      while (OB_SUCC(ret) && pos < len) {
        const uint8_t vh = p[pos++];
        if (0 == vh) { break; }
        const int vd = vh >> 4;
        const int vt2 = vh & 0x0F;
        int64_t vid = 0;
        if (0 == vd) {
          uint64_t x = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, x))) { break; }
          vid = fs_zigzag(x);
        } else {
          vid = vlast + vd;
        }
        vlast = vid;
        if (1 == vid) {
          uint64_t v = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
          num_values = fs_zigzag(v);
        } else if (5 == vid) {
          uint64_t v = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
          def_len = fs_zigzag(v);
        } else if (6 == vid) {
          uint64_t v = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
          rep_len = fs_zigzag(v);
        } else if (7 == vid) {
          is_compressed = (1 == vt2 || 2 == vt2) ? (1 == vt2) : 1;
          if (3 == vt2 || 4 == vt2 || 5 == vt2 || 6 == vt2) {
            uint64_t v = 0;
            if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
            is_compressed = (0 != fs_zigzag(v));
          }
        } else {
          if (OB_FAIL(fs_thrift_skip(p, len, pos, vt2))) { break; }
        }
      }
    } else if (5 == fid && 12 == t) {
      int64_t dlast = 0;
      while (OB_SUCC(ret) && pos < len) {
        const uint8_t dh = p[pos++];
        if (0 == dh) { break; }
        const int dd = dh >> 4;
        const int dt = dh & 0x0F;
        int64_t did = 0;
        if (0 == dd) {
          uint64_t x = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, x))) { break; }
          did = fs_zigzag(x);
        } else {
          did = dlast + dd;
        }
        dlast = did;
        if (1 == did) {
          uint64_t v = 0;
          if (OB_FAIL(fs_thrift_varint(p, len, pos, v))) { break; }
          num_values = fs_zigzag(v);
        } else {
          if (OB_FAIL(fs_thrift_skip(p, len, pos, dt))) { break; }
        }
      }
    } else {
      if (OB_FAIL(fs_thrift_skip(p, len, pos, t))) { break; }
    }
  }
  return ret;
}

// Reads a single INT32 column from a PLAIN/OPTIONAL/RLE data page.
static int fs_build_parquet_subquery(ObSqlString &sql, char *fbuf, int64_t flen)
{
  int ret = OB_SUCCESS;
  const uint8_t *p = reinterpret_cast<const uint8_t *>(fbuf);
  int64_t num_rows = 0;
  FsParquetCol col;
  if (OB_FAIL(fs_parquet_footer(p, flen, num_rows, col))) {
    LOG_WARN("parquet footer parse failed", K(ret));
  } else if (NULL == col.name || col.data_off <= 0) {
    ret = OB_INVALID_DATA;
  } else {
    int64_t pos = col.data_off;
    int page_type = -1;
    int64_t comp_size = 0;
    int64_t page_values = 0;
    int64_t def_len = 0;
    int64_t rep_len = 0;
    int is_compressed = 1;
    if (OB_FAIL(fs_parquet_page_header(p, flen, pos, page_type, comp_size, page_values,
                                       def_len, rep_len, is_compressed))) {
    } else if ((0 != page_type && 3 != page_type) || pos + comp_size > flen) {
      ret = OB_INVALID_DATA;
    } else {
      const int64_t hdr_len = rep_len + def_len;
      const uint8_t *vsrc = p + pos + hdr_len;
      const int64_t vsrc_len = comp_size - hdr_len;
      uint8_t *plain = NULL;
      int64_t plain_len = 0;
      if (vsrc_len < 0) {
        ret = OB_INVALID_DATA;
      } else if (1 == col.codec && is_compressed) {
        const int64_t cap = vsrc_len * 8 + 1024;
        plain = static_cast<uint8_t *>(ob_malloc(cap, "FileSqlPq"));
        if (NULL == plain) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (OB_FAIL(fs_snappy_uncompress(vsrc, vsrc_len, plain, cap, plain_len))) {
        }
      } else if (0 == col.codec || !is_compressed) {
        plain_len = vsrc_len;
        plain = const_cast<uint8_t *>(vsrc);
      } else {
        ret = OB_NOT_SUPPORTED;
      }
      if (OB_SUCC(ret)) {
        // v1 pages prefix definition levels with a 4-byte length; v2 already split them
        int64_t vpos = 0;
        if (0 == page_type && def_len > 0 && plain_len >= 4) {
          const int64_t dlen = static_cast<int64_t>(plain[0]) |
                               (static_cast<int64_t>(plain[1]) << 8) |
                               (static_cast<int64_t>(plain[2]) << 16) |
                               (static_cast<int64_t>(plain[3]) << 24);
          vpos = 4 + dlen;
        }
        const int64_t n = (page_values > 0 ? page_values : col.num_values);
        if (OB_SUCC(ret) && vpos + n * 4 > plain_len) {
          ret = OB_INVALID_DATA;
        }
        if (OB_SUCC(ret)) {
          ret = sql.append("(");
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < n; ++i) {
          const int32_t v = static_cast<int32_t>(plain[vpos] | (plain[vpos + 1] << 8) |
                                                 (plain[vpos + 2] << 16) | (plain[vpos + 3] << 24));
          vpos += 4;
          if (i > 0) { ret = sql.append(" UNION ALL "); }
          if (OB_SUCC(ret)) { ret = sql.append("SELECT "); }
          if (OB_SUCC(ret)) {
            char num[32];
            const int nl = snprintf(num, sizeof(num), "%d", v);
            ret = sql.append("CAST(");
            if (OB_SUCC(ret)) { ret = sql.append(num, nl); }
            if (OB_SUCC(ret)) { ret = sql.append(" AS SIGNED) AS `"); }
          }
          if (OB_SUCC(ret)) { ret = sql.append(col.name, col.name_len); }
          if (OB_SUCC(ret)) { ret = sql.append("`"); }
        }
        if (OB_SUCC(ret)) { ret = sql.append(")"); }
      }
      if (1 == col.codec && NULL != plain) {
        ob_free(plain);
      }
    }
  }
  return ret;
}

static const char *fs_type_name(int t)
{
  switch (t) {
    case FS_BIGINT: return "BIGINT";
    case FS_DOUBLE: return "DOUBLE";
    case FS_BOOL: return "BOOLEAN";
    case FS_DATE: return "DATE";
    case FS_DATETIME: return "DATETIME";
    default: return "VARCHAR";
  }
}

// DUckDB style DESCRIBE SELECT output
static int fs_build_describe(ObSqlString &sql,
                             ObArray<ObString> &names,
                             ObArray<int> &types,
                             ObArray<bool> &nullable)
{
  int ret = OB_SUCCESS;
  for (int64_t c = 0; OB_SUCC(ret) && c < names.count(); ++c) {
    if (0 == c) {
      ret = sql.append("SELECT ");
    } else {
      ret = sql.append(" UNION ALL SELECT ");
    }
    ObString &nm = names.at(c);
    if (OB_SUCC(ret)) { ret = fs_append_quoted(sql, nm.ptr(), nm.length()); }
    if (OB_SUCC(ret)) { ret = sql.append(" AS `column_name`, "); }
    if (OB_SUCC(ret)) {
      const char *tn = fs_type_name(types.at(c));
      ret = fs_append_quoted(sql, tn, static_cast<int64_t>(strlen(tn)));
    }
    if (OB_SUCC(ret)) { ret = sql.append(" AS `column_type`, "); }
    if (OB_SUCC(ret)) {
      ret = sql.append(nullable.at(c) ? "'YES'" : "'NO'");
    }
    if (OB_SUCC(ret)) { ret = sql.append(" AS `null`"); }
    if (OB_SUCC(ret)) {
      ret = sql.append(0 == c ? ", NULL AS `key`, NULL AS `default`, NULL AS `extra`"
                              : ", NULL, NULL, NULL");
    }
  }
  if (OB_SUCC(ret) && 0 == names.count()) {
    ret = sql.append("SELECT NULL AS `column_name`, NULL AS `column_type`, NULL AS `null`,"
                     " NULL AS `key`, NULL AS `default`, NULL AS `extra` WHERE 1 = 0");
  }
  return ret;
}


// ---- entry: scan and rewrite ----
static bool fs_is_file_func(const char *p, int64_t n, int64_t &kind)
{
  kind = -1;
  if (8 == n && 0 == strncasecmp(p, "read_csv", 8)) { kind = 0; return true; }
  if (9 == n && 0 == strncasecmp(p, "read_json", 9)) { kind = 1; return true; }
  if (12 == n && 0 == strncasecmp(p, "read_parquet", 12)) { kind = 2; return true; }
  if (4 == n && 0 == strncasecmp(p, "glob", 4)) { kind = 3; return true; }
  return false;
}

static bool fs_word_is(const char *p, int64_t n, const char *lit)
{
  const int64_t l = static_cast<int64_t>(strlen(lit));
  return n == l && 0 == strncasecmp(p, lit, static_cast<size_t>(n));
}

// Does an alias follow position 'pos'?
static bool fs_has_alias(const char *s, int64_t len, int64_t pos)
{
  int64_t i = fs_skip_ws(s, len, pos);
  if (i >= len) { return false; }
  if (!fs_is_alpha(s[i]) && s[i] != '`') { return false; }
  int64_t b = i;
  if (s[i] == '`') {
    ++i;
    while (i < len && s[i] != '`') { ++i; }
    return true;
  }
  while (i < len && fs_is_word_char(s[i])) { ++i; }
  const int64_t n = i - b;
  static const char *kw[] = {"where", "order", "group", "having", "limit", "on", "join",
                             "inner", "left", "right", "full", "cross", "union", "using",
                             "for", "natural", "straight_join", "lateral", "offset"};
  for (int64_t k = 0; k < static_cast<int64_t>(sizeof(kw) / sizeof(kw[0])); ++k) {
    if (fs_word_is(s + b, n, kw[k])) { return false; }
  }
  return true;
}

// Parses the argument list between lparen+1 and rparen-1 for read_csv options.
static int fs_check_csv_args(const char *s, int64_t begin, int64_t end, ObString &format_out)
{
  int ret = OB_SUCCESS;
  format_out.reset();
  int64_t i = fs_skip_ws(s, end, begin);
  // skip first argument (the path)
  int64_t depth = 0;
  bool sq = false;
  while (i < end) {
    const char c = s[i];
    if (sq) {
      if (c == '\'') {
        if (i + 1 < end && s[i + 1] == '\'') { ++i; } else { sq = false; }
      }
    } else if (c == '\'') { sq = true; }
    else if (c == '(') { ++depth; }
    else if (c == ')') { --depth; }
    else if (',' == c && 0 == depth) { ++i; break; }
    ++i;
  }
  bool first_extra = true;
  while (OB_SUCC(ret) && i < end) {
    i = fs_skip_ws(s, end, i);
    if (i >= end || s[i] == ')') { break; }
    // read optional "name =" prefix
    int64_t wb = i;
    while (i < end && fs_is_word_char(s[i])) { ++i; }
    const int64_t wlen = i - wb;
    int64_t j = fs_skip_ws(s, end, i);
    const bool named = (j < end && s[j] == '=');
    if (named) {
      if (fs_word_is(s + wb, wlen, "sample_size")) {
        // only sample_size = -1 is accepted
        int64_t k = fs_skip_ws(s, end, j + 1);
        int64_t vb = k;
        while (k < end && s[k] != ',' && s[k] != ')') { ++k; }
        int64_t ve = k;
        while (ve > vb && (s[ve - 1] == ' ' || s[ve - 1] == '\t')) { --ve; }
        if (!(2 == (ve - vb) && s[vb] == '-' && s[vb + 1] == '1')) {
          LOG_USER_ERROR(OB_NOT_SUPPORTED, "file sql sample_size");
          ret = OB_NOT_SUPPORTED;
        }
        i = k;
      } else if (fs_word_is(s + wb, wlen, "header")) {
        int64_t k = fs_skip_ws(s, end, j + 1);
        while (k < end && s[k] != ',' && s[k] != ')') { ++k; }
        i = k;
      } else {
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "file sql option");
        ret = OB_NOT_SUPPORTED;
      }
    } else {
      // positional extra argument: only a format string is allowed
      i = fs_skip_ws(s, end, wb);
      if (i < end && s[i] == '\'') {
        char tmp[64];
        int64_t tlen = 0;
        const int64_t e2 = end;
        int64_t k = i + 1;
        while (k < e2 && s[k] != '\'' && tlen < static_cast<int64_t>(sizeof(tmp))) {
          tmp[tlen++] = s[k++];
        }
        if (first_extra && !(4 == tlen && 0 == strncasecmp(tmp, "csv", 3))) {
          ret = OB_ERR_PARSE_SQL;
        }
        i = (k < e2) ? k + 1 : k;
      } else {
        ret = OB_ERR_PARSE_SQL;
      }
    }
    first_extra = false;
    i = fs_skip_ws(s, end, i);
    if (i < end && s[i] == ',') { ++i; }
  }
  return ret;
}


static int fs_rewrite(ObIAllocator &alloc, ObSQLSessionInfo &session,
                      const ObString &sql, ObString &out);

static bool fs_is_copy_stmt(const char *s, int64_t len)
{
  const int64_t i = fs_skip_ws(s, len, 0);
  if (i + 4 > len) { return false; }
  if (0 != strncasecmp(s + i, "copy", 4)) { return false; }
  const int64_t j = fs_skip_ws(s, len, i + 4);
  return j < len && s[j] == '(';
}

// COPY (query) TO 'path' [(options)] -> SELECT * FROM (header UNION ALL query) t
//                                      INTO OUTFILE 'path' FIELDS TERMINATED BY ','
static int fs_rewrite_copy(ObIAllocator &alloc, ObSQLSessionInfo &session,
                           const char *s, int64_t len, ObString &out)
{
  int ret = OB_SUCCESS;
  int64_t i = fs_skip_ws(s, len, 0);
  i = fs_skip_ws(s, len, i + 4);
  const int64_t rp = fs_match_paren(s, len, i);
  if (rp < 0) {
    ret = OB_ERR_PARSE_SQL;
  } else {
    const int64_t qb = i + 1;
    const int64_t qe = rp;
    int64_t k = fs_skip_ws(s, len, rp + 1);
    if (k + 2 > len || 0 != strncasecmp(s + k, "to", 2)) {
      ret = OB_ERR_PARSE_SQL;
    } else {
      k = fs_skip_ws(s, len, k + 2);
      if (k >= len || s[k] != '\'') {
        ret = OB_ERR_PARSE_SQL;
      } else {
        int64_t pb = ++k;
        while (k < len && s[k] != '\'') { ++k; }
        if (k >= len) {
          ret = OB_ERR_PARSE_SQL;
        } else {
          const ObString path(static_cast<int32_t>(k - pb), s + pb);
          // header names from the inner query's select list
          ObSqlString hdr;
          int64_t a = fs_skip_ws(s, qe, qb);
          if (a + 6 <= qe && 0 == strncasecmp(s + a, "select", 6)) {
            int64_t b = fs_skip_ws(s, qe, a + 6);
            int64_t depth = 0;
            bool sq = false;
            int64_t from_pos = -1;
            for (int64_t q = b; q < qe; ++q) {
              const char ch = s[q];
              if (sq) {
                if (ch == '\'') { sq = false; }
                continue;
              }
              if (ch == '\'') { sq = true; }
              else if (ch == '(') { ++depth; }
              else if (ch == ')') { --depth; }
              else if (0 == depth && (q + 4 <= qe) && 0 == strncasecmp(s + q, "from", 4)) {
                from_pos = q;
                break;
              }
            }
            if (from_pos > b) {
              int64_t item_start = b;
              int64_t d2 = 0;
              bool sq2 = false;
              bool first = true;
              for (int64_t q = b; q <= from_pos; ++q) {
                const char ch = (q < from_pos) ? s[q] : ',';
                if (sq2) {
                  if (ch == '\'') { sq2 = false; }
                  continue;
                }
                if (ch == '\'') { sq2 = true; continue; }
                if (ch == '(') { ++d2; continue; }
                if (ch == ')') { --d2; continue; }
                if (ch == ',' && 0 == d2) {
                  int64_t is = item_start;
                  int64_t ie = q;
                  while (is < ie && (s[is] == ' ' || s[is] == '\t' ||
                                     s[is] == '\n' || s[is] == '\r')) { ++is; }
                  while (ie > is && (s[ie - 1] == ' ' || s[ie - 1] == '\t' ||
                                     s[ie - 1] == '\n' || s[ie - 1] == '\r')) { --ie; }
                  // strip trailing alias "AS x"
                  int64_t nm_end = ie;
                  for (int64_t q2 = is; q2 + 2 <= ie; ++q2) {
                    if (0 == strncasecmp(s + q2, "as", 2) &&
                        (q2 == is || s[q2 - 1] == ' ' || s[q2 - 1] == '\t')) {
                      nm_end = q2;
                      while (nm_end > is && (s[nm_end - 1] == ' ' || s[nm_end - 1] == '\t')) { --nm_end; }
                      break;
                    }
                  }
                  // take last identifier of the item
                  int64_t idb = nm_end;
                  while (idb > is && (fs_is_word_char(s[idb - 1]) || s[idb - 1] == '`')) { --idb; }
                  int64_t ide = nm_end;
                  while (ide > idb && s[ide - 1] == '`') { --ide; }
                  if (!first && OB_SUCC(ret)) { ret = hdr.append(", "); }
                  if (OB_SUCC(ret) && ide > idb) {
                    ret = fs_append_quoted(hdr, s + idb, ide - idb);
                  }
                  first = false;
                  item_start = q + 1;
                }
              }
            }
          }
          ObString inner(static_cast<int32_t>(qe - qb), s + qb);
          ObString inner_rw;
          if (OB_SUCC(ret)) {
            ret = fs_rewrite(alloc, session, inner, inner_rw);
          }
          const ObString &body = inner_rw.empty() ? inner : inner_rw;
          if (OB_SUCC(ret)) {
            ObSqlString res;
            if (hdr.length() > 0) {
              ret = res.append("SELECT * FROM (SELECT ");
              if (OB_SUCC(ret)) { ret = res.append(hdr.ptr(), hdr.length()); }
              if (OB_SUCC(ret)) { ret = res.append(" UNION ALL SELECT * FROM ("); }
              if (OB_SUCC(ret)) { ret = res.append(body.ptr(), body.length()); }
              if (OB_SUCC(ret)) { ret = res.append(") file_sql_inner) file_sql_copy"); }
            } else {
              ret = res.append("SELECT * FROM (");
              if (OB_SUCC(ret)) { ret = res.append(body.ptr(), body.length()); }
              if (OB_SUCC(ret)) { ret = res.append(") file_sql_copy"); }
            }
            if (OB_SUCC(ret)) { ret = res.append(" INTO OUTFILE '"); }
            if (OB_SUCC(ret)) { ret = res.append(path.ptr(), path.length()); }
            if (OB_SUCC(ret)) { ret = res.append("' FIELDS TERMINATED BY ','"); }
            if (OB_SUCC(ret)) {
              
              char *dst = static_cast<char *>(alloc.alloc(res.length() + 1));
              if (NULL == dst) {
                ret = OB_ALLOCATE_MEMORY_FAILED;
              } else {
                MEMCPY(dst, res.ptr(), res.length());
                dst[res.length()] = '\0';
                out.assign_ptr(dst, static_cast<int32_t>(res.length()));
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

static int fs_rewrite(ObIAllocator &alloc, ObSQLSessionInfo &session,
                      const ObString &sql, ObString &out)
{
  int ret = OB_SUCCESS;
  out.reset();
  const char *s = sql.ptr();
  const int64_t len = sql.length();
  if (NULL == s || len <= 0) {
    ret = OB_SUCCESS;
  } else if (fs_is_copy_stmt(s, len)) {
    if (OB_FAIL(fs_rewrite_copy(alloc, session, s, len, out))) {
    }
  } else {
    // locate first file function token
    int64_t first = -1;
    int64_t first_kind = -1;
    bool sq = false;
    bool dq = false;
    bool bt = false;
    bool line_cmt = false;
    for (int64_t i = 0; i < len && first < 0 && OB_SUCC(ret); ++i) {
      const char c = s[i];
      if (line_cmt) {
        if (c == '\n') { line_cmt = false; }
        continue;
      }
      if (sq) {
        if (c == '\\' && i + 1 < len) { ++i; }
        else if (c == '\'') { sq = false; }
        continue;
      }
      if (dq) {
        if (c == '\\' && i + 1 < len) { ++i; }
        else if (c == '"') { dq = false; }
        continue;
      }
      if (bt) {
        if (c == '`') { bt = false; }
        continue;
      }
      if (c == '-') {
        if (i + 1 < len && s[i + 1] == '-') { line_cmt = true; ++i; }
        continue;
      }
      if (c == '\'') { sq = true; continue; }
      if (c == '"') { dq = true; continue; }
      if (c == '`') { bt = true; continue; }
      if (!fs_is_alpha(c)) { continue; }
      const int64_t wb = i;
      while (i + 1 < len && fs_is_word_char(s[i + 1])) { ++i; }
      const int64_t wlen = i - wb + 1;
      const bool prev_ok = (wb == 0) || !fs_is_word_char(s[wb - 1]);
      int64_t kind = -1;
      if (prev_ok && fs_is_file_func(s + wb, wlen, kind)) {
        const int64_t j = fs_skip_ws(s, len, i + 1);
        if (j < len && s[j] == '(') { first = wb; first_kind = kind; }
      }
    }
    if (first < 0) {
      ret = OB_SUCCESS;
    } else {
      bool enabled = false;
      if (OB_FAIL(session.get_sys_variable(share::SYS_VAR_ENABLE_FILE_SQL, enabled))) {
      } else if (!enabled) {
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "file sql");
        ret = OB_NOT_SUPPORTED;
      } else {
        ObSqlString res;
        int64_t pos = 0;
        int64_t i = first;
        int64_t kind = first_kind;
        bool is_describe = false;
        {
          const int64_t qi = fs_skip_ws(s, len, 0);
          if (qi + 8 <= len && 0 == strncasecmp(s + qi, "describe", 8)) {
            is_describe = true;
          } else if (qi + 4 <= len && 0 == strncasecmp(s + qi, "desc", 4)) {
            is_describe = true;
          }
        }
        while (OB_SUCC(ret) && i < len) {
          // find the '(' after the name
          int64_t k = i;
          while (k < len && fs_is_word_char(s[k])) { ++k; }
          k = fs_skip_ws(s, len, k);
          if (k >= len || s[k] != '(') { break; }
          const int64_t rp = fs_match_paren(s, len, k);
          if (rp < 0) {
            ret = OB_ERR_PARSE_SQL;
            break;
          }
          if (!is_describe && OB_FAIL(res.append(s + pos, i - pos))) { break; }
          char path[4096];
          int64_t plen = 0;
          if (OB_FAIL(fs_first_string_arg(s, k + 1, rp, path, sizeof(path) - 1, plen))) {
            ret = OB_ERR_PARSE_SQL;
            break;
          }
          ObString fmt;
          if (0 == kind && OB_FAIL(fs_check_csv_args(s, k + 1, rp, fmt))) {
            break;
          }
          char *fbuf = NULL;
          int64_t flen = 0;
          if ((0 == kind || 1 == kind || 2 == kind) && OB_FAIL(fs_read_file(path, plen, fbuf, flen))) {
            break;
          }
          if (0 == kind) {
            FsCsv csv;
            ObSqlString sub;
            if (OB_FAIL(fs_parse_csv(fbuf, flen, csv))) {
            } else if (is_describe) {
              ObSqlString dummy;
              if (OB_FAIL(fs_build_csv_subquery(dummy, csv, &sub))) {
              }
            } else if (OB_FAIL(fs_build_csv_subquery(sub, csv, NULL))) {
            }
            if (OB_SUCC(ret)) {
              ret = res.append(sub.ptr(), sub.length());
            }
            if (NULL != csv.off_) { ob_free(csv.off_); }
            if (OB_SUCC(ret) && is_describe) {
              pos = len;
            }
          } else if (1 == kind) {
            ObSqlString sub;
            if (OB_FAIL(fs_build_jsonl_subquery(sub, fbuf, flen))) {
            } else if (OB_FAIL(res.append(sub.ptr(), sub.length()))) {
            }
            if (OB_SUCC(ret) && is_describe) { pos = len; }
          } else if (2 == kind) {
            ObSqlString sub;
            if (OB_FAIL(fs_build_parquet_subquery(sub, fbuf, flen))) {
            } else if (OB_FAIL(res.append(sub.ptr(), sub.length()))) {
            }
            if (OB_SUCC(ret) && is_describe) { pos = len; }
          } else if (3 == kind) {
            ObSqlString sub;
            if (OB_FAIL(fs_build_glob_subquery(sub, path, plen))) {
            } else if (OB_FAIL(res.append(sub.ptr(), sub.length()))) {
            }
            if (OB_SUCC(ret) && is_describe) { pos = len; }
          } else {
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "file sql function");
            ret = OB_NOT_SUPPORTED;
          }
          if (NULL != fbuf) { ob_free(fbuf); }
          if (OB_FAIL(ret)) { break; }
          if (!is_describe && !fs_has_alias(s, len, rp + 1)) {
            if (OB_FAIL(res.append(" file_sql_t"))) { break; }
          }
          pos = rp + 1;
          // find next call
          int64_t next = -1;
          int64_t next_kind = -1;
          bool sq2 = false, dq2 = false, bt2 = false, cmt2 = false;
          for (int64_t t = pos; t < len && next < 0; ++t) {
            const char c2 = s[t];
            if (cmt2) { if (c2 == '\n') { cmt2 = false; } continue; }
            if (sq2) {
              if (c2 == '\\' && t + 1 < len) { ++t; }
              else if (c2 == '\'') { sq2 = false; }
              continue;
            }
            if (dq2) {
              if (c2 == '\\' && t + 1 < len) { ++t; }
              else if (c2 == '"') { dq2 = false; }
              continue;
            }
            if (bt2) { if (c2 == '`') { bt2 = false; } continue; }
            if (c2 == '-' && t + 1 < len && s[t + 1] == '-') { cmt2 = true; ++t; continue; }
            if (c2 == '\'') { sq2 = true; continue; }
            if (c2 == '"') { dq2 = true; continue; }
            if (c2 == '`') { bt2 = true; continue; }
            if (!fs_is_alpha(c2)) { continue; }
            const int64_t wb2 = t;
            while (t + 1 < len && fs_is_word_char(s[t + 1])) { ++t; }
            const int64_t wl2 = t - wb2 + 1;
            int64_t kd2 = -1;
            if ((wb2 == 0 || !fs_is_word_char(s[wb2 - 1])) && fs_is_file_func(s + wb2, wl2, kd2)) {
              const int64_t j2 = fs_skip_ws(s, len, t + 1);
              if (j2 < len && s[j2] == '(') { next = wb2; next_kind = kd2; }
            }
          }
          if (next < 0) {
            if (OB_FAIL(res.append(s + pos, len - pos))) { break; }
            pos = len;
            break;
          }
          i = next;
          kind = next_kind;
        }
        if (OB_SUCC(ret) && pos < len && res.length() == 0) {
          ret = OB_SUCCESS;
        }
        if (OB_SUCC(ret)) {
          if (res.length() > 0) {
            char *dst = static_cast<char *>(alloc.alloc(res.length() + 1));
            if (NULL == dst) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else {
              MEMCPY(dst, res.ptr(), res.length());
              dst[res.length()] = '\0';
              out.assign_ptr(dst, static_cast<int32_t>(res.length()));
            }
          }
        }
      }
    }
  }
  return ret;
}


} // namespace filesql

int ObSql::stmt_query(const common::ObString &stmt, ObSqlCtx &context, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  LinkExecCtxGuard link_guard(result.get_session(), result.get_exec_context());
  ObTraceSpanGuard compile_span(&result.get_session(), TRACE_SQL_COMPILE);
  ObTruncatedString trunc_stmt(stmt);
#ifndef NDEBUG
  LOG_INFO("Begin to handle text statement",
           "sess_id", result.get_session().get_server_sid(),
           "execution_id", result.get_session().get_current_execution_id(),
           K(trunc_stmt));
#endif
  NG_TRACE(parse_begin);
  //1 check inited
  if (OB_FAIL(sanity_check(context))) {
  } else if (OB_FAIL(handle_text_query(stmt, context, result))) {
    if (OB_EAGAIN != ret) {
    }
  }
  //LOG_DEBUG("result errno", N_ERR_CODE, result.get_errcode(), K(ret));
  if (OB_SUCCESS != ret
      && OB_SUCCESS == result.get_errcode()) {
    result.set_errcode(ret);
  }
  if (OB_ISNULL(result.get_physical_plan())) {
  } else {
  }
  NG_TRACE_EXT(stmt_query_end, OB_ID(stmt),
               context.is_sensitive_ ? ObString(OB_MASKED_STR) : trunc_stmt.string(),
               OB_ID(stmt_len), stmt.length());

  if (OB_FAIL(ret)) {
    rollback_implicit_trans_when_fail(result, ret);
  }
  return ret;
}

int ObSql::stmt_execute(const ObPsStmtId stmt_id,
                        const stmt::StmtType stmt_type,
                        const ParamStore &params,
                        ObSqlCtx &context,
                        ObResultSet &result,
                        bool is_inner_sql)
{
  int ret = OB_SUCCESS;
  LinkExecCtxGuard link_guard(result.get_session(), result.get_exec_context());
  if (OB_FAIL(sanity_check(context))) {
  } else if (OB_FAIL(init_result_set(context, result))) {
  } else if (OB_FAIL(handle_ps_execute(stmt_id, stmt_type, params, context, result, is_inner_sql))) {
  }
  if (OB_FAIL(ret) && OB_SUCCESS == result.get_errcode()) {
    result.set_errcode(ret);
  }
  if (OB_FAIL(ret)) {
    rollback_implicit_trans_when_fail(result, ret);
  }
  return ret;
}


int ObSql::fill_result_set(ObResultSet &result_set,
                           ObSqlCtx *context,
                           const PlanCacheMode mode,
                           ObStmt &basic_stmt)
{
  int ret = OB_SUCCESS;
  ObStmt *stmt = &basic_stmt;
  ObExecContext &ectx = result_set.get_exec_context();
  ObPhysicalPlanCtx *pctx = ectx.get_physical_plan_ctx();
  if (OB_UNLIKELY(NULL == context) || OB_UNLIKELY(NULL == context->session_info_) || OB_UNLIKELY(NULL == pctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    result_set.set_affected_rows(0);
    result_set.set_warning_count(0);
    result_set.set_message("");
    ObString type_name = ObString::make_string("varchar");
    number::ObNumber number;
    number.set_zero();
    ObField field;
    common::ObIAllocator &alloc = result_set.get_mem_pool();
    ObCharsetType result_charset = CHARSET_INVALID;
    ObCollationType collation_type = CS_TYPE_INVALID;
    context->session_info_->get_character_set_results(result_charset);
    collation_type =
        ObCharset::get_default_collation_by_mode(result_charset);
    switch (stmt->get_stmt_type()) {
    case stmt::T_SELECT: {
      if (OB_FAIL(fill_select_result_set(result_set, context, mode, collation_type, type_name,
                                         basic_stmt, field))) {
      }
      break;
    }
    case stmt::T_INSERT:
    case stmt::T_REPLACE:
    case stmt::T_UPDATE:
    case stmt::T_DELETE: {
      break;
    }
    case stmt::T_EXPLAIN: {
      ObString tname = ObString::make_string("explain_table");
      ObString cname = ObString::make_string("Query Plan");
      field.tname_ = tname;
      field.org_tname_ = tname;
      field.cname_ = cname;
      field.org_cname_ = cname;
      field.type_.set_type(ObVarcharType);
      field.charsetnr_ = CS_TYPE_UTF8MB4_GENERAL_CI;
      field.type_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
      field.type_.set_collation_level(CS_LEVEL_IMPLICIT);
      field.type_.set_varchar(type_name);
      if (OB_FAIL(result_set.reserve_field_columns(1))) {
      } else if (OB_FAIL(result_set.add_field_column(field))) {
      }
      break;
    }
    case stmt::T_CALL_PROCEDURE: {
      ObCallProcedureStmt &call_stmt = static_cast<ObCallProcedureStmt&>(basic_stmt);
      ObString tname = ObString::make_string("procedure");
      if (NULL == call_stmt.get_call_proc_info()) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int64_t size = call_stmt.get_call_proc_info()->get_output_count();
        field.charsetnr_ = CS_TYPE_UTF8MB4_GENERAL_CI;

        if (0 == size) {
          break;
        }

        if (OB_SUCC(ret) && OB_FAIL(result_set.reserve_field_columns(size))) {
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
          if (!call_stmt.get_call_proc_info()->is_client_out_param_by_out_param_id(i)) {
            // only return client out param to client
            continue;
          }
          ObCollationType charsetnr;
          const ObDataType *type = call_stmt.get_call_proc_info()->get_out_type().at(i).get_data_type();
          ObObjType out_obj_type = call_stmt.get_call_proc_info()->get_out_type().at(i).get_obj_type();
          if (ObUnknownType == out_obj_type
            || ObExtendType == out_obj_type) {
            // do nothing ...
          } else if (OB_NOT_NULL(type)) {
            if (ob_is_string_or_lob_type(out_obj_type)
                  && CS_TYPE_ANY == type->get_collation_type()) {
              charsetnr = ObCharset::is_valid_collation(collation_type)
                            ? collation_type
                            : CS_TYPE_UTF8MB4_BIN;
            } else {
              OZ (ObCharset::get_default_collation(type->get_collation_type(), charsetnr));
            }
            OX (field.charsetnr_ = static_cast<uint16_t>(charsetnr));
          }
          if (OB_SUCC(ret)) {
            field.type_.set_type(out_obj_type);
            if (OB_NOT_NULL(type)) {
              field.accuracy_ = type->get_accuracy();
              // Setup Collation and Collation levl
              if (ob_is_string_or_lob_type(out_obj_type)
                  || ob_is_enum_or_set_type(out_obj_type)) {
                field.type_.set_collation_type(type->get_collation_type());
                field.type_.set_collation_level(type->get_collation_level());
              }
            }
            if (ObExtendType == out_obj_type) {
              field.length_ = field.accuracy_.get_length();
            } else if (ObCharType == out_obj_type
                      || ObVarcharType == out_obj_type) {
              if (-1 == field.accuracy_.get_length()) {
                field.length_ = ObCharType == out_obj_type ?
                  OB_MAX_EXTENDED_CHAR_LENGTH_BYTE : OB_MAX_EXTENDED_VARCHAR_LENGTH;
              } else {
                field.length_ = field.accuracy_.get_length();
              }
            } else if (ob_is_enum_or_set_type(out_obj_type)) {
              CK (OB_NOT_NULL(type));
              OZ (common::ObField::get_field_mb_length(field.type_.get_type(),
                                                      field.accuracy_,
                                                      type->get_collation_type(),
                                                      field.length_));
              OX (field.type_.set_type(ObVarcharType));
            } else {
              OZ (common::ObField::get_field_mb_length(field.type_.get_type(),
                                                      field.accuracy_,
                                                      common::CS_TYPE_INVALID,
                                                      field.length_));
            }
            // Setup Scale
            if (OB_SUCC(ret)) {
              if (ObVarcharType == field.type_.get_type()) {
                field.type_.set_varchar(type_name);
              } else if (ObNumberType == field.type_.get_type()) {
                field.type_.set_number(number);
              }
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(ob_write_string(alloc, tname, field.tname_))) {
            } else if (OB_FAIL(ob_write_string(alloc, tname, field.org_tname_))) {
            } else if (OB_FAIL(ob_write_string(alloc, call_stmt.get_call_proc_info()->get_out_name().at(i), field.cname_))) {
            } else if (OB_FAIL(ob_write_string(alloc, call_stmt.get_call_proc_info()->get_out_name().at(i), field.org_cname_))) {
            } else if (OB_FAIL(ob_write_string(alloc, call_stmt.get_call_proc_info()->get_out_type_name().at(i), field.type_name_))) {
            } else if (OB_FAIL(ob_write_string(alloc, call_stmt.get_call_proc_info()->get_out_type_owner().at(i), field.type_owner_))) {
            } else { /*do nothing*/ }
          }

          if (OB_SUCC(ret)) {
            if (OB_FAIL(result_set.add_field_column(field))) {
            }
          }

          if (OB_SUCC(ret)) {
            field.cname_.assign(NULL, 0);
            field.org_cname_.assign(NULL, 0);
            field.dname_.assign(NULL, 0);
            field.tname_.assign(NULL, 0);
            field.org_tname_.assign(NULL, 0);
            field.type_.reset();
            field.type_.set_type(ObExtendType);
            field.type_name_.assign(NULL, 0);
            field.type_owner_.assign(NULL, 0);
          }
        }
        break;
      }
    }
    default:
      break;
    }

    const int64_t question_marks_count = pctx->get_ps_fixed_array_index() != nullptr && stmt::T_ANONYMOUS_BLOCK != stmt->get_stmt_type() ?
          pctx->get_orig_question_mark_cnt() : stmt->get_query_ctx()->get_prepare_param_count();
    // param column is only needed in ps mode
    if (OB_SUCC(ret) && question_marks_count > 0
        && (PC_PS_MODE == mode || PC_PL_MODE == mode)) {
      if (OB_FAIL(result_set.reserve_param_columns(question_marks_count))) {
      }
      ObAnonymousBlockStmt *anonymous_stmt = NULL;
      ObCallProcedureStmt *call_stmt = NULL;
      if (stmt::T_ANONYMOUS_BLOCK == stmt->get_stmt_type()) {
        CK (OB_NOT_NULL(anonymous_stmt = static_cast<ObAnonymousBlockStmt *>(stmt)));
        OZ (result_set.reserve_field_columns(anonymous_stmt->get_out_idx().num_members()));
      } else if (stmt::T_CALL_PROCEDURE == stmt->get_stmt_type()) {
        CK (OB_NOT_NULL(call_stmt = static_cast<ObCallProcedureStmt *>(stmt)));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < question_marks_count; ++i) {
        ObField param_field;
        param_field.type_.set_type(ObIntType); // @bug
        param_field.cname_ = ObString::make_string("?");
        if (OB_NOT_NULL(pctx->get_ps_fixed_array_index()) &&
            OB_NOT_NULL(anonymous_stmt) &&
            is_exist_in_fixed_param_idx(i, *pctx->get_ps_fixed_array_index())) {
          continue;
        } else if (OB_NOT_NULL(anonymous_stmt) && anonymous_stmt->get_out_idx().has_member(i)) {
          ObField column_field;
          column_field.type_.set_type(ObNullType);
          OX (param_field.inout_mode_ = ObRoutineParamInOut::SP_PARAM_INOUT);
          OZ (result_set.add_field_column(column_field),
              K(i), K(question_marks_count), K(column_field));
        } else if (OB_NOT_NULL(call_stmt) && call_stmt->get_call_proc_info()->is_out_param(i)) {
          OX (param_field.inout_mode_ = ObRoutineParamInOut::SP_PARAM_INOUT);
        }
        OZ (result_set.add_param_column(param_field),
            K(param_field), K(i), K(question_marks_count));
      }
    }
  }
  return ret;
}

int ObSql::fill_select_result_set(ObResultSet &result_set, ObSqlCtx *context, const PlanCacheMode mode,
                                  ObCollationType collation_type, const ObString &type_name,
                                  ObStmt &basic_stmt, ObField &field)
{
  int ret = OB_SUCCESS;
  ObSelectStmt *select_stmt = static_cast<ObSelectStmt *>(&basic_stmt);
  if (select_stmt->has_select_into()) { //  for select into, no rows return
    // do nothing.
  } else {
    int64_t size = select_stmt->get_select_item_size();
    common::ObIAllocator &alloc = result_set.get_mem_pool();
    number::ObNumber number;
    number.set_zero();
    if (OB_FAIL(result_set.reserve_field_columns(size))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < size; ++i) {
      const SelectItem &select_item = select_stmt->get_select_item(i);
      ObRawExpr *expr = select_item.expr_;
      bool is_ext_field = false;
      ObSqlString composite_field_name;
      if (OB_UNLIKELY(NULL == expr)) {
        ret = OB_ERR_ILLEGAL_ID;
      } else {
        if (ob_is_string_or_lob_type(expr->get_data_type())
            && CS_TYPE_BINARY != expr->get_collation_type()
            && ObCharset::is_valid_collation(collation_type)) {
          field.charsetnr_ = static_cast<uint16_t>(collation_type);
        } else {
          field.charsetnr_ = static_cast<uint16_t>(expr->get_collation_type());
        }
      }

      if (OB_SUCC(ret) && expr->get_result_type().is_ext()) {
        is_ext_field = true;
        field.type_.set_collation_type(CS_TYPE_BINARY);
        field.type_.set_collation_level(CS_LEVEL_IMPLICIT);
        field.length_ = OB_MAX_LONGTEXT_LENGTH;
        if (CS_TYPE_BINARY != expr->get_collation_type()
          && ObCharset::is_valid_collation(collation_type)) {
          field.charsetnr_ = static_cast<uint16_t>(collation_type);
        } else {
          field.charsetnr_ = static_cast<uint16_t>(expr->get_collation_type());
        }
      }

      if (OB_SUCC(ret)) {
        // Setup field Type and Accuracy
        field.type_.set_type(expr->get_data_type());
        field.accuracy_ = expr->get_accuracy();
        field.flags_ = static_cast<uint16_t>(expr->get_result_flag());
        // Setup Collation and Collation level
        if (ob_is_string_or_lob_type(static_cast<ObObjType>(expr->get_data_type()))
            || ob_is_enum_or_set_type(static_cast<ObObjType>(expr->get_data_type()))) {
          field.type_.set_collation_type(expr->get_collation_type());
          field.type_.set_collation_level(expr->get_collation_level());
        }
        // Setup Scale
        if (ObVarcharType == field.type_.get_type()) {
          field.type_.set_varchar(type_name);
        } else if (ObNumberType == field.type_.get_type()) {
          field.type_.set_number(number);
        }
        if (context->session_info_->is_varparams_sql_prepare()) {
          // question mark expr has no valid result type in prepare stage
          if (!expr->get_result_type().is_ext()
              && !expr->get_result_type().is_collection_sql_type()
              && !expr->get_result_type().is_enum_or_set()
              && !expr->get_result_type().is_enumset_inner_type()
              && OB_FAIL(expr->get_length_for_meta_in_bytes(
                    field.length_, static_cast<ObCollationType>(field.charsetnr_)))) {
          }
        } else if (expr->get_result_type().is_collection_sql_type()) {
          uint16_t subschema_id = expr->get_result_type().get_subschema_id();
          uint16_t tmp_subschema_id = ObInvalidSqlType;
          uint64_t udt_id = expr->get_result_type().get_udt_id();
          ObSqlUDTMeta udt_meta;
          if (expr->get_result_type().is_collection_sql_type()
              && !ObObjUDTUtil::ob_is_supported_sql_udt(udt_id)) {
            // array type
            field.type_.set_subschema_id(subschema_id);
            field.charsetnr_ = CS_TYPE_UTF8MB4_BIN;
            field.length_ = OB_MAX_LONGTEXT_LENGTH;
          } else if (OB_FAIL(result_set.get_exec_context().get_subschema_id_by_udt_id(udt_id, tmp_subschema_id))) {
          } else if (OB_FAIL(result_set.get_exec_context().get_sqludt_meta_by_subschema_id(tmp_subschema_id, udt_meta))) {
          } else if(ObObjUDTUtil::ob_is_supported_sql_udt(udt_meta.udt_id_)) {
            if (udt_id != udt_meta.udt_id_) {
              ret = OB_ERR_UNEXPECTED;
            }
            field.type_.set_subschema_id(tmp_subschema_id);
            field.charsetnr_ = CS_TYPE_BINARY;
            field.length_ = OB_MAX_LONGTEXT_LENGTH;
            if (OB_SUCC(ret)) {
              if (OB_FAIL(ob_write_string(alloc, ObString(udt_meta.udt_name_len_, udt_meta.udt_name_), field.type_name_))) {
              }
            }
          } else {
            ret = OB_NOT_SUPPORTED;
          }
        } else if (expr->get_result_type().is_ext()) {
          field.type_.meta_.set_extend_type(expr->get_result_type().get_extend_type());
          if (OB_INVALID_ID != expr->get_result_type().get_udt_id()
              && (PL_VARRAY_TYPE == expr->get_result_type().get_extend_type()
                  || PL_NESTED_TABLE_TYPE == expr->get_result_type().get_extend_type()
                  || PL_ASSOCIATIVE_ARRAY_TYPE == expr->get_result_type().get_extend_type()
                  || PL_RECORD_TYPE == expr->get_result_type().get_extend_type())) {
            if (PC_TEXT_MODE == mode
                && NULL == context->secondary_namespace_
                && NULL == context->session_info_->get_pl_context()) {
              // Text protocol convert extend type field to varchar
              field.type_.set_varchar(type_name);
            }
          }
        } else if (!expr->get_result_type().is_ext()
                   && OB_FAIL(expr->get_length_for_meta_in_bytes(
                        field.length_, static_cast<ObCollationType>(field.charsetnr_)))) {
        }
      }
      // SELECT ITEM's alias name and expr name rule examples:
      // SELECT field1+field2 AS f1, field1+3, "thanks", field2 AS f2, field3, "hello" as f4, field1+4 as f5 FROM t1
      // "is_alias":true,  "alias_name":"f1", "expr_name":"f1"
      // "is_alias":false, "alias_name":"", "expr_name":"field1+3"
      // "is_alias":false, "alias_name":"", "expr_name":", "thanks"",
      // "is_alias":true,  "alias_name":"f2", "expr_name":"f2",  "column_name":"field2",
      // "is_alias":true,  "alias_name":"field3", "expr_name":"field3",  "column_name":"field3",
      // "is_alias":true,  "alias_name":"f4", "expr_name":"f4"
      // "is_alias":true,  "alias_name":"f5", "expr_name":"f5"
      //
      ObCollationType field_names_collation =
            ObCharset::is_valid_collation(collation_type) ? collation_type : CS_TYPE_UTF8MB4_BIN;
      if (OB_SUCC(ret)) {
        ObSqlString field_name;
        if (composite_field_name.length() > 0) {
          // need record member name
          if (OB_FAIL(field_name.append(select_item.alias_name_))) {
          } else if (OB_FAIL(field_name.append(composite_field_name.string()))) {
          }
        }
        if (OB_FAIL(ret)) {
          // do nothing
        } else if (OB_FAIL(ObSQLUtils::copy_and_convert_string_charset(alloc,
                                field_name.length() > 0 ? field_name.string() : select_item.alias_name_,
                                field.cname_, CS_TYPE_UTF8MB4_BIN, field_names_collation))) {
        }
      }
      if (OB_SUCC(ret)) {
        bool is_contain_column_ref = false;
        if (expr->is_column_ref_expr()
            || T_FUN_SET_TO_STR == expr->get_expr_type()
            || T_FUN_ENUM_TO_STR == expr->get_expr_type()) {
          const TableItem *table_item = NULL;
          ObString column_name;
          ObColumnRefRawExpr *column_expr = NULL;
          if (OB_FAIL(ObRawExprUtils::get_col_ref_expr_recursively(expr, column_expr))) {
          } else if (OB_NOT_NULL(column_expr)) {
            is_contain_column_ref = true;
            uint64_t table_id = column_expr->get_table_id();
            uint64_t column_id = column_expr->get_column_id();
            ColumnItem *column_item = select_stmt->get_column_item_by_id(table_id, column_id);
            if (OB_ISNULL(column_item)) {
              ret = OB_ERR_ILLEGAL_ID;
            } else {
              column_name = column_item->column_name_;
              table_item = select_stmt->get_table_item_by_id(table_id);
              if (column_expr->is_unique_key_column()) {
                field.flags_ |= UNIQUE_KEY_FLAG;
              }
              if (column_expr->is_mul_key_column()) {
                field.flags_ |= MULTIPLE_KEY_FLAG;
              }
              if (::oceanbase::share::schema::is_heap_table_primary_key_column(column_expr->get_column_flags())) {
                field.flags_ |= PRI_KEY_FLAG;
              }
            }
          }
          if (OB_SUCC(ret) && is_contain_column_ref) {
            if (OB_ISNULL(table_item)) {
              ret = OB_ERR_ILLEGAL_ID;
            } else if (OB_FAIL(ob_write_string(alloc,
                                                column_name,
                                                field.org_cname_))) {
            } else if (OB_FAIL(ObSQLUtils::copy_and_convert_string_charset(
                                    alloc, table_item->database_name_, field.dname_,
                                    CS_TYPE_UTF8MB4_BIN, field_names_collation))) {
            } else if (table_item->alias_name_.length() > 0) {
              if (OB_FAIL(ObSQLUtils::copy_and_convert_string_charset(
                                    alloc, table_item->alias_name_, field.tname_,
                                    CS_TYPE_UTF8MB4_BIN, field_names_collation))) {
              }
            } else {
              if (OB_FAIL(ObSQLUtils::copy_and_convert_string_charset(
                                    alloc, table_item->table_name_, field.tname_,
                                    CS_TYPE_UTF8MB4_BIN, field_names_collation))) {
              }
            }
            if (OB_FAIL(ret)) {
            } else if (OB_FAIL(ob_write_string(alloc, table_item->table_name_, field.org_tname_))) {
            }
          }
        }
      }
      if (OB_SUCC(ret) && !(PC_PS_MODE == mode || PC_PL_MODE == mode)) {
        void *buf = NULL;
        if (OB_ISNULL(buf = alloc.alloc(sizeof(ObParamedSelectItemCtx)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          ObSqlString paramed_field_name;
          field.paramed_ctx_ = new(buf) ObParamedSelectItemCtx();
          if (composite_field_name.length() > 0) {
            // need record member name
            if (OB_FAIL(paramed_field_name.append(select_item.paramed_alias_name_))) {
            } else if (OB_FAIL(paramed_field_name.append(composite_field_name.string()))) {
            }
          }
          if (OB_FAIL(ret)) {
            // do nothing
          } else if (OB_FAIL(ob_write_string(alloc,
                                             paramed_field_name.length() > 0
                                               ? paramed_field_name.string()
                                               : select_item.paramed_alias_name_,
                                             field.paramed_ctx_->paramed_cname_))) {
          } else if (OB_FAIL(field.paramed_ctx_->param_str_offsets_.assign(
                                                  select_item.questions_pos_))) {
          } else if (OB_FAIL(field.paramed_ctx_->param_idxs_.assign(select_item.params_idx_))) {
          } else {
            field.paramed_ctx_->neg_param_idxs_ = select_item.neg_param_idx_;
            field.paramed_ctx_->esc_str_flag_ = select_item.esc_str_flag_;
            field.paramed_ctx_->need_check_dup_name_ = select_item.need_check_dup_name_;
            // If the projection column is a column
            field.paramed_ctx_->is_column_field_ = (T_REF_COLUMN == expr->get_expr_type());
            field.is_paramed_select_item_ = true;
          }
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_FAIL(result_set.add_field_column(field))) {
      } else {
        field.cname_.assign(NULL, 0);
        field.org_cname_.assign(NULL, 0);
        field.dname_.assign(NULL, 0);
        field.tname_.assign(NULL, 0);
        field.org_tname_.assign(NULL, 0);
        field.type_.reset();
        field.type_.set_type(ObExtendType);
        field.paramed_ctx_ = NULL;
      }
    }
  }
  return ret;
}

int ObSql::fill_result_set(const ObPsStmtId stmt_id, const ObPsStmtInfo &stmt_info, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  result.set_statement_id(stmt_id);
  result.set_stmt_type(stmt_info.get_stmt_type());
  result.set_literal_stmt_type(stmt_info.get_literal_stmt_type());
  const ObPsSqlMeta &sql_meta = stmt_info.get_ps_sql_meta();
  result.set_p_param_fileds(const_cast<common::ParamsFieldIArray *>(&sql_meta.get_param_fields()));
  result.set_p_column_fileds(const_cast<common::ParamsFieldIArray *>(&sql_meta.get_column_fields()));
  //ObPsSqlMeta::const_column_iterator column_iter = sql_meta.column_begin();
  //result.reserve_field_columns(sql_meta.get_column_size());
  //for (; OB_SUCC(ret) && column_iter != sql_meta.column_end(); ++column_iter) {
    //if (OB_ISNULL(column_iter) || OB_ISNULL(*column_iter)) {
      //ret = OB_ERR_UNEXPECTED;
      //LOG_WARN("column iter is null", K(ret), K(column_iter));
    //} else if (OB_FAIL(result.add_field_column(**column_iter))) {
      //LOG_WARN("add column field failed", K(ret));
    //}
  //}

  //ObPsSqlMeta::const_param_iterator param_iter = sql_meta.param_begin();
  //for (; OB_SUCC(ret) && param_iter != sql_meta.param_end(); ++param_iter) {
    //if (OB_ISNULL(param_iter) || OB_ISNULL(param_iter)) {
      //ret = OB_ERR_UNEXPECTED;
      //LOG_WARN("param iter is null", K(ret), K(param_iter));
    //} else if (OB_FAIL(result.add_param_column(**param_iter))) {
      //LOG_WARN("add param field faield", K(ret));
    //}
  //}
  return ret;
}

int ObSql::do_add_ps_cache(const PsCacheInfoCtx &info_ctx,
                           ObSchemaGetterGuard &schema_guard,
                           ObResultSet &result)
{
  int ret = OB_SUCCESS;
  bool is_contain_tmp_tbl = false;
  ObSQLSessionInfo &session = result.get_session();
  ObPsCache *ps_cache = ps_cache_;
  uint64_t db_id = OB_INVALID_ID;
  (void)session.get_database_id(db_id);
  if (OB_ISNULL(ps_cache)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (      OB_FAIL(check_contain_temporary_table(schema_guard, result, is_contain_tmp_tbl))) {
  } else {
    ObPsStmtItem *ps_stmt_item = NULL;
    ObPsStmtInfo *ref_stmt_info = NULL;
    bool duplicate_prepare = false;
    ObPsSqlKey ps_key;
    ps_key.db_id_ = db_id;
    ps_key.ps_sql_ = info_ctx.normalized_sql_;
    // add stmt item
    if (OB_FAIL(ps_cache->get_or_add_stmt_item(ps_key,
                                               is_contain_tmp_tbl,
                                               ps_stmt_item))) {
    } else if (OB_FAIL(ps_cache->get_or_add_stmt_info(info_ctx,
                                                      result,
                                                      schema_guard,
                                                      ps_stmt_item,
                                                      ref_stmt_info))) {
    } else if (OB_ISNULL(ps_stmt_item) || OB_ISNULL(ref_stmt_info)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ref_stmt_info->set_literal_stmt_type(result.get_literal_stmt_type());
    }
    if (NULL != ref_stmt_info) {
      ref_stmt_info->set_is_sensitive_sql(info_ctx.is_sensitive_sql_);
    }
    //add session info
    if (OB_SUCC(ret)) {
      ObPsStmtId inner_stmt_id = ps_stmt_item->get_ps_stmt_id();
      ObPsStmtId client_stmt_id = OB_INVALID_ID;
      if (OB_FAIL(session.prepare_ps_stmt(inner_stmt_id,
                                          ref_stmt_info,
                                          client_stmt_id,
                                          duplicate_prepare,
                                          info_ctx.is_inner_sql_))) {
      } else {
        result.set_statement_id(client_stmt_id);
        result.set_stmt_type(info_ctx.stmt_type_);
        LOG_TRACE("add ps session info", K(ret), K(*ref_stmt_info), K(client_stmt_id),
                                        K(*ps_stmt_item), K(session.get_server_sid()));
      }
    }
    if (OB_FAIL(ret) || duplicate_prepare) { //dec ref count
      if (NULL != ps_stmt_item) {
        if (NULL != ref_stmt_info) {
          ObPsStmtId inner_stmt_id = ps_stmt_item->get_ps_stmt_id();
          ps_cache->deref_stmt_info(inner_stmt_id); // need to decide whether to remove
        }
        ps_stmt_item->dec_ref_count();
      }
    }
  }
  return ret;
}

int ObSql::check_contain_temporary_table(share::schema::ObSchemaGetterGuard &schema_guard,
                                         ObResultSet &result,
                                         bool &is_contain_tmp_tbl)
{
  int ret = OB_SUCCESS;
  is_contain_tmp_tbl = false;
  const ObTableSchema *table_schema = nullptr;
  for (int64_t i = 0; OB_SUCC(ret) && !is_contain_tmp_tbl && i < result.get_ref_objects().count(); i++) {
    table_schema = nullptr;
    ObSchemaObjVersion &obj_version = result.get_ref_objects().at(i);
    if (DEPENDENCY_TABLE != obj_version.object_type_) {
      // do nothing
    } else if (OB_FAIL(schema_guard.get_table_schema(
                                              obj_version.object_id_,
                                              table_schema))) {
    } else if (nullptr == table_schema) {
      ret = OB_ERR_UNEXPECTED;
    } else if (table_schema->is_tmp_table()) {
      is_contain_tmp_tbl = true;
      break;
    }
  }
  return ret;
}

int ObSql::do_real_prepare(const ObString &sql,
                           ObSqlCtx &context,
                           ObResultSet &result,
                           bool is_inner_sql)
{
  int ret = OB_SUCCESS;
  ParseResult parse_result;
  MEMSET(&parse_result, 0, SIZEOF(ParseResult));
  ObStmt *basic_stmt = NULL;
  stmt::StmtType stmt_type = stmt::T_NONE;
  int64_t param_cnt = 0;
  PsCacheInfoCtx info_ctx;
  ObIAllocator &allocator = result.get_mem_pool();
  ObSQLSessionInfo &session = result.get_session();
  ObExecContext &ectx = result.get_exec_context();
  ObParser parser(allocator, session.get_sql_mode(), session.get_charsets4parser());
  ParseMode parse_mode = (context.is_dynamic_sql_  || !is_inner_sql) ? DYNAMIC_SQL_MODE :
                         session.is_for_trigger_package() ? TRIGGER_MODE : STD_MODE;

  // normal ps sql also a dynamic sql, we adjust is_dynamic_sql_ for normal ps sql parser.
  context.is_dynamic_sql_ = !context.is_dynamic_sql_ ? !is_inner_sql : context.is_dynamic_sql_;

  bool is_from_pl = (NULL != context.secondary_namespace_ || result.is_simple_ps_protocol());
  ObPsPrepareStatusGuard ps_status_guard(session);
  ObPlanCacheCtx pc_ctx(sql, PC_PS_MODE, allocator, context, ectx);
  ParamStore param_store( (ObWrapperAllocator(&allocator)) );
  pc_ctx.set_is_inner_sql(is_inner_sql);

  if (OB_ISNULL(context.session_info_) || OB_ISNULL(context.schema_guard_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(parser.parse(sql,
                                  parse_result,
                                  parse_mode))) {
  } else if (ObSQLUtils::is_mysql_ps_not_support_stmt(parse_result)) {
    ret = OB_ER_UNSUPPORTED_PS;
  }  else if (parse_result.question_mark_ctx_.count_ > common::OB_MAX_PS_PARAM_COUNT) {
    ret = OB_ERR_PS_TOO_MANY_PARAM;
    LOG_USER_ERROR(OB_ERR_PS_TOO_MANY_PARAM);
  } else {
    ps_status_guard.is_varparams_sql_prepare(parse_result.question_mark_ctx_.count_ > 0 ? true : false);
  }
  context.is_sensitive_ |= parse_result.contain_sensitive_data_;

  OZ (ObResolverUtils::resolve_stmt_type(parse_result, stmt_type));

  if (OB_FAIL(ret)) {
  } else if (result.is_simple_ps_protocol() // simple_ps_protocol only do parse
             // for anonymous block, mock prepare only parses and reports bind metadata
             || (stmt::T_ANONYMOUS_BLOCK == stmt_type
                 && context.is_prepare_protocol_
                 && context.is_prepare_stage_
                 && context.is_mock_prepare_)) {
    param_cnt = parse_result.question_mark_ctx_.count_;
    info_ctx.normalized_sql_ = sql;
    if (stmt::T_ANONYMOUS_BLOCK == stmt_type
        && context.is_prepare_protocol_
        && context.is_prepare_stage_
        && context.is_mock_prepare_) {
      OZ (result.reserve_param_columns(param_cnt));
      for (int64_t i = 0; OB_SUCC(ret) && i < param_cnt; ++i) {
        ObField param_field;
        param_field.type_.set_type(ObIntType);
        param_field.cname_ = ObString::make_string("?");

        // Mock prepare does not fully resolve anonymous blocks, so out row info is unknown here.
        // Mark parameters as INOUT conservatively to avoid selecting an async driver that cannot return rows.

        param_field.inout_mode_ = ObRoutineParamInOut::SP_PARAM_INOUT;
        OZ (result.add_param_column(param_field), K(param_field), K(i), K(param_cnt));
      }
    }

  } else {
    if (context.is_dynamic_sql_) {
      parse_result.input_sql_ = parse_result.no_param_sql_;
      parse_result.input_sql_len_ = parse_result.no_param_sql_len_;
    }
    if (OB_FAIL(generate_stmt(parse_result, NULL, context, allocator, result, basic_stmt))) {
    } else if (ObStmt::is_dml_stmt(stmt_type)
              && !ObStmt::is_show_stmt(stmt_type)
              && !is_inner_sql
              && !is_from_pl) {
      bool is_transform_outline = false;
      if (OB_FAIL(ObSqlParameterization::parameterize_syntax_tree(allocator,
                                                                  is_transform_outline,
                                                                  pc_ctx,
                                                                  parse_result.result_tree_,
                                                                  param_store,
                                                                  session.get_charsets4parser()))) {
        LOG_INFO("parameterize syntax tree failed", K(ret));
        pc_ctx.ps_need_parameterized_ = false;
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret)) {
        if (!pc_ctx.ps_need_parameterized_) {
          pc_ctx.fixed_param_idx_.reset();
          pc_ctx.fp_result_.raw_params_.reset();
        } else {
          info_ctx.no_param_sql_ = pc_ctx.sql_ctx_.plan_key_.constructed_sql_;
        }
      }
    } else if (!is_inner_sql && stmt::T_CALL_PROCEDURE == stmt_type) {
      if (OB_FAIL(ObSqlParameterization::parameterize_syntax_tree(allocator,
                                                                  false,
                                                                  pc_ctx,
                                                                  parse_result.result_tree_,
                                                                  param_store,
                                                                  session.get_charsets4parser()))) {
        LOG_INFO("parameterize call procedure syntax tree failed", K(ret));
        pc_ctx.ps_need_parameterized_ = false;
        pc_ctx.fixed_param_idx_.reset();
        pc_ctx.fp_result_.raw_params_.reset();
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret)) {
        if (!pc_ctx.ps_need_parameterized_) {
          pc_ctx.fixed_param_idx_.reset();
          pc_ctx.fp_result_.raw_params_.reset();
        } else {
          info_ctx.no_param_sql_ = pc_ctx.sql_ctx_.plan_key_.constructed_sql_;
        }
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(basic_stmt)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(basic_stmt->get_query_ctx())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (stmt::T_CALL_PROCEDURE == basic_stmt->get_stmt_type()
               && FALSE_IT(result.set_cmd(dynamic_cast<ObICmd*>(basic_stmt)))) {
    } else if (OB_FAIL(fill_result_set(result, &context, PC_PS_MODE, *basic_stmt))) {
    } else if (OB_ISNULL(result.get_param_fields())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      param_cnt = result.get_param_fields()->count();
      stmt_type = basic_stmt->get_stmt_type();
      // If it is internal sql, such as pl internal sql, need to use the formatted text string,
      // Because need to replace the variables in sql of pl with standard ps text for hard parsing
      // and the ps text of external requests cannot be formatted, because checksum verification is required when parsing the ps execute package,
      // Need to ensure the prepare text is consistent with what the client sends
      if (is_inner_sql) {
        // pl
        info_ctx.normalized_sql_ = basic_stmt->get_query_ctx()->get_sql_stmt();
      } else {
        info_ctx.normalized_sql_ = sql;
      }
    }

    LOG_INFO("generate new stmt", K(ret), K(param_cnt), K(stmt_type), K(info_ctx.no_param_sql_),
             K(info_ctx.normalized_sql_),
             "sql", context.is_sensitive_ ? ObString(OB_MASKED_STR) : sql);
  }
  if (OB_SUCC(ret)) {
    info_ctx.param_cnt_ = param_cnt;
    info_ctx.stmt_type_ = stmt_type;
    info_ctx.is_inner_sql_ = is_inner_sql;
    info_ctx.is_sensitive_sql_ = context.is_sensitive_;
    info_ctx.raw_params_ = &pc_ctx.fp_result_.raw_params_;
    info_ctx.fixed_param_idx_ = &pc_ctx.fixed_param_idx_;
    info_ctx.raw_sql_.assign_ptr(sql.ptr(), sql.length());
    if (OB_FAIL(do_add_ps_cache(info_ctx, *context.schema_guard_, result))) {
    }
  }
  //if the error code is ob_timeout, we add more error info msg for dml query.
  if (OB_UNLIKELY(OB_TIMEOUT == ret && session.is_user_session())) {
    LOG_USER_ERROR(OB_TIMEOUT, THIS_WORKER.get_timeout_ts() - session.get_query_start_time());
  }
  LOG_INFO("add ps cache", K(info_ctx.normalized_sql_), K(param_cnt), K(ret));
  return ret;
}


ObSql::TimeoutGuard::TimeoutGuard(ObSQLSessionInfo &session)
  : session_(session)
{
  int ret = OB_SUCCESS;
  worker_timeout_ = THIS_WORKER.get_timeout_ts();
}

ObSql::TimeoutGuard::~TimeoutGuard()
{
  int ret = OB_SUCCESS;
  if (THIS_WORKER.get_timeout_ts() != worker_timeout_) {
    THIS_WORKER.set_timeout_ts(worker_timeout_);
  }
}

int ObSql::set_timeout_for_pl(ObSQLSessionInfo &session_info, int64_t &abs_timeout_us)
{
  int ret = OB_SUCCESS;
  int64_t query_timeout;
  if (THIS_WORKER.is_timeout()) {
    ret = OB_TIMEOUT;
  } else if (OB_FAIL(session_info.get_query_timeout(query_timeout))) {
    // do nothing
  } else {
    OX (abs_timeout_us = session_info.get_query_start_time() > 0
                         ? session_info.get_query_start_time() + query_timeout
                           : ObTimeUtility::current_time() + query_timeout);
    if (THIS_WORKER.get_timeout_ts() > abs_timeout_us) {
      OX (THIS_WORKER.set_timeout_ts(abs_timeout_us));
    }
  }
  return ret;
}

/*
 * sql: sql statement in pl
 * PLPrepareCtx: relevant information used for prepare
 * PLPrepareResult: output result after prepare
 */

int ObSql::prepare_pl_sql(const ObString &sql,
                          ObSPIService::PLPrepareCtx &pl_prepare_ctx,
                          ObSPIService::PLPrepareResult &pl_prepare_result,
                          ParamStore *params)
{
  int ret = OB_SUCCESS;
  ObString cur_query;
  ObString trimed_stmt = const_cast<ObString &>(sql).trim();
  ObSqlCtx &context = pl_prepare_result.sql_ctx_;
  int64_t param_cnt = 0;
  ObString normalized_sql;
  ParseResult parse_result;
  MEMSET(&parse_result, 0, SIZEOF(ParseResult));
  ObStmt *basic_stmt = NULL;
  int64_t cur_timeout_us = 0;
  stmt::StmtType stmt_type = stmt::T_NONE;
  ObSchemaGetterGuard &schema_guard = pl_prepare_result.schema_guard_;
  ObSQLSessionInfo &sess = pl_prepare_ctx.sess_info_;
  TimeoutGuard timeout_guard(sess);
  int64_t old_query_start_time = sess.get_query_start_time();
  sess.set_query_start_time(ObTimeUtility::current_time());
  OZ (pl_prepare_result.init(sess, get_plan_cache_access_service()));
  CK (OB_NOT_NULL(pl_prepare_result.get_allocator()));
  CK (OB_NOT_NULL(pl_prepare_result.result_set_));
  if (OB_SUCC(ret)) {
    ObIAllocator &allocator = *pl_prepare_result.get_allocator();
    ObParser parser(allocator, sess.get_sql_mode(), sess.get_charsets4parser());
    bool is_for_trigger = false;
    if (NULL != pl_prepare_ctx.secondary_ns_) {
      is_for_trigger = ObTriggerInfo::is_trigger_package_id(pl_prepare_ctx.secondary_ns_->get_package_id());
    }
    ParseMode parse_mode = pl_prepare_ctx.is_dynamic_sql_ ? DYNAMIC_SQL_MODE :
                          is_for_trigger ? TRIGGER_MODE : STD_MODE;

    context.is_dynamic_sql_ = pl_prepare_ctx.is_dynamic_sql_;
    context.is_cursor_ = pl_prepare_ctx.is_cursor_;
    context.secondary_namespace_ = pl_prepare_ctx.secondary_ns_;
    context.session_info_ = &sess;
    context.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
    context.exec_type_ = PLSql;
    context.is_prepare_protocol_ = true;
    context.is_prepare_stage_ = true;

    if (OB_FAIL(ob_write_string(allocator, sess.get_current_query_string(), cur_query))) {
    }

    if (OB_SUCC(ret)) {
      WITH_CONTEXT(pl_prepare_result.mem_context_) {
          ObResultSet &result = *pl_prepare_result.result_set_;
          LinkExecCtxGuard link_guard(result.get_session(), result.get_exec_context());
          ObPsPrepareStatusGuard ps_status_guard(sess);
          if (trimed_stmt.empty()) {
            ret = OB_ERR_EMPTY_QUERY;
          } else if (OB_FAIL(set_timeout_for_pl(sess, cur_timeout_us))) {
          } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(
                                  schema_guard))) {
          } else if (FALSE_IT(context.schema_guard_ = &schema_guard)) {
          } else if (OB_FAIL(init_result_set(context, result))) {
          } else if (OB_ISNULL(result.get_exec_context().get_physical_plan_ctx())) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_NOT_NULL(params) &&
                     OB_FAIL(construct_param_store(*params,
                                                   result.get_exec_context().get_physical_plan_ctx()->get_param_store_for_update()))) {
          } else if (OB_FAIL(sess.store_query_string(sql))) {
          } else if (OB_FAIL(parser.parse(sql, parse_result, parse_mode,
                                          false, false, true,
                                          pl_prepare_ctx.is_parser_dynamic_sql_))) {
          } else if (ObSQLUtils::is_mysql_ps_not_support_stmt(parse_result)) {
            ret = OB_ER_UNSUPPORTED_PS;
          } else if (NULL == pl_prepare_ctx.secondary_ns_ && !pl_prepare_ctx.is_dynamic_sql_) {
            result.set_simple_ps_protocol();
          }
          context.is_sensitive_ |= parse_result.contain_sensitive_data_;

          if (OB_SUCC(ret) && !pl_prepare_ctx.is_parser_dynamic_sql_) {
            ps_status_guard.is_varparams_sql_prepare(parse_result.question_mark_ctx_.count_ > 0 ? true : false);
          }
          if (OB_FAIL(ret)) {
            // do nothing
          } else if (OB_FAIL(ObResolverUtils::resolve_stmt_type(parse_result, stmt_type))) {
          } else if (FALSE_IT(result.set_stmt_type(stmt_type))) {
          } else if (result.is_simple_ps_protocol()
                    || (stmt::T_ANONYMOUS_BLOCK == stmt_type && context.is_prepare_protocol_
                        && context.is_prepare_stage_ && context.is_mock_prepare_)) {
            if (parse_result.is_dynamic_sql_) {
              context.is_dynamic_sql_ = true;
            }
            bool for_update = false;
            if (stmt::T_SELECT == stmt_type &&
                PARSE_SELECT_MAX_IDX == parse_result.result_tree_->children_[0]->num_child_ &&
                NULL != parse_result.result_tree_->children_[0]->children_[PARSE_SELECT_FOR_UPD]) {
              for_update = true;
            }
            result.get_external_retrieve_info().is_select_for_update_ = for_update;
            param_cnt = parse_result.question_mark_ctx_.count_;
            normalized_sql = context.is_dynamic_sql_ && parse_result.no_param_sql_len_ > 0
              ? ObString(parse_result.no_param_sql_len_, parse_result.no_param_sql_) : sql;
            if (stmt::T_ANONYMOUS_BLOCK == stmt_type && context.is_prepare_protocol_
                && context.is_prepare_stage_ && context.is_mock_prepare_) {
              OZ (result.reserve_param_columns(param_cnt));
              for (int64_t i = 0; OB_SUCC(ret) && i < param_cnt; ++i) {
                ObField param_field;
                param_field.type_.set_type(ObIntType);
                param_field.cname_ = ObString::make_string("?");
                OZ (result.add_param_column(param_field), K(param_field), K(i), K(param_cnt));
              }
            }
          } else {
            if (parse_result.is_dynamic_sql_) {
              context.is_dynamic_sql_ = true;
            }
            if (context.is_dynamic_sql_) {
              parse_result.input_sql_ = parse_result.no_param_sql_;
              parse_result.input_sql_len_ = parse_result.no_param_sql_len_;
            }
            pl_prepare_result.question_mark_cnt_ = parse_result.question_mark_ctx_.count_;
            if (OB_FAIL(generate_stmt(parse_result, NULL, context, allocator, result, basic_stmt))) {
            } else if (OB_ISNULL(basic_stmt)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_ISNULL(basic_stmt->get_query_ctx())) {
              ret = OB_ERR_UNEXPECTED;
            } else if (stmt::T_CALL_PROCEDURE == basic_stmt->get_stmt_type()
                      && FALSE_IT(result.set_cmd(dynamic_cast<ObICmd*>(basic_stmt)))) {
            } else if (OB_FAIL(fill_result_set(result, &context, PC_PL_MODE, *basic_stmt))) {
            } else if (OB_ISNULL(result.get_param_fields())) {
              ret = OB_INVALID_ARGUMENT;
            } else {
              normalized_sql = basic_stmt->get_query_ctx()->get_sql_stmt();
            }
          }

          if (OB_FAIL(ret)) {
            // do nothing
          } else if (OB_FAIL(ob_write_string(allocator, normalized_sql, result.get_stmt_ps_sql(), true))) {
          }
          int tmp_ret = OB_SUCCESS;
          if ((tmp_ret = sess.store_query_string(cur_query)) != OB_SUCCESS) {
            LOG_WARN("failed to store query string", K(ret), K(tmp_ret));
            ret = OB_SUCCESS == ret ?  tmp_ret : ret;
          }
      }
    }
  }
  sess.set_query_start_time(old_query_start_time);
  return ret;
}

int ObSql::handle_sql_execute(const ObString &sql,
                              ObSqlCtx &context,
                              ObResultSet &result,
                              ParamStore &org_params,
                              PlanCacheMode mode)
{
  int ret = OB_SUCCESS;
  int get_plan_err = OB_SUCCESS;
  bool use_plan_cache = false;
  ObIAllocator &allocator = result.get_mem_pool();
  ObSQLSessionInfo *session = context.session_info_;
  ObExecContext &ectx = result.get_exec_context();
  ObPhysicalPlanCtx *pctx = ectx.get_physical_plan_ctx();
  ParamStore params( (ObWrapperAllocator(allocator)) );
  ParamStore *ab_params = NULL;

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(session) || OB_ISNULL(plan_cache_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((mode == PC_PS_MODE || mode == PC_PL_MODE) && OB_ISNULL(pctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    use_plan_cache = session->get_local_ob_enable_plan_cache();
  }

  ObPlanCacheCtx pc_ctx(sql, mode, allocator, context, ectx);

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (mode == PC_PL_MODE) {
    if (OB_FAIL(reconstruct_pl_params_store(allocator, context, org_params, params, ab_params))) {
    } else if (context.is_batch_params_execute() && OB_ISNULL(ab_params)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(construct_param_store(params, pctx->get_param_store_for_update()))) {
    } else if (OB_FAIL(construct_parameterized_params(params, pc_ctx))) {
    } else {
      pc_ctx.normal_parse_const_cnt_ = params.count();
      pc_ctx.set_is_parameterized_execute();
      pc_ctx.ab_params_ = ab_params;
    }
  } else if (mode == PC_PS_MODE) {
    // TODO, not support ps now.
    ret = OB_ERR_UNEXPECTED;
  }

  if (OB_SUCC(ret)) {
    if (!use_plan_cache) {
      // do nothing
    } else if (OB_FAIL(pc_get_plan_and_fill_result(pc_ctx, result,
                         get_plan_err, ectx.get_need_disconnect_for_update()))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (!result.get_is_from_plan_cache()) {
      if (mode == PC_PS_MODE || mode == PC_PL_MODE) {
        pctx->get_param_store_for_update().reset();
      }
      // Use SMART_CALL_LARGE for complex SQL plan generation when plan cache misses
      // This prevents frequent stack extending during physical plan generation
      if (OB_FAIL(SMART_CALL_LARGE(handle_physical_plan(sql, context, result, pc_ctx, get_plan_err)))) {
      }
    }
  }

  if (OB_SUCC(ret) && !context.is_text_ps_mode_) {
    if (OB_FAIL(after_get_plan(pc_ctx, *session, result.get_physical_plan(),
                result.get_is_from_plan_cache()))) {
    }
  }

  return ret;
}

/*!
 * sql: The SQL statement to be executed
 * params: The parameter list for the current SQL statement
 * res: Direct result set
 */
// TODO remove is_prepare_protocol and is_dynamic_sql
int ObSql::execute_pl_sql(const ObString &sql,
                          ObSQLSessionInfo &session,
                          ParamStore &params,
                          ObResultSet &result,
                          ObSqlCtx &context,
                          bool is_prepare_protocol,
                          bool is_dynamic_sql)
{
  int ret = OB_SUCCESS;
  int get_plan_err = OB_SUCCESS;
  TimeoutGuard timeout_guard(session);
  LinkExecCtxGuard link_guard(result.get_session(), result.get_exec_context());
  int64_t cur_timeout_us = 0;
  context.session_info_ = &session;
  ObIAllocator &allocator = result.get_mem_pool();
  ObExecContext &ectx = result.get_exec_context();
  ObPhysicalPlanCtx *pctx = NULL;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(result.init())) {
  } else if (OB_FAIL(init_result_set(context, result))) {
  } else {
    context.cur_sql_ = sql;
    context.is_from_pl_ = true;
    context.is_dynamic_sql_ = is_dynamic_sql;
    context.is_prepare_protocol_ = is_prepare_protocol;
    context.plan_key_.db_id_ = session.get_database_id();
    context.disable_privilege_check_ = PRIV_CHECK_FLAG_IN_PL;
    pctx = ectx.get_physical_plan_ctx();
    int64_t local_database_schema_version = -1;
    if (OB_ISNULL(context.schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(context.schema_guard_->get_schema_version(local_database_schema_version))) {
    } else {
      result.get_exec_context().get_sql_exec_ctx().set_query_begin_schema_version(local_database_schema_version);
    }
  }
  if (OB_SUCC(ret) && is_prepare_protocol && !is_dynamic_sql) {
    result.set_simple_ps_protocol();
  }

  LOG_TRACE("arrive handle pl execute", K(ret),
            "sql", context.is_sensitive_ ? ObString(OB_MASKED_STR) : sql,
            K(is_prepare_protocol), K(is_dynamic_sql), K(lbt()));
  int64_t start_time = ObTimeUtility::current_time();
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(pctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(set_timeout_for_pl(session, cur_timeout_us))) {
  } else if (OB_FAIL(session.store_query_string(sql))) {
  } else if (OB_FAIL(handle_sql_execute(sql, context, result, params, is_prepare_protocol ? PC_PL_MODE : PC_TEXT_MODE))) {
  }
  int64_t end_time = ObTimeUtility::current_time();
  if (OB_FAIL(ret) && OB_SUCCESS == result.get_errcode()) {
    result.set_errcode(ret);
  }
  //todo:@hr351303 the logic below will be moved to the spi layer later
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(context.schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(session.update_query_sensitive_system_variable(*(context.schema_guard_)))) {
    } else if (OB_FAIL(result.open())) {
    } else {
      // do nothing
    }
  }

  if (OB_SUCC(ret) && session.get_in_transaction()) {
    if (ObStmt::is_dml_write_stmt(result.get_stmt_type()) ||
        ObStmt::is_savepoint_stmt(result.get_stmt_type()) ||
        (ObStmt::is_select_stmt(result.get_stmt_type()) && OB_NOT_NULL(result.get_physical_plan()) && result.get_physical_plan()->has_for_update())) {
      session.set_has_exec_inner_dml(true);
    }
  }
  return ret;
}

int ObSql::handle_ps_prepare(const ObString &stmt,
                             ObSqlCtx &context,
                             ObResultSet &result,
                             bool is_inner_sql)
{
// open_cursors is 0 to indicate a special state, no limit is set
#define NEED_CHECK_SESS_MAX_PS_HANDLE_LIMIT(v) (0 == v ? false : true)
  int ret = OB_SUCCESS;
  ObString cur_query;
  // trimmed_stmt is only used for query empty check, prepare statement needs to use the original statement, avoid checksum inconsistency
  ObString trimed_stmt = const_cast<ObString &>(stmt).trim();
  if (trimed_stmt.empty()) {
    ret = OB_ERR_EMPTY_QUERY;
  } else if (OB_FAIL(init_result_set(context, result))) {
  }

  if (OB_SUCC(ret)) {
    ObSQLSessionInfo &session = result.get_session();
    ObPsCache *ps_cache = ps_cache_;
    ObExecContext &ectx = result.get_exec_context();
    ObIAllocator &allocator = result.get_mem_pool();
    ObPhysicalPlanCtx *pctx = ectx.get_physical_plan_ctx();
    ObSchemaGetterGuard *schema_guard = context.schema_guard_;
    ectx.set_is_ps_prepare_stage(true);

#ifndef NDEBUG
    LOG_INFO("Begin to handle prepare statement", "sess_id", session.get_server_sid(), K(stmt));
#endif

    if (OB_ISNULL(ps_cache) || OB_ISNULL(pctx) || OB_ISNULL(schema_guard)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("physical plan context or ps plan cache is NULL or schema_guard is null",
                K(ret), K(pctx), K(ps_cache));
    } else if (OB_FAIL(ob_write_string(allocator, session.get_current_query_string(), cur_query))) {
    } else if (OB_FAIL(session.store_query_string(stmt))) {
    } else {
      bool need_do_real_prepare = false;
      uint64_t db_id = OB_INVALID_ID;
      (void)session.get_database_id(db_id);
      ObPsSqlKey ps_key;
      ps_key.db_id_ = db_id;
      ps_key.ps_sql_ = stmt;
      ObPsStmtId inner_stmt_id = OB_INVALID_STMT_ID;
      ObPsStmtId client_stmt_id = OB_INVALID_STMT_ID;
      ObPsStmtInfo *stmt_info = NULL;
      ObPsStmtItem *stmt_item = NULL;
      bool duplicate_prepare = false;
      bool is_expired = false;
      int64_t open_cursors_limit = 0;
      int64_t cur_ps_handle_size = session.get_ps_session_info_size();
      if (FALSE_IT(open_cursors_limit = config::open_cursors())) {
      } else if (!is_inner_sql
                && NEED_CHECK_SESS_MAX_PS_HANDLE_LIMIT(open_cursors_limit)
                && cur_ps_handle_size >= open_cursors_limit) {
        ret = OB_ERR_OPEN_CURSORS_EXCEEDED;
      } else if (NULL != context.secondary_namespace_ || result.is_simple_ps_protocol()) {
        // pl initiated sql parsing, since each time it needs to calculate dependent objects and other extra parameters, therefore do_real_prepare needs to be done
        need_do_real_prepare = true;
        if (REACH_TIME_INTERVAL(1000000)) {
          LOG_INFO("need do real prepare",
                   K(db_id), K(stmt), K(need_do_real_prepare), K(context.secondary_namespace_),
                   K(result.is_simple_ps_protocol()));
        }
      } else if (OB_FAIL(ps_cache->ref_stmt_item(ps_key, stmt_item))) {
        if (OB_HASH_NOT_EXIST == ret) {
          ret = OB_SUCCESS;
          need_do_real_prepare = true;
          if (REACH_TIME_INTERVAL(1000000)) {
            LOG_INFO("stmt id not exist", K(db_id), K(stmt), K(need_do_real_prepare));
          }
        } else {
        }
      } else if (OB_ISNULL(stmt_item)
                 || OB_INVALID_STMT_ID == (inner_stmt_id = stmt_item->get_ps_stmt_id())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ps_cache->ref_stmt_info(inner_stmt_id, stmt_info))) {
        //inc stmt_info ref for session
        if (OB_HASH_NOT_EXIST == ret) {
          need_do_real_prepare = true;
          if (REACH_TIME_INTERVAL(1000000)) {
            LOG_INFO("stmt info not exist", K(db_id), K(stmt), K(inner_stmt_id), K(ret));
          }
          ret = OB_SUCCESS;
        } else {
        }
      } else if (OB_ISNULL(stmt_info)) {
        ret = OB_INVALID_ARGUMENT;
        //check stmt_info whether expired, if expired, do nothing
      } else if (OB_FAIL(ps_cache->check_schema_version(*context.schema_guard_,
                                                        *stmt_info,
                                                        is_expired))) {
      } else if (is_expired) {
        stmt_info->set_is_expired();
        if (OB_FAIL(ps_cache->erase_stmt_item(inner_stmt_id, ps_key))) {
        }
        need_do_real_prepare = true;
      } else if (OB_FAIL(session.prepare_ps_stmt(inner_stmt_id,
                                                stmt_info,
                                                client_stmt_id,
                                                duplicate_prepare,
                                                is_inner_sql))) {
      } else if (OB_FAIL(fill_result_set(client_stmt_id, *stmt_info, result))) {
        // prepare ps stmt succeeded, failure here requires close
        IGNORE_RETURN session.close_ps_stmt(*ps_cache, client_stmt_id);
      }
      if (OB_FAIL(ret)
          || need_do_real_prepare
          || duplicate_prepare) {
        if (NULL != stmt_item) {
          stmt_item->dec_ref_count();
        }
        if (NULL != stmt_info) {
          ps_cache->deref_stmt_info(inner_stmt_id); // need to decide whether to remove
        }
      }
      if (OB_SUCC(ret) && need_do_real_prepare) {
        if (OB_FAIL(do_real_prepare(stmt, context, result, is_inner_sql))) {
        }
      } else if (OB_SUCC(ret) && NULL != stmt_info) {
        context.is_sensitive_ = stmt_info->get_is_sensitive_sql();
      }
      if (OB_SUCC(ret)) {
        if (false == need_do_real_prepare) {
          ps_cache->inc_access_and_hit_count();
        } else {
          // Without hitting ps cache, only increase access count
          // The judgment logic here will cause pl to only increase the access count, not the hit_count, every prepare
          // So the ps cache hit rate seen from the ps related virtual tables will be relatively low
          ps_cache->inc_access_count();
        }
      }
    }
    OZ (session.store_query_string(cur_query));
  }
  return ret;
}

int ObSql::add_param_to_param_store(const ObObjParam &param,
                                    ParamStore &param_store)
{
  int ret = OB_SUCCESS;
  if (param.is_numeric_type()) {
    const_cast<ObObjParam &>(param).set_param_meta();
  }
  if (OB_FAIL(param_store.push_back(param))) {
  }
  return ret;
}

int ObSql::construct_param_store_from_parameterized_params(const ObPlanCacheCtx &phy_ctx,
                                               ParamStore &param_store)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(param_store.reserve(phy_ctx.fp_result_.parameterized_params_.count()))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < phy_ctx.fp_result_.parameterized_params_.count(); ++i) {
    const common::ObObjParam *param = phy_ctx.fp_result_.parameterized_params_.at(i);
    if (OB_FAIL(add_param_to_param_store(*param, param_store))) {
    }
  }
  return ret;
}

bool ObSql::is_exist_in_fixed_param_idx(const int64_t idx,
                                        const ObIArray<int64_t> &fixed_param_idx)
{
  bool bool_ret = false;
  for (int i = 0; i < fixed_param_idx.count(); ++i) {
    if (idx == fixed_param_idx.at(i)) {
      bool_ret = true;
      break;
    }
  }
  return bool_ret;
}

int ObSql::construct_ps_param_store(const ParamStore &params,
                                    const ParamStore &fixed_params,
                                    const ObIArray<int64_t> &fixed_params_idx,
                                    ParamStore &param_store)
{
  int ret = OB_SUCCESS;
  int64_t param_idx = 0;
  int64_t fixed_param_idx = 0;
  int64_t param_count = params.count() + fixed_params.count();
  if (fixed_params.count() != fixed_params_idx.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the number of fixed param does not match", K(fixed_params), K(fixed_params_idx));
  } else if (OB_FAIL(param_store.reserve(param_count))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < param_count; ++i) {
    if (is_exist_in_fixed_param_idx(i, fixed_params_idx)) {
      if (fixed_param_idx >= fixed_params.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Invalid index", K(fixed_param_idx), K(fixed_params));
      } else if (OB_FAIL(add_param_to_param_store(fixed_params.at(fixed_param_idx),
                                                  param_store))) {
      } else {
        LOG_TRACE("ps param is", K(fixed_params.at(fixed_param_idx)), K(i));
      }
      ++fixed_param_idx;
    } else {
      if (param_idx >= params.count()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Invalid index", K(param_idx), K(params));
      } else if (OB_FAIL(add_param_to_param_store(params.at(param_idx), param_store))) {
      } else {
        LOG_TRACE("ps param is", K(params.at(param_idx)), K(i));
      }
      ++param_idx;
    }
  }
  return ret;
}

int ObSql::construct_param_store(const ParamStore &params,
                                 ParamStore &param_store)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(param_store.reserve(params.count()))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    if (OB_FAIL(add_param_to_param_store(params.at(i), param_store))) {
    }
    LOG_TRACE("ps param is", K(params.at(i)), K(i));
  }
  return ret;
}

int ObSql::construct_parameterized_params(const ParamStore &params,
                                          ObPlanCacheCtx &phy_ctx)
{
  int ret = OB_SUCCESS;
  phy_ctx.fp_result_.parameterized_params_.reset();
  phy_ctx.fp_result_.parameterized_params_.set_allocator(&phy_ctx.allocator_);
  phy_ctx.fp_result_.parameterized_params_.set_capacity(params.count());
  for (int i = 0; OB_SUCC(ret) && i < params.count(); ++i) {
    if (OB_FAIL(phy_ctx.fp_result_.parameterized_params_.push_back(&params.at(i)))) {
    }
  }
  return ret;
}

int ObSql::clac_fixed_param_store(const stmt::StmtType stmt_type,
                                  const ObIArray<int64_t> &raw_params_idx,
                                  const ObIArray<ObPCParam *> &raw_params,
                                  ObIAllocator &allocator,
                                  ObSQLSessionInfo &session,
                                  ParamStore &fixed_param_store)
{
  int ret = OB_SUCCESS;
  ObCollationType collation_connection = static_cast<ObCollationType>(
                                         session.get_local_collation_connection());
  ObString literal_prefix;
  ObObjParam value;
  const bool is_paramlize = false;
  int64_t server_collation = CS_TYPE_INVALID;
  bool enable_decimal_int = false;
  bool enable_mysql_compatible_dates = false;
  if (raw_params.empty()) {
    // do nothing
  } else if (raw_params_idx.count() != raw_params.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the number of fixed param does not match",
    K(raw_params_idx.count()), K(raw_params.count()));
  } else if (OB_FAIL(fixed_param_store.reserve(raw_params_idx.count()))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(&session, enable_decimal_int))) {
  } else if (OB_FAIL(ObSQLUtils::check_enable_mysql_compatible_dates(&session, false /*is_ddl*/,
                       enable_mysql_compatible_dates))) {
  }
  for (int i = 0; OB_SUCC(ret) && i < raw_params.count(); ++i) {
    value.reset();
    ParseNode *raw_param = NULL;;
    if (OB_ISNULL(raw_params.at(i))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(raw_param = raw_params.at(i)->node_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObResolverUtils::resolve_const(raw_param,
                                                      stmt_type,
                                                      allocator,
                                                      collation_connection,
                                                      session.get_nls_collation_nation(),
                                                      session.get_timezone_info(),
                                                      value,
                                                      is_paramlize,
                                                      literal_prefix,
                                                      session.get_actual_length_semantics(),
                                                      static_cast<ObCollationType>(server_collation),
                                                      NULL, session.get_sql_mode(),
                                                      enable_decimal_int,
                                                      share::COMPAT_MYSQL57,
                                                      enable_mysql_compatible_dates,
                                                      session.get_min_const_integer_precision(),
                                                      stmt::T_ANONYMOUS_BLOCK == stmt_type))) {
    } else if (OB_FAIL(add_param_to_param_store(value, fixed_param_store))) {
    } else {
    }
  }
  return ret;
}

int ObSql::init_execute_params_for_ab(ObIAllocator &allocator,
                                      const ParamStore &params_store,
                                      ParamStore *&first_group_params)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(first_group_params)) {
    // do nothing
  } else if (OB_ISNULL(first_group_params = static_cast<ParamStore *>(allocator.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(first_group_params = new(first_group_params)ParamStore(ObWrapperAllocator(allocator)))) {
  }
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(ObPlanCacheValue::get_one_group_params(0, params_store, *first_group_params))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < first_group_params->count(); i++) {
      ObObjParam &obj_param = first_group_params->at(i);
      const ObObjParam &src_param = params_store.at(i);
      const ObSqlArrayObj *array_obj = NULL;
      if (OB_UNLIKELY(!src_param.is_ext_sql_array())) {
        // do not need set accuracy
      } else if (OB_ISNULL(array_obj = reinterpret_cast<const ObSqlArrayObj*>(src_param.get_ext()))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        obj_param.set_accuracy(array_obj->element_.get_accuracy());
      }
      obj_param.get_param_flag().is_batch_parameter_ = true;
    }
  }
  return ret;
}

int ObSql::reconstruct_pl_params_store(ObIAllocator &allocator,
                                       ObSqlCtx &context,
                                       const ParamStore &origin_params,
                                       ParamStore &pl_params,
                                       ParamStore *&pl_ab_params)
{
  int ret = OB_SUCCESS;
  if (context.is_batch_params_execute()) {
    ParamStore *first_group_params = &pl_params;
    if (OB_FAIL(init_execute_params_for_ab(allocator, origin_params, first_group_params))) {
    } else if (OB_ISNULL(pl_ab_params = static_cast<ParamStore *>(allocator.alloc(sizeof(ParamStore))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(pl_ab_params = new(pl_ab_params)ParamStore(ObWrapperAllocator(allocator)))) {
      // do nothing
    } else if (OB_FAIL(construct_param_store(origin_params,
                                             *pl_ab_params))) {
    }
  } else {
    if (OB_FAIL(construct_param_store(origin_params, pl_params))) {
    }
  }
  return ret;
}

int ObSql::reconstruct_ps_params_store(ObIAllocator &allocator,
                                       ObSqlCtx &context,
                                       const ParamStore &origin_params,
                                       const ParamStore &fixed_params,
                                       ObPsStmtInfo *ps_info,
                                       ParamStore &ps_params,
                                       ParamStore *&ps_ab_params)
{
  int ret = OB_SUCCESS;
  ParamStore *first_group_params = NULL;
  if (context.is_batch_params_execute()) {
    if (OB_FAIL(init_execute_params_for_ab(allocator, origin_params, first_group_params))) {
    } else if (OB_FAIL(construct_ps_param_store(*first_group_params,
                                                fixed_params,
                                                ps_info->get_raw_params_idx(),
                                                ps_params))) {
    } else if (OB_ISNULL(ps_ab_params = static_cast<ParamStore *>(allocator.alloc(sizeof(ParamStore))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(ps_ab_params = new(ps_ab_params)ParamStore(ObWrapperAllocator(allocator)))) {
      // do nothing
    } else if (OB_FAIL(construct_ps_param_store(origin_params,
                                                fixed_params,
                                                ps_info->get_raw_params_idx(),
                                                *ps_ab_params))) {
    }
  } else if (OB_FAIL(construct_ps_param_store(origin_params,
                                              fixed_params,
                                              ps_info->get_raw_params_idx(),
                                              ps_params))) {
  }
  return ret;
}

int ObSql::check_read_only_privilege(ParseResult &parse_result,
                                     ObExecContext &exec_ctx,
                                     ObSchemaGetterGuard &schema_guard,
                                     ObSqlTraits &sql_traits)
{
  int ret = OB_SUCCESS;
  bool read_only = false;
  ObPhysicalPlanCtx *pctx = exec_ctx.get_physical_plan_ctx();
  ObSQLSessionInfo *session = exec_ctx.get_my_session();
  sql_traits.is_readonly_stmt_ = ObSQLUtils::is_readonly_stmt(parse_result);
  sql_traits.is_cause_implicit_commit_
      = ObSQLUtils::cause_implicit_commit(parse_result);
  sql_traits.is_commit_stmt_ = ObSQLUtils::is_commit_stmt(parse_result);
  sql_traits.stmt_type_ = ObSQLUtils::get_sql_item_type(parse_result);
  const bool is_write_enabled = share::server_is_write_enabled();
  const bool is_standby_control_stmt = !is_write_enabled
      && ObSQLUtils::is_allowed_on_standby(sql_traits.stmt_type_);
  if (OB_ISNULL(pctx) || OB_ISNULL(session)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!(session->is_inner() && !session->is_user_session())
             && !is_write_enabled
             && !sql_traits.is_readonly_stmt_
             && !ObSQLUtils::is_allowed_on_standby(sql_traits.stmt_type_)) {
    ret = OB_STANDBY_DATABASE_READ_ONLY;
    LOG_WARN("standby server is read only", KR(ret), K(sql_traits));
  } else if (!is_standby_control_stmt
             && OB_FAIL(schema_guard.get_runtime_read_only(read_only))) {
  } else if (!is_standby_control_stmt
             && OB_FAIL(session->check_read_only_privilege(read_only,
                                                           sql_traits))) {
    LOG_WARN("failed to check read_only privilege", K(ret));
    if (ObSQLUtils::is_end_trans_stmt(parse_result)) {
      int et_ret = OB_SUCCESS;
      exec_ctx.set_need_disconnect(false);
      //FIXME qianfu NG_TRACE_EXT(set_need_disconnect, OB_ID(need_disconnect), false);
      LOG_WARN("is commit or rollback stmt, but fail to check read_only privilege, "
              "rollback", K(ret));
      int64_t plan_timeout = 0;
      if (OB_SUCCESS != (et_ret = session->get_query_timeout(plan_timeout))) {
      } else {
        pctx->set_timeout_timestamp(session->get_query_start_time() + plan_timeout);
        // explicitly rollback the transaction, if it fails, the connection will be disconnected
        if (OB_SUCCESS != (et_ret = ObSqlTransControl::explicit_end_trans(
                    exec_ctx, true))) {
        }
      }
    }
  }
  return ret;
}

int ObSql::handle_ps_execute(const ObPsStmtId client_stmt_id,
                             const stmt::StmtType stmt_type,
                             const ParamStore &params,
                             ObSqlCtx &context,
                             ObResultSet &result,
                             bool is_inner_sql)
{
  int ret = OB_SUCCESS;
  ParamStore *ps_ab_params = NULL;
  int get_plan_err = OB_SUCCESS;
  context.is_prepare_protocol_ = true;
  ObPsStmtId inner_stmt_id = client_stmt_id;
  context.stmt_type_ = stmt_type;

  // normal ps execute sql also a dynamic sql, here we adjust is_dynamic_sql_.
  context.is_dynamic_sql_ = !context.is_dynamic_sql_ ? !is_inner_sql : context.is_dynamic_sql_;

  ObIAllocator &allocator = result.get_mem_pool();
  ObSQLSessionInfo &session = result.get_session();
  ObExecContext &ectx = result.get_exec_context();
  ParamStore fixed_params( (ObWrapperAllocator(allocator)) );
  ParamStore ps_params( (ObWrapperAllocator(allocator)) );
  ObPsCache *ps_cache = ps_cache_;
  ObPlanCache *plan_cache = plan_cache_;
  bool use_plan_cache = session.get_local_ob_enable_plan_cache();
  ObPhysicalPlanCtx *pctx = ectx.get_physical_plan_ctx();
  ObSchemaGetterGuard *schema_guard = context.schema_guard_;
  int64_t origin_params_count = params.count();
  if (OB_ISNULL(ps_cache) || OB_ISNULL(pctx) || OB_ISNULL(schema_guard) || OB_ISNULL(plan_cache)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("physical plan context or ps plan cache is NULL or schema_guard is null",
              K(ret), K(pctx), K(ps_cache));
  } else if (!is_inner_sql && OB_FAIL(session.get_inner_ps_stmt_id(client_stmt_id,
                                                                    inner_stmt_id))) {
  } else {
    context.statement_id_ = inner_stmt_id;
    ObPsStmtInfoGuard guard;
    ObPsStmtInfo *ps_info = NULL;
    pctx->set_original_param_cnt(origin_params_count);
    pctx->set_orig_question_mark_cnt(origin_params_count);
    if (OB_FAIL(ps_cache->get_stmt_info_guard(inner_stmt_id, guard))) {
    } else if (OB_ISNULL(ps_info = guard.get_stmt_info())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (ps_info->get_question_mark_count() != origin_params_count) {
      ret = OB_INVALID_ARGUMENT;
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, "execute");
    } else if (OB_FAIL(clac_fixed_param_store(stmt_type,
                                              ps_info->get_raw_params_idx(),
                                              ps_info->get_fixed_raw_params(),
                                              allocator,
                                              session,
                                              fixed_params))) {
    } else if (OB_FAIL(reconstruct_ps_params_store(
        allocator, context, params, fixed_params, ps_info, ps_params, ps_ab_params))) {
    } else if (context.is_batch_params_execute() &&
        OB_ISNULL(ps_ab_params)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(construct_param_store(ps_params, pctx->get_param_store_for_update()))) {
    } else {
      const ObString &sql = !ps_info->get_no_param_sql().empty() ? ps_info->get_no_param_sql() : ps_info->get_ps_sql();
      context.cur_sql_ = sql;
      context.raw_sql_ = ps_info->get_ps_sql();
#ifndef NDEBUG
      LOG_INFO("Begin to handle execute statement", "sess_id", session.get_server_sid(), K(sql));
#endif

      if (!ps_info->get_fixed_raw_params().empty()) {
        pctx->set_ps_fixed_array_index(ps_info->get_raw_params_idx());
      }
      if (OB_FAIL(session.store_query_string(sql))) {
      } else if (FALSE_IT(generate_ps_sql_id(sql, context))) {
      } else if (OB_LIKELY(ObStmt::is_dml_stmt(stmt_type))) {
        //if plan not exist, generate plan
        ObPlanCacheCtx pc_ctx(sql, PC_PS_MODE, allocator, context, ectx);
        pc_ctx.fp_result_.pc_key_.key_id_ = inner_stmt_id;
        pc_ctx.normal_parse_const_cnt_ = ps_params.count();
        context.plan_key_.db_id_ = session.get_database_id();
        pc_ctx.set_is_parameterized_execute();
        pc_ctx.set_is_inner_sql(is_inner_sql);
        pc_ctx.ab_params_ = ps_ab_params;
        if (OB_FAIL(construct_parameterized_params(ps_params, pc_ctx))) {
        } else {
          if (!use_plan_cache) {
            /*do nothing*/
          } else if (OB_FAIL(pc_get_plan_and_fill_result(pc_ctx, result, get_plan_err,
                                          ectx.get_need_disconnect_for_update()))) {
          }

          if (OB_FAIL(ret)) {//do nothing
          } else if (!result.get_is_from_plan_cache()) {
            pctx->set_original_param_cnt(origin_params_count);
            pctx->get_param_store_for_update().reset();
            // Use SMART_CALL_LARGE for complex SQL plan generation when plan cache misses
            if (OB_FAIL(SMART_CALL_LARGE(handle_physical_plan(sql, context, result, pc_ctx, get_plan_err)))) {
            }
          }
          if (OB_SUCC(ret) && (OB_FAIL(after_get_plan(pc_ctx,
                                                      session,
                                                      result.get_physical_plan(),
                                                      result.get_is_from_plan_cache())))) {
          }
        }
      } else if (stmt::T_ANONYMOUS_BLOCK == stmt_type && !context.is_mock_prepare_) {
        ParseResult parse_result;
        MEMSET(&parse_result, 0, SIZEOF(ParseResult));
        if (OB_FAIL(generate_physical_plan(parse_result, NULL, context, result,
                                            false/*is_begin_commit_stmt*/, PC_PS_MODE))) {
        } else {
          const ObPsSqlMeta &sql_meta = ps_info->get_ps_sql_meta();
          const common::ObIArray<ObField> &param_fields = sql_meta.get_param_fields();
          int64_t field_column_cnt = 0;
          for (int64_t i = 0; OB_SUCC(ret) && i < param_fields.count(); ++i) {
            if (ObRoutineParamInOut::SP_PARAM_INOUT
                  == static_cast<ObRoutineParamInOut>(param_fields.at(i).inout_mode_)
                || ObRoutineParamInOut::SP_PARAM_OUT
                  == static_cast<ObRoutineParamInOut>(param_fields.at(i).inout_mode_)) {
              field_column_cnt++;
            }
          }
          OZ (result.reserve_field_columns(field_column_cnt));
          for (int64_t i = 0; OB_SUCC(ret) && i < field_column_cnt; ++i) {
            ObField field;
            field.type_.set_type(ObNullType);
            OZ (result.add_field_column(field));
          }
        }
      } else {
        if (stmt::T_CALL_PROCEDURE == stmt_type && !context.is_dynamic_sql_) {
          // call procedure stmt call always parse as dynamic sql
          context.is_dynamic_sql_ = true;
        }
        if ((stmt::T_CALL_PROCEDURE == stmt_type || stmt::T_ANONYMOUS_BLOCK == stmt_type) && !context.is_execute_call_stmt_) {
          context.is_execute_call_stmt_ = true;
        }
        ObParser parser(allocator, session.get_sql_mode(),
                        session.get_charsets4parser());
        ParseResult parse_result;
        ObSqlTraits sql_traits;
        ParseMode parse_mode = context.is_dynamic_sql_ ? DYNAMIC_SQL_MODE :
                                (context.session_info_->is_for_trigger_package() ? TRIGGER_MODE : STD_MODE);
        if (OB_FAIL(parser.parse(sql, parse_result, parse_mode))) {
        }
        context.is_sensitive_ |= parse_result.contain_sensitive_data_;

        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(check_read_only_privilege(parse_result, ectx, *schema_guard, sql_traits))) {
        } else if (OB_FAIL(generate_physical_plan(parse_result, NULL, context, result,
            false /*is_begin_commit_stmt*/, PC_PS_MODE))) {
        } // TODO Generate the path for physical plan distinction
      }
    }
  }
  return ret;
}

OB_INLINE int ObSql::handle_text_query(const ObString &stmt, ObSqlCtx &context, ObResultSet &result)
{
  int ret = OB_SUCCESS;
  //trim the sql first, let 'select c1 from t' and '  select c1 from t' and hit the same plan_cache
  ObString trimed_stmt = const_cast<ObString &>(stmt).trim();
  context.is_prepare_protocol_ = false;
  if (OB_FAIL(init_result_set(context, result))) {
  } else if (trimed_stmt.empty()) {
    ret = OB_ERR_EMPTY_QUERY;
    LOG_USER_ERROR(OB_ERR_EMPTY_QUERY);
    // Empty request, can be categorized as a known error of parser, no need to disconnect
    result.get_exec_context().set_need_disconnect(false);
    //FIXME qianfu NG_TRACE_EXT(set_need_disconnect, OB_ID(need_disconnect), false);
  }

  // file sql rewrite: materialise read_csv()/read_json()/... into inline tables
  ObArenaAllocator filesql_alloc("FileSqlRewrite");
  ObString filesql_stmt;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(filesql::fs_rewrite(filesql_alloc, result.get_session(), trimed_stmt, filesql_stmt))) {
    result.get_exec_context().set_need_disconnect(false);
  } else if (!filesql_stmt.empty()) {
    trimed_stmt = filesql_stmt;
  }
  if (OB_SUCC(ret)) {
  }

  ObIAllocator &allocator = THIS_WORKER.get_sql_arena_allocator();
  ObSQLSessionInfo &session = result.get_session();
  session.clear_ai_column_refs();
  
  ObExecContext& ectx = result.get_exec_context();
  int get_plan_err = OB_SUCCESS; //used for judge whether add plan to plan cache
  bool ai_strict_read = false;
  int strict_ret = session.get_sys_variable(SYS_VAR_AI_COLUMN_STRICT_READ, ai_strict_read);
  bool use_plan_cache = session.get_local_ob_enable_plan_cache()
                        && !(OB_SUCCESS == strict_ret && ai_strict_read);
  ObPlanCacheCtx *pc_ctx = NULL;
  bool is_begin_commit_stmt = false;

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (NULL == (pc_ctx = static_cast<ObPlanCacheCtx *>
                               (allocator.alloc(sizeof(ObPlanCacheCtx))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    context.cur_sql_ = trimed_stmt;
    pc_ctx = new (pc_ctx) ObPlanCacheCtx(trimed_stmt,
                                         PC_TEXT_MODE,
                                         allocator,
                                         context,
                                         ectx);
    if (trimed_stmt.length() == 6) {
      // Whether it is a COMMIT statement
      is_begin_commit_stmt = (0 == STRNCASECMP(trimed_stmt.ptr(), "commit", 6)
                              && !context.is_batch_params_execute());
    } else if (trimed_stmt.length() == 5) {
      is_begin_commit_stmt = (0 == STRNCASECMP(trimed_stmt.ptr(), "begin", 5)
                              && !context.is_batch_params_execute());
    }
    if (is_begin_commit_stmt) {
      // Record current statement is begin/commit statement, used for performance optimization
      pc_ctx->set_begin_commit_stmt();
    }
    uint64_t database_id = OB_INVALID_ID;

    if (OB_FAIL(session.get_database_id(database_id))) {
    } else if (FALSE_IT(context.plan_key_.db_id_ =
                                  (database_id == OB_INVALID_ID) ?
                                      OB_MOCK_DEFAULT_DATABASE_ID:
                                      database_id)) {
      // do nothing
    } else if (!use_plan_cache) {
      if (context.multi_stmt_item_.is_batched_multi_stmt()) {
        ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
        LOG_WARN("batched multi_stmt needs rollback");
      }
      // If it is a begin/commit statement, do not get the plan from the plan cache
    } else if (!is_begin_commit_stmt
        && OB_FAIL(pc_get_plan_and_fill_result(*pc_ctx, result, get_plan_err,
                                               ectx.get_need_disconnect_for_update()))) {
    }
  }

  if (OB_SUCC(ret) && !result.get_is_from_plan_cache()) { // did not get plan from plan cache, take the long path to generate plan
    if (OB_FAIL(SMART_CALL_LARGE(handle_physical_plan(trimed_stmt, context, result, *pc_ctx, get_plan_err)))) {
    }
  }

  if ((NULL != pc_ctx) && !(pc_ctx->sql_ctx_.is_sensitive_)) {
    // if sql context contains sensitive data, can not flush sql info to trace.log
  }

  // set auto-increment related param into physical plan ctx
  // if get plan from plan cache, reset its auto-increment variable here
  // do not set variable for hidden primary key; its default value is 1
  if (OB_SUCC(ret)) {
    if (!context.is_text_ps_mode_
        && OB_FAIL(after_get_plan(*pc_ctx, session, result.get_physical_plan(),
                                  result.get_is_from_plan_cache()))) {
    }
  }
  if (NULL != pc_ctx) {
    pc_ctx->~ObPlanCacheCtx();
  }
  return ret;
}

int ObSql::generate_stmt(ParseResult &parse_result,
                         ObPlanCacheCtx *pc_ctx,
                         ObSqlCtx &context,
                         ObIAllocator &allocator,
                         ObResultSet &result,
                         ObStmt *&stmt,
                         ParseResult *outline_parse_result)
{
  int ret = OB_SUCCESS;
  uint64_t session_id = 0;
  ObResolverParams resolver_ctx;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  ObSchemaChecker *schema_checker = NULL;

  int64_t last_mem_usage = allocator.total();
  int64_t resolver_mem_usage = 0;
  if (OB_FAIL(sanity_check(context))) {
  } else {
    if (result.get_session().get_session_type() != ObSQLSessionInfo::INNER_SESSION) {
      session_id = result.get_session().get_sessid_for_table();
    } else {
      session_id = OB_INVALID_ID; // internal session, not affected by table_schema->session_id visibility, can see tables during query and table creation process
    }
    schema_checker = OB_NEWx(ObSchemaChecker, (&allocator));
    if (OB_UNLIKELY(NULL == schema_checker)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("Failed to malloc ObSchemaChecker", K(ret));
    } else if (NULL == context.schema_guard_) {
      ret = OB_ERR_UNEXPECTED;
    }
  }

  if (OB_SUCC(ret)) {
    resolver_ctx.allocator_  = &allocator;
    resolver_ctx.schema_checker_ = schema_checker;
    resolver_ctx.secondary_namespace_ = context.secondary_namespace_;
    resolver_ctx.session_info_ = context.session_info_;
    bind_resolver_runtime_services(resolver_ctx);
    resolver_ctx.expr_factory_ = result.get_exec_context().get_expr_factory();
    resolver_ctx.stmt_factory_ = result.get_exec_context().get_stmt_factory();
    resolver_ctx.cur_sql_ = context.cur_sql_;
    resolver_ctx.is_restore_ = context.is_restore_;
    resolver_ctx.is_ddl_from_primary_ = context.is_ddl_from_primary_;
    resolver_ctx.is_cursor_ = context.is_cursor_;
    resolver_ctx.is_batch_stmt_ = context.is_batch_params_execute();
    resolver_ctx.batch_stmt_num_ = context.get_batch_params_count();
    resolver_ctx.need_check_col_dup_
      = !(context.is_prepare_protocol_ && parse_result.question_mark_ctx_.by_ordinal_);
    resolver_ctx.external_param_info_.by_name_
        = parse_result.question_mark_ctx_.by_name_ || NULL != context.secondary_namespace_; //static sql in PL must be by name
    resolver_ctx.outline_parse_result_ = outline_parse_result;
    resolver_ctx.is_execute_call_stmt_ = context.is_execute_call_stmt_;
    if (NULL != pc_ctx) {
      resolver_ctx.select_item_param_infos_ = &pc_ctx->select_item_param_infos_;
    }
    plan_ctx = result.get_exec_context().get_physical_plan_ctx();
    if (OB_ISNULL(plan_ctx) || OB_ISNULL(result.get_exec_context().get_stmt_factory())
        || OB_ISNULL(result.get_exec_context().get_expr_factory())) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_ISNULL(resolver_ctx.query_ctx_ =
        result.get_exec_context().get_stmt_factory()->get_query_ctx())) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("allocate query context failed", K(ret));
    } else {
      resolver_ctx.query_ctx_->sql_schema_guard_.set_schema_guard(context.schema_guard_);
      resolver_ctx.expr_factory_->set_query_ctx(resolver_ctx.query_ctx_);
      if (OB_FAIL(resolver_ctx.schema_checker_->init(resolver_ctx.query_ctx_->sql_schema_guard_, session_id))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (NULL != resolver_ctx.secondary_namespace_
        && NULL !=  resolver_ctx.secondary_namespace_->get_external_ns()) {
      resolver_ctx.package_guard_ =
        &resolver_ctx.secondary_namespace_->get_external_ns()->get_resolve_ctx().package_guard_;
    }
  }
  if (OB_SUCC(ret)) {
    resolver_ctx.is_prepare_protocol_ = context.is_prepare_protocol_;
    resolver_ctx.is_prepare_stage_ = context.is_prepare_stage_;
    resolver_ctx.is_mock_prepare_ = context.is_mock_prepare_;
    resolver_ctx.is_dynamic_sql_ = context.is_dynamic_sql_;
    resolver_ctx.statement_id_ = context.statement_id_;
    resolver_ctx.param_list_ = &plan_ctx->get_param_store();
    resolver_ctx.sql_proxy_ = GCTX.sql_proxy_;
  }

  if (OB_FAIL(ret)) {
  } else if (stmt::T_ANONYMOUS_BLOCK == context.stmt_type_ && context.is_prepare_protocol_ && !context.is_prepare_stage_) {
    // anonymous + ps will not do parser in execute stage, therefore should not check parser_result
    //do nothing...
  } else if (OB_ISNULL(parse_result.result_tree_)
        || OB_ISNULL(parse_result.result_tree_->children_)
        || OB_ISNULL(parse_result.result_tree_->children_[0])) {
      ret = OB_INVALID_ARGUMENT;
      SQL_LOG(WARN, "invalid args",
              KP(parse_result.result_tree_),
              KP(parse_result.result_tree_->children_),
              KP(parse_result.result_tree_->children_[0]));
  }

  if (OB_SUCC(ret)) {
      // set # of question marks
      if (context.is_prepare_protocol_ && !context.is_prepare_stage_) {
        resolver_ctx.query_ctx_->set_questionmark_count(plan_ctx->get_param_store().count());
        LOG_DEBUG("question mark size is ", K(plan_ctx->get_param_store()));
      } else {
        resolver_ctx.query_ctx_->set_questionmark_count(static_cast<int64_t>(parse_result.question_mark_ctx_.count_));
      }

      HEAP_VAR(ObResolver, resolver, resolver_ctx) {
      NG_TRACE(resolve_begin);
      if (OB_FAIL(plan_ctx->build_subschema_ctx_by_param_store(context.schema_guard_))) {
      } else if (stmt::T_ANONYMOUS_BLOCK == context.stmt_type_
          && context.is_prepare_protocol_
          && !context.is_prepare_stage_
          && !context.is_mock_prepare_) {
        ParseNode tmp_node;
        tmp_node.type_ = T_SP_ANONYMOUS_BLOCK;
        ret = resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT, tmp_node, stmt);
      } else {
        ret = resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT, *parse_result.result_tree_->children_[0], stmt);
      }

      // set const param constraint after resolving
      context.all_plan_const_param_constraints_ = &(resolver_ctx.query_ctx_->all_plan_const_param_constraints_);
      context.all_possible_const_param_constraints_ = &(resolver_ctx.query_ctx_->all_possible_const_param_constraints_);
      context.all_equal_param_constraints_ = &(resolver_ctx.query_ctx_->all_equal_param_constraints_);
      context.all_pre_calc_constraints_ = &(resolver_ctx.query_ctx_->all_pre_calc_constraints_);
      context.all_expr_constraints_ = &(resolver_ctx.query_ctx_->all_expr_constraints_);
      context.all_priv_constraints_ = &(resolver_ctx.query_ctx_->all_priv_constraints_);
      context.need_match_all_params_ = resolver_ctx.query_ctx_->need_match_all_params_;
      context.all_local_session_vars_ = &(resolver_ctx.query_ctx_->all_local_session_vars_);
      context.cur_stmt_ = stmt;
      NG_TRACE(resolve_end);
      if (OB_SUCC(ret)) {
        if (NULL != pc_ctx && NULL != pc_ctx->exec_ctx_.get_physical_plan_ctx()) {
          if (OB_FAIL(pc_ctx->exec_ctx_.get_physical_plan_ctx()->init_datum_param_store())) {
          }
        }
      }
      //add ref obj schema version to PL and ps info
      if (OB_SUCC(ret)) {
        if (OB_FAIL(result.get_ref_objects().assign(resolver_ctx.query_ctx_->global_dependency_tables_))) {
        }
      }
      if (OB_FAIL(ret)) {
        /* for audit */
        if (NULL != stmt) {
          result.set_stmt_type(stmt->get_stmt_type());
        }
        SQL_LOG(WARN, "failed to resolve", K(ret));
      } else {
        // process stmt
        if (NULL != stmt && NULL != resolver_ctx.query_ctx_) {
          SQL_LOG(DEBUG, "SET STMT PARAM COUNT", K(resolver.get_params().prepare_param_count_), K(&resolver_ctx));
          // secondary_namespace_ is not empty, indicating that it is the prepare stage of sql in PL
          // PL dynamic SQL needs its external parameters rebuilt after resolving.
          bool need_rebuild = (resolver_ctx.is_dynamic_sql_ &&
          OB_NOT_NULL(result.get_session().get_pl_context()) && resolver_ctx.is_prepare_stage_);
          if (need_rebuild) {
            if (OB_FAIL(result.get_external_retrieve_info().build(*stmt,
                result.get_session(),
                resolver_ctx.secondary_namespace_,
                resolver_ctx.is_dynamic_sql_,
                resolver.get_params().external_param_info_.params_))) {
            } else {
              if (result.get_external_params().empty() && result.get_into_exprs().empty()) {
                if (resolver_ctx.query_ctx_->get_sql_stmt().empty()) {
                  resolver_ctx.query_ctx_->set_sql_stmt(parse_result.input_sql_, parse_result.input_sql_len_);
                }
                result.get_route_sql() = resolver_ctx.query_ctx_->get_sql_stmt();
                resolver_ctx.query_ctx_->set_prepare_param_count(parse_result.question_mark_ctx_.count_);
              } else {
                // For SQL statements within PL, the specific prepare_param_count_
                // can only be determined after resolver completion.
                resolver_ctx.query_ctx_->set_prepare_param_count(resolver.get_params().prepare_param_count_);
              }
            }
          } else {
            if (stmt::T_ANONYMOUS_BLOCK == context.stmt_type_ && context.is_prepare_protocol_) {
              resolver_ctx.query_ctx_->set_sql_stmt(context.cur_sql_);
            } else {
              resolver_ctx.query_ctx_->set_sql_stmt(parse_result.input_sql_, parse_result.input_sql_len_);
            }
            result.get_route_sql() = resolver_ctx.query_ctx_->get_sql_stmt();
            resolver_ctx.query_ctx_->set_prepare_param_count(parse_result.question_mark_ctx_.count_);
          }
          if (OB_SUCC(ret)) {
            result.set_stmt_type(stmt->get_stmt_type()); // used by query retry
            result.set_literal_stmt_type(resolver_ctx.query_ctx_->get_literal_stmt_type()); // used by show warn, show trace
            ObDMLStmt *dml_stmt = NULL;
            ObVariableSetStmt *variable_set_stmt = NULL;
            if ((dml_stmt = dynamic_cast<ObDMLStmt*>(stmt)) != NULL) {
              const ObGlobalHint &global_hint = resolver_ctx.query_ctx_->get_global_hint();
              result.set_is_calc_found_rows(dml_stmt->is_calc_found_rows());
              plan_ctx->set_is_affect_found_row(dml_stmt->is_affect_found_rows());
              context.force_print_trace_ = global_hint.force_trace_log_;
              if (MpQuery == context.exec_type_ && global_hint.log_level_.length() > 0) {
                const ObString &log_level = global_hint.log_level_;
                if (OB_UNLIKELY(OB_SUCCESS != process_thread_log_id_level_map(log_level.ptr(),
                                                                              log_level.length()))) {
                }
              }
            } else if ((variable_set_stmt = dynamic_cast<ObVariableSetStmt*>(stmt)) != NULL) {
              result.set_has_global_variable(variable_set_stmt->has_global_variable());
            }
          }
          if (OB_SUCC(ret)) {
          } else {
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
          SQL_LOG(WARN, "failed to generate stmt", K(ret));
        }
      }
      } // end HEAP_VAR(ObResolver)
  }
  resolver_mem_usage = allocator.total() - last_mem_usage;
  return ret;
}

int ObSql::generate_physical_plan(ParseResult &parse_result,
                                  ObPlanCacheCtx *pc_ctx,
                                  ObSqlCtx &sql_ctx,
                                  ObResultSet &result,
                                  const bool is_begin_commit_stmt,
                                  const PlanCacheMode mode,
                                  ParseResult *outline_parse_result /* null */ )
{
  int ret = OB_SUCCESS;
  bool is_valid = true;
  ObStmt *basic_stmt = NULL;
  ObIAllocator &allocator = result.get_mem_pool();
  ObStmtNeedPrivs stmt_need_privs;
  stmt_need_privs.need_privs_.set_allocator(&allocator);
  if (OB_FAIL(sanity_check(sql_ctx))) {
  } else if (OB_FAIL(generate_stmt(parse_result,
                                   pc_ctx,
                                   sql_ctx,
                                   allocator,
                                   result,
                                   basic_stmt,
                                   outline_parse_result))) {
  } else if (OB_ISNULL(basic_stmt)) {
    ret = OB_ERR_UNEXPECTED;
    // begin/commit statement does not need to check privilege
  } else if (!is_begin_commit_stmt
          && OB_FAIL(ObPrivilegeCheck::check_privilege_new(sql_ctx,
                                                           basic_stmt,
                                                           stmt_need_privs))) {
    LOG_WARN("Failed to check ora privilege info", K(ret), K(*basic_stmt));
  } else if (OB_FAIL(ObPrivilegeCheck::check_password_expired(sql_ctx,
                                                              basic_stmt->get_stmt_type()))) {
  } else if ((sql_ctx.is_batch_params_execute()) &&
             NULL != pc_ctx &&
             OB_FAIL(check_batched_multi_stmt_after_resolver(*pc_ctx,
                                                             *basic_stmt,
                                                             is_valid))) {
  } else if (!is_valid) {
    ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
    LOG_WARN("batched multi_stmt needs rollback", K(ret));
  } else if (basic_stmt->is_dml_stmt()
             || basic_stmt->is_explain_stmt()
            ) {
    if (OB_FAIL(generate_plan(parse_result, pc_ctx, sql_ctx, result, mode, basic_stmt,
                             stmt_need_privs))) {
    }
  } else if (stmt::T_EXECUTE == basic_stmt->get_stmt_type() &&
             stmt::T_CALL_PROCEDURE != static_cast<ObExecuteStmt*>(basic_stmt)->get_prepare_type()) {
    if (OB_FAIL(handle_text_execute(basic_stmt, sql_ctx, result))) {
    }
  } else {
    ObICmd *cmd = dynamic_cast<ObICmd*>(basic_stmt);
    if (OB_UNLIKELY(NULL == cmd)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      result.set_cmd(cmd);
      result.get_session().set_cur_sql_id(sql_ctx.sql_id_);
      if (!is_begin_commit_stmt
        && OB_FAIL(fill_result_set(result, &sql_ctx, mode, *basic_stmt))) {
      }
    }
  }
  return ret;
}

int ObSql::generate_plan(ParseResult &parse_result,
                        ObPlanCacheCtx *pc_ctx,
                        ObSqlCtx &sql_ctx,
                        ObResultSet &result,
                        const PlanCacheMode mode,
                        ObStmt *basic_stmt,
                        ObStmtNeedPrivs &stmt_need_privs)
{
  int ret = OB_SUCCESS;
  uint64_t aggregate_setting = 0;
  ObPhysicalPlanCtx *pctx = result.get_exec_context().get_physical_plan_ctx();
  ObPlanCache *plan_cache = plan_cache_;
  bool allow_audit = false;
  if (OB_ISNULL(pctx) || OB_ISNULL(basic_stmt) ||
      OB_ISNULL(result.get_exec_context().get_expr_factory()) ||
      OB_ISNULL(plan_cache)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(result.get_exec_context().get_stmt_factory()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(fill_result_set(result, &sql_ctx, mode, *basic_stmt))) {
  } else if (OB_FAIL(sql_ctx.session_info_->get_sys_variable(
                      share::SYS_VAR__AGGREGATION_OPTIMIZATION_SETTINGS,
                      aggregate_setting))) {
  } else {
    ObDMLStmt *stmt = static_cast<ObDMLStmt*>(basic_stmt);
    stmt->get_query_ctx()->root_stmt_ = stmt;
    const ObGlobalHint &global_hint = stmt->get_query_ctx()->get_global_hint();
    HEAP_VAR(ObOptimizerContext, optctx, sql_ctx.session_info_,
                              &result.get_exec_context(),
                              &result.get_exec_context().get_stmt_factory()->get_query_ctx()->sql_schema_guard_,
                              opt_stat_mgr_,
                              result.get_mem_pool(),
                              &pctx->get_param_store(),
                              self_addr_,
                              global_hint,
                              *result.get_exec_context().get_expr_factory(),
                              stmt,
                              result.is_ps_protocol(),
                              result.get_exec_context().get_stmt_factory()->get_query_ctx()) {
    optctx.set_aggregation_optimization_settings(aggregate_setting);
    pctx->set_field_array(result.get_field_columns());
    pctx->set_is_ps_protocol(result.is_ps_protocol());
    bool is_restore = false;
    ObOptimizer optimizer(optctx);

    ObLogPlan *logical_plan = NULL;
    ObPhysicalPlan *phy_plan = NULL;
    ObCacheObjGuard& guard = result.get_cache_obj_guard();
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObCacheObjectFactory::alloc(*plan_cache, guard,
                                                    ObLibCacheNameSpace::NS_CRSR))) {
    } else if (FALSE_IT(phy_plan = static_cast<ObPhysicalPlan*>(guard.get_cache_obj()))) {
      // do nothing
    } else if (OB_UNLIKELY(NULL == phy_plan)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("Failed to alloc physical plan from tc factory", K(ret));
    } else {
      if (NULL != pc_ctx) {
        pc_ctx->should_add_plan_ = true;
      }
    }

    if (OB_SUCC(ret)) {
      phy_plan->set_fetch_cur_time(stmt->get_fetch_cur_time());
      phy_plan->set_stmt_type(stmt->get_stmt_type());
      phy_plan->set_literal_stmt_type(stmt->get_query_ctx()->get_literal_stmt_type());
      if (phy_plan->get_fetch_cur_time() && !pctx->has_cur_time()) {
        pctx->set_cur_time(ObTimeUtility::current_time(), *(sql_ctx.session_info_));
      }
      pctx->set_last_trace_id(sql_ctx.session_info_->get_last_trace_id());
    }

    ObSQLSessionInfo *session_info = result.get_exec_context().get_my_session();
    BEGIN_OPT_TRACE(session_info, ObString(strlen(sql_ctx.sql_id_), sql_ctx.sql_id_));
    OPT_TRACE_ENV;
    OPT_TRACE_SESSION_INFO;
    OPT_TRACE_PARAMETERS;
    OPT_TRACE_TITLE("CURRENT SQL TEXT");
    OPT_TRACE("sql_id =", ObString(strlen(sql_ctx.sql_id_), sql_ctx.sql_id_));
    OPT_TRACE(ObString(parse_result.input_sql_len_, parse_result.input_sql_));

    // hint seed injected random status rand
    const ObQueryHint &query_hint = stmt->get_query_ctx()->get_query_hint();
    if (query_hint.has_outline_data() || session_info->is_inner()){
      // if there is outline data no error inject
    } else {
      int64_t tmp_ret = (OB_E(EventTable::EN_GENERATE_RANDOM_PLAN) OB_SUCCESS);
      if (OB_SUCCESS == tmp_ret) {
        // do nothing
      } else {
        time_t seed = OB_ERROR == tmp_ret ? time(NULL) : std::abs(tmp_ret);
        stmt->get_query_ctx()->set_injected_random_status(true);
        stmt->get_query_ctx()->set_random_plan_seed(seed);
        LOG_INFO("The random seed for plan gen is ", K(seed));
        OPT_TRACE("The random seed for plan gen is ", seed);
      }
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(transform_stmt(&stmt->get_query_ctx()->sql_schema_guard_,
                                      opt_stat_mgr_,
                                      &self_addr_,
                                      phy_plan,
                                      result.get_exec_context(),
                                      stmt))) {
    } else if (OB_FAIL(generate_stmt_with_reconstruct_sql(stmt,
                                                          pc_ctx,
                                                          sql_ctx,
                                                          result,
                                                          phy_plan))) {
    } else if (OB_FALSE_IT(optctx.set_root_stmt(stmt))) {
    } else if (OB_FAIL(optimize_stmt(optimizer, *(sql_ctx.session_info_),
                                      *stmt, logical_plan))) {
    } else if (OB_FAIL(create_expr_constraints(*stmt->get_query_ctx(),
                                                result.get_exec_context()))){
    } else if (OB_ISNULL(logical_plan)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(code_generate(sql_ctx, result, stmt,
                                     stmt_need_privs,
                                     logical_plan, phy_plan))) {
    } else if (OB_FAIL(prepare_outline_for_phy_plan(logical_plan,
                                                    phy_plan))) {
    } else if (logical_plan->get_optimizer_context().is_online_ddl()) {
      int tmp_ret = OB_SUCCESS;
      ObExplainDisplayOpt option;
      option.with_tree_line_ = false;
      ObSqlPlan sql_plan(logical_plan->get_allocator());
      ObSEArray<common::ObString, 32> plan_strs;
      if (OB_TMP_FAIL(sql_plan.print_sql_plan(logical_plan->get_plan_root(),
                                          EXPLAIN_EXTENDED,
                                          option,
                                          plan_strs))) {
      } else {
        _OB_LOG(INFO, "ddl sql:%.*s", parse_result.input_sql_len_, parse_result.input_sql_);
        LOG_INFO("ddl plan");
        for (int64_t i = 0; OB_SUCCESS == tmp_ret && i < plan_strs.count(); i++) {
          _OB_LOG(INFO, "%.*s", plan_strs.at(i).length(), plan_strs.at(i).ptr());
        }
        LOG_INFO("ddl stmt:", KPC(stmt));
      }
    }
    END_OPT_TRACE(session_info);
    if (OB_SUCC(ret)) {
      ObSqlPlan sql_plan(result.get_mem_pool());
      if (stmt->is_explain_stmt()) {
        // do nothing
      } else if (OB_FAIL(sql_plan.store_sql_plan(logical_plan, phy_plan))) {
      } else {
        phy_plan->set_record_plan_info(true);
      }
    }

    // memory debug for sql work arena
    if (OB_UNLIKELY(result.get_mem_pool().total() > SQL_MEM_SIZE_LIMIT)) {
      int64_t total_mem_used = result.get_mem_pool().total();
      LOG_INFO("[SQL MEM USAGE] use too much memory", K(total_mem_used),
                K(ObString(parse_result.input_sql_len_, parse_result.input_sql_)));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(stmt->get_query_ctx())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      result.get_exec_context().reference_my_plan(phy_plan);
    }
    } // end HEAP_VAR optctx
  }
  return ret;
}

int ObSql::generate_stmt_with_reconstruct_sql(ObDMLStmt* &stmt,
                                              ObPlanCacheCtx *pc_ctx,
                                              ObSqlCtx &sql_ctx,
                                              ObResultSet &result,
                                              ObPhysicalPlan *phy_plan)
{
  int ret = OB_SUCCESS;
  ObString sql;
  ObObjPrintParams print_param;
  ObSQLSessionInfo *session = sql_ctx.session_info_;
  ObPhysicalPlanCtx *phy_plan_ctx = NULL;
  if (OB_ISNULL(session) || OB_ISNULL(pc_ctx) ||
      OB_ISNULL(stmt) || (OB_ISNULL(stmt->get_query_ctx())) ||
      OB_ISNULL(phy_plan_ctx=pc_ctx->exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if ((OB_E(EventTable::EN_GENERATE_PLAN_WITH_RECONSTRUCT_SQL) OB_SUCCESS) == OB_SUCCESS) {
    //do nothing
  } else if (!session->is_user_session()) {
    //do nothing
  } else if (OB_FAIL(ObSQLUtils::reconstruct_sql(pc_ctx->allocator_,
                                                stmt,
                                                sql,
                                                sql_ctx.schema_guard_,
                                                print_param,
                                                &phy_plan_ctx->get_param_store(),
                                                session))) {
  } else {
    ObStmt *basic_stmt = NULL;
    ParseResult parse_result;
    ObParser parser(pc_ctx->allocator_,
                    session->get_sql_mode(),
                    session->get_charsets4parser());
    stmt->get_query_ctx()->global_dependency_tables_.reuse();
    if (OB_FAIL(parser.parse(sql, parse_result))) {
    } else if (OB_FAIL(generate_stmt(parse_result,
                                    pc_ctx,
                                    sql_ctx,
                                    result.get_mem_pool(),
                                    result,
                                    basic_stmt,
                                    NULL))) {
    } else if (OB_ISNULL(basic_stmt) || !basic_stmt->is_dml_stmt()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FALSE_IT(stmt = static_cast<ObDMLStmt*>(basic_stmt))) {
    } else if (OB_FAIL(transform_stmt(&stmt->get_query_ctx()->sql_schema_guard_,
                                      opt_stat_mgr_,
                                      &self_addr_,
                                      phy_plan,
                                      result.get_exec_context(),
                                      stmt,
                                      true))) {
    }
  }
  return ret;
}

int ObSql::prepare_outline_for_phy_plan(ObLogPlan *logical_plan,
                                        ObPhysicalPlan *phy_plan)
{
  int ret = OB_SUCCESS;
  void *tmp_ptr = NULL;
  char *buf = NULL;
  PlanText plan_text;

  if (OB_ISNULL(logical_plan) || OB_ISNULL(phy_plan)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(NULL == (tmp_ptr = logical_plan->get_allocator().alloc(OB_MAX_SQL_LENGTH)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to alloc memory", K(ret));
  } else if (FALSE_IT(buf = static_cast<char *>(tmp_ptr))) {
  } else {
    //print outline data
    plan_text.buf_ = buf;
    plan_text.buf_len_ = OB_MAX_SQL_LENGTH;
    if (OB_FAIL(ObSqlPlan::get_plan_outline_info_one_line(plan_text, logical_plan))) {
    } else if (OB_FAIL(ob_write_string(phy_plan->get_allocator(),
                                       ObString(plan_text.pos_, plan_text.buf_),
                                       phy_plan->stat_.outline_data_))) {
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    //print used hint
    plan_text.pos_ = 0;
    if (OB_FAIL(ObSqlPlan::get_plan_used_hint_info_one_line(plan_text, logical_plan))) {
    } else if (OB_FAIL(ob_write_string(phy_plan->get_allocator(),
                                       ObString(plan_text.pos_, plan_text.buf_),
                                       phy_plan->stat_.hints_info_))) {
    }
  }
  //free temp buffer
  if (NULL != tmp_ptr && NULL != logical_plan) {
    logical_plan->get_allocator().free(tmp_ptr);
  }
  return ret;
}
// stmt full const folding.
// To rewrite layer const folding extracted function
int ObSql::calc_pre_calculable_exprs(
    ObIArray<ObHiddenColumnItem> &calculable_exprs,
    ObExecContext &exec_ctx,
    ObDMLStmt &stmt,
    ObPhysicalPlan &phy_plan)
{
  int ret = OB_SUCCESS;
  stmt::StmtType stmt_type = stmt::T_NONE;
  ObDList <ObSqlExpression> pre_calc_exprs;
  ObPhysicalPlanCtx *plan_ctx = exec_ctx.get_physical_plan_ctx();
  ObSQLSessionInfo *session_info = exec_ctx.get_my_session();
  ObRawExprFactory *expr_factory = exec_ctx.get_expr_factory();
  if (OB_ISNULL(plan_ctx) || OB_ISNULL(session_info) || OB_ISNULL(expr_factory)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (stmt.is_explain_stmt()) {
    ObDMLStmt *real_stmt = static_cast<ObExplainStmt&>(stmt).get_explain_query_stmt();
    if (OB_ISNULL(real_stmt)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      stmt_type = real_stmt->get_stmt_type();
    }
  } else {
    stmt_type = stmt.get_stmt_type();
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < calculable_exprs.count(); i++) {
    bool transformed = false;
    if (OB_FAIL(ObTransformPreProcess::transform_expr(*expr_factory,
                                                      *session_info,
                                                      calculable_exprs.at(i).expr_,
                                                      transformed))) {
    }
  }
  if (OB_SUCC(ret)) {
    // is_ignore should be set before pre_calculate, which is used in column_conv
    bool is_ignore_stmt = false;
    bool need_fetch_cur_time = false;
    ObDelUpdStmt *modify_stmt = dynamic_cast<ObDelUpdStmt*>(&stmt);
    if (NULL != modify_stmt) {
      is_ignore_stmt = modify_stmt->is_ignore();
    }

    if (OB_FAIL(calc_pre_calculable_exprs(stmt, calculable_exprs,
                                          is_ignore_stmt, exec_ctx, phy_plan))) {
    }
  }
  return ret;
}

int ObSql::transform_stmt(ObSqlSchemaGuard *sql_schema_guard,
                          common::ObOptStatManager *opt_stat_mgr,
                          common::ObAddr *self_addr,
                          ObPhysicalPlan *phy_plan,
                          ObExecContext &exec_ctx,
                          ObDMLStmt *&stmt,
                          bool ignore_trace_event)
{
  int ret = OB_SUCCESS;
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_rewrite);
  ObDMLStmt *transform_stmt = stmt;
  int64_t last_mem_usage = exec_ctx.get_allocator().total();
  int64_t transformer_mem_usage = 0;
  //get transform stmt
  if (OB_ISNULL(stmt) || OB_ISNULL(stmt->get_query_ctx()) || OB_ISNULL(sql_schema_guard) ||
      OB_ISNULL(sql_schema_guard->get_schema_guard()) ||
      OB_ISNULL(opt_stat_mgr) || OB_ISNULL(self_addr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(exec_ctx.get_my_session())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (stmt->is_explain_stmt()) {
    if (OB_ISNULL(transform_stmt = static_cast<ObExplainStmt*>(stmt)->get_explain_query_stmt())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("Explain query stmt is NULL", K(ret));
    }
  } else { }//do nothing

  ObTransformerCtx trans_ctx;
  ObSchemaChecker schema_checker;

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(schema_checker.init(*sql_schema_guard))) {
  } else {
    trans_ctx.allocator_ = &exec_ctx.get_allocator();
    trans_ctx.schema_checker_ = &schema_checker;
    trans_ctx.session_info_ = exec_ctx.get_my_session();
    trans_ctx.exec_ctx_ = &exec_ctx;
    trans_ctx.expr_factory_ = exec_ctx.get_expr_factory();
    trans_ctx.stmt_factory_ = exec_ctx.get_stmt_factory();
    trans_ctx.opt_stat_mgr_ = opt_stat_mgr;
    trans_ctx.sql_schema_guard_ = sql_schema_guard;
    trans_ctx.self_addr_ = self_addr;
    // trans_ctx.merged_version_ = merged_version;

    trans_ctx.phy_plan_ = phy_plan;
  }

  NG_TRACE(transform_begin);
  OPT_TRACE_TITLE("START TRANSFORM");
  bool need_transform = transform_stmt->is_valid_transform_stmt();
  if (ignore_trace_event) {
    //do nothing
  } else if ((OB_E(EventTable::EN_GENERATE_PLAN_WITH_RECONSTRUCT_SQL)OB_SUCCESS) != OB_SUCCESS) {
    if (transform_stmt->is_dml_write_stmt()) {
      need_transform = false;
    }
  }
  if (OB_SUCC(ret) && need_transform) {
    ObTransformerImpl transformer(&trans_ctx);
    if (OB_FAIL(transformer.transform(transform_stmt))) {
    } else if (stmt->is_explain_stmt()) {
      static_cast<ObExplainStmt*>(stmt)->set_explain_query_stmt(transform_stmt);
    } else {
      bool or_expansion_happened = false;
      if (OB_FAIL(transformer.get_cost_based_trans_happened(OR_EXPANSION,
                                                            or_expansion_happened))) {
      } else if (or_expansion_happened) {
        if (OB_ISNULL(exec_ctx.get_physical_plan_ctx())) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          exec_ctx.get_physical_plan_ctx()->set_or_expand_transformed(true);
        }
      }
      if (OB_SUCC(ret)) {
        stmt = transform_stmt;
      }
    }
  }

  if (OB_SUCC(ret)) {
    OPT_TRACE("after transform, sql is\n", transform_stmt);
    OPT_TRACE_TIME_USED;
    OPT_TRACE_MEM_USED;
  }
  transformer_mem_usage = exec_ctx.get_allocator().total() - last_mem_usage;
  return ret;
}

int ObSql::optimize_stmt(
    ObOptimizer &optimizer,
    const ObSQLSessionInfo &session_info,
    ObDMLStmt &stmt,
    ObLogPlan *&logical_plan)
{
  int ret = OB_SUCCESS;
  logical_plan = NULL;
  OPT_TRACE_TITLE("START GENERATE PLAN");
  if (OB_FAIL(optimizer.optimize(stmt, logical_plan))) {
  } else if (OB_FAIL(optimizer.update_column_usage_infos())) {
  } else if (OB_ISNULL(logical_plan)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    OPT_TRACE_TIME_USED;
    OPT_TRACE_MEM_USED;
    OPT_TRACE_TITLE("SYSTEM STATS:");
    OPT_TRACE(logical_plan->get_optimizer_context().get_system_stat());
    OPT_TRACE(logical_plan);
  }

  return ret;
}

int ObSql::code_generate(
    ObSqlCtx &sql_ctx,
    ObResultSet &result,
    ObDMLStmt *stmt,
    ObStmtNeedPrivs &stmt_need_privs,
    ObLogPlan *logical_plan,
    ObPhysicalPlan *&phy_plan)
{
  int ret = OB_SUCCESS;
  int64_t last_mem_usage = 0;
  int64_t codegen_mem_usage = 0;
  ObPhysicalPlanCtx *pctx = result.get_exec_context().get_physical_plan_ctx();
  if (OB_ISNULL(stmt) || OB_ISNULL(logical_plan) || OB_ISNULL(stmt->get_query_ctx())
        || OB_ISNULL(phy_plan) || OB_ISNULL(sql_ctx.session_info_)
        || OB_ISNULL(pctx)) {
      ret = OB_INVALID_ARGUMENT;
  } else {
    ObCodeGenerator code_generator(&(pctx->get_datum_param_store()));
    phy_plan->set_is_packed(logical_plan->get_optimizer_context().is_packed());
    if (OB_FAIL(code_generator.generate(*logical_plan, *phy_plan))) {
    } else {
      // session's ignore_stmt status for CG use, needs to be cleared after CG ends
      sql_ctx.session_info_->set_ignore_stmt(false);
      last_mem_usage = phy_plan->get_mem_size();
    }
  }
  //add local_session_var array to phy_plan_ctx
  if (OB_SUCC(ret)) {
    if (OB_FAIL(phy_plan->set_all_local_session_vars(sql_ctx.all_local_session_vars_))) {
    }
  }
  NG_TRACE(cg_end);

  // set phy table location in task_exec_ctx, query_timeout in exec_context
  if (OB_SUCC(ret)) {
    const ObPlanCachePolicy plan_cache_policy = phy_plan->get_phy_plan_hint().plan_cache_policy_;
    ObPhyPlanHint phy_hint(logical_plan->get_optimizer_context().get_global_hint());
    if (OB_USE_PLAN_CACHE_NONE == plan_cache_policy) {
      phy_hint.plan_cache_policy_ = OB_USE_PLAN_CACHE_NONE;
    }
    // set larger query_time for IS
    if (stmt->get_query_ctx()->has_is_table_) {
      int tmp_ret = OB_SUCCESS;
      if (phy_hint.query_timeout_ <= 0) {
        if (OB_SUCCESS != (tmp_ret = sql_ctx.session_info_->get_query_timeout(
                    phy_hint.query_timeout_))) {
        }
      }
      phy_hint.query_timeout_ *= 10;
    } else {}//do nothing
    ObSEArray<ObTablePartitionInfo *, 3> tbl_part_infos;
    phy_plan->set_phy_plan_hint(phy_hint);//remember in phy_plan
    if (OB_FAIL(ret)) {
      // do nothing
    } else {
      if (OB_FAIL(logical_plan->get_global_table_partition_info(tbl_part_infos))) {
      } else if (OB_FAIL(sql_ctx.set_partition_infos(
              tbl_part_infos,
              result.get_exec_context().get_allocator()))) {
      } else {
        ObDASCtx &das_ctx = DAS_CTX(result.get_exec_context());
        for (int64_t i = 0; OB_SUCC(ret) && i < tbl_part_infos.count(); i++) {
          ObTableLocation &tl = tbl_part_infos.at(i)->get_table_location();
          if (!tl.use_das()) {
            const ObCandiTableLoc &candi_table_loc = tbl_part_infos.at(i)->get_phy_tbl_location_info();
            if (OB_FAIL(das_ctx.add_candi_table_loc(tl.get_loc_meta(), candi_table_loc))) {
            } else if (OB_UNLIKELY(das_ctx.get_table_loc_list().empty())) {
              ret = OB_ERR_UNEXPECTED;
            }
          }
        } // for end
      }
    }
    // get tablet id and partition id for non partition tables
    if (OB_SUCC(ret)) {
      bool skip_non_partition_optimized = false;
      for (int64_t i = 0; OB_SUCC(ret) && i < tbl_part_infos.count(); i++) {
        ObTableLocation &tl = tbl_part_infos.at(i)->get_table_location();
        if (tl.is_partitioned() || is_virtual_table(tl.get_loc_meta().ref_table_id_)) {
          skip_non_partition_optimized = true;
          break;
        }
      }
      if (OB_SUCC(ret) && !skip_non_partition_optimized) {
        for (int64_t i = 0; OB_SUCC(ret) && i < tbl_part_infos.count(); i++) {
          ObTableLocation &tl = tbl_part_infos.at(i)->get_table_location();
          if (OB_FAIL(tl.calc_not_partitioned_table_ids(result.get_exec_context()))) {
          } else {
            tl.set_is_non_partition_optimized(true);
          }
        }
      }
    }
    // set table location for phy_plan
    if (OB_SUCC(ret)) {
      if (OB_FAIL(phy_plan->set_table_locations(tbl_part_infos, *sql_ctx.schema_guard_))) {
      }
    }
  }

  // set multi stmt info
  if (OB_SUCC(ret)) {
    if (OB_FAIL(sql_ctx.set_multi_stmt_rowkey_pos(logical_plan->get_multi_stmt_rowkey_pos(),
                                                  result.get_exec_context().get_allocator()))) {
    } else {
    }
  }

  if (OB_SUCC(ret)) {
    bool use_plan_cache = sql_ctx.session_info_->get_local_ob_enable_plan_cache();
    ObPlanCache *plan_cache = plan_cache_;
    if (OB_UNLIKELY(NULL == plan_cache)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      if (use_plan_cache) {
        if (OB_FAIL(phy_plan->set_stmt_need_privs(stmt_need_privs))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(phy_plan->init_operator_stats())) {
        } else {
          codegen_mem_usage = phy_plan->get_mem_size() - last_mem_usage;
        }
      }
    }
  }

  return ret;
}

inline int ObSql::sanity_check(ObSqlCtx &context)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
  } else if (OB_UNLIKELY(NULL == context.session_info_)
             || OB_UNLIKELY(NULL == context.schema_guard_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    // do nothing
  }
  return ret;
}

int ObSql::init_result_set(ObSqlCtx &context, ObResultSet &result_set)
{
  return init_exec_context(context, result_set.get_exec_context());
}

OB_INLINE int ObSql::init_exec_context(const ObSqlCtx &context, ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObSqlExecutorCtx &task_exec_ctx = exec_ctx.get_sql_exec_ctx();
  task_exec_ctx.set_retry_times(context.retry_times_);
  if (OB_FAIL(exec_ctx.create_physical_plan_ctx())) {
  } else {
    exec_ctx.set_my_session(context.session_info_);
    bind_exec_context_runtime_services(exec_ctx);
    exec_ctx.set_sql_ctx(const_cast<ObSqlCtx*>(&context));
    if (OB_NOT_NULL(exec_ctx.get_physical_plan_ctx()) && OB_NOT_NULL(context.session_info_)) {
      int64_t query_timeout = 0;
      context.session_info_->get_query_timeout(query_timeout);
      exec_ctx.get_physical_plan_ctx()->set_timeout_timestamp(
        context.session_info_->get_query_start_time() + query_timeout);
      exec_ctx.set_retry_info(&context.session_info_->get_retry_info());
    }
  }
  return ret;
}

int ObSql::execute_get_plan(ObPlanCache &plan_cache,
                            ObPlanCacheCtx &pc_ctx,
                            ObCacheObjGuard& guard)
{
  int ret = OB_SUCCESS;
  ObIAllocator &allocator = pc_ctx.allocator_;
  ObSQLSessionInfo *session = pc_ctx.sql_ctx_.session_info_;
  ObPhysicalPlanCtx *pctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
  if (OB_ISNULL(session) || OB_ISNULL(pctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
      // TODO change pl mode hit cache as text mode.
      ObPsStmtId stmt_id = pc_ctx.fp_result_.pc_key_.key_id_;
      if (OB_FAIL(plan_cache.get_ps_plan(guard, stmt_id, pc_ctx))) {
        if (OB_SQL_PC_NOT_EXIST == ret || OB_PC_LOCK_CONFLICT == ret) {
          // do nothing
        } else {
        }
      }
    } else {
      if (OB_FAIL(plan_cache.get_plan(allocator, pc_ctx, guard))) {
        if (OB_SQL_PC_NOT_EXIST == ret || OB_PC_LOCK_CONFLICT == ret) {
          // do nothing
        } else {
        }
      }
    }
  }
  return ret;
}

int ObSql::pc_get_plan_and_fill_result(ObPlanCacheCtx &pc_ctx,
                                       ObResultSet &result,
                                       int &get_plan_err,
                                       bool &need_disconnect)
{
  UNUSED(need_disconnect);
  int ret = OB_SUCCESS;
  ObPhysicalPlan *plan = NULL;
  ObExecContext &exec_ctx = result.get_exec_context();
  ObCacheObjGuard& guard = result.get_cache_obj_guard();

  if (OB_FAIL(pc_get_plan(pc_ctx, guard, get_plan_err,
                          exec_ctx.get_need_disconnect_for_update()))) {
  } else if (OB_SUCCESS != get_plan_err) {
    //get plan from plan cache failed
  } else if ( FALSE_IT(plan = static_cast<ObPhysicalPlan*>(guard.get_cache_obj()))) {
    // do nothing
  } else if (OB_ISNULL(plan)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    result.set_is_from_plan_cache(true);
    if (OB_FAIL(result.from_plan(*plan, pc_ctx.fp_result_.raw_params_))) {
    }
  }
  return ret;
}

int ObSql::pc_get_plan(ObPlanCacheCtx &pc_ctx,
                       ObCacheObjGuard& guard,
                       int &get_plan_err,
                       bool &need_disconnect)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_get_plan_cache);
  int ret = OB_SUCCESS;
  //NG_TRACE(cache_get_plan_begin);
  ObPlanCache *plan_cache = NULL;
  if (OB_ISNULL(plan_cache = plan_cache_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(execute_get_plan(*plan_cache, pc_ctx, guard))) {
    if (OB_EAGAIN == ret
        || OB_ARRAY_BINDING_ROLLBACK == ret
        || OB_BATCHED_MULTI_STMT_ROLLBACK == ret) {
      /*do nothing*/
    } else if (OB_PC_LOCK_CONFLICT == ret) {
      // Lock contention is a plan-cache miss. Continue on the long path instead
      // of exposing an internal cache error to the client.
      get_plan_err = ret;
      ret = OB_SUCCESS;
    } else {
      get_plan_err = ret;
      int tmp_ret = OB_SUCCESS;
      tmp_ret = OB_E(EventTable::EN_PC_NOT_SWALLOW_ERROR) OB_SUCCESS;
      if (OB_SUCCESS != tmp_ret) {
         // do nothing
        if (OB_SQL_PC_NOT_EXIST == ret || OB_REACH_MEMORY_LIMIT == ret) {
          ret = OB_SUCCESS;
        }
      } else {
        ret = OB_SUCCESS; // get plan error, cover error code, ensure that errors from plan cache do not affect the normal execution path
      }
    }
  } else { // get plan successfully
    plan_cache->inc_hit_and_access_cnt();
    ObPhysicalPlan* plan = static_cast<ObPhysicalPlan*>(guard.get_cache_obj());
    if (OB_ISNULL(plan)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get plan cache");
    } else {
      // Hit the plan cache, then it cannot be a commit or rollback statement, default to keep connecting
      need_disconnect = false;
      //FIXME qianfu NG_TRACE_EXT(set_need_disconnect, OB_ID(need_disconnect), false);
      pc_ctx.sql_ctx_.plan_cache_hit_ = true;
      // In extreme performance scenarios (perf_event=true), permission information is no longer validated
      if (OB_SUCC(ret) && config::enable_perf_event()) {
        if (OB_FAIL(ObPrivilegeCheck::check_read_only(pc_ctx.sql_ctx_, plan->get_stmt_type(), false,
                                                      plan->get_stmt_need_privs()))) {
        } else if (OB_FAIL(ObPrivilegeCheck::check_privilege(
                                          pc_ctx.sql_ctx_,
                                          plan->get_stmt_need_privs()))) {
        } else {
          NG_TRACE(check_priv);
        }
        if (OB_SUCC(ret) && OB_FAIL(ObPrivilegeCheck::check_password_expired(pc_ctx.sql_ctx_,
                                                                            stmt::T_NONE))) {
          LOG_WARN("Falied to check password expired", K(ret));
        }
      }

    }
  }
  return ret;
}

int ObSql::get_outline_data(ObSqlCtx &context,
                            ObPlanCacheCtx &pc_ctx,
                            const ObString &signature_sql,
                            const ObString &signature_format_sql,
                            ObOutlineState &outline_state,
                            ParseResult &outline_parse_result)
{
  NG_TRACE(transform_with_outline_begin);
  int ret = OB_SUCCESS;
  memset(&outline_parse_result, 0, sizeof(ParseResult));
  ObString outline_content;
  ObSQLSessionInfo *session = pc_ctx.sql_ctx_.session_info_;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (0 != context.first_plan_hash_) {
    outline_content = context.first_outline_data_;
  } else if (OB_INVALID_ID == context.plan_key_.db_id_
             || context.is_batch_params_execute()) {
    //no outline is available when database name of session is not specified, just keep the stmt
  } else if (pc_ctx.is_begin_commit_stmt()) {
    /* do nothing */
  } else if (OB_FAIL(get_outline_data(pc_ctx, signature_sql, signature_format_sql, outline_state, outline_content))) {
  }

  if (OB_SUCC(ret) && !outline_content.empty()) {
    ObParser parser(pc_ctx.allocator_, session->get_sql_mode(), session->get_charsets4parser());
    ObSqlString sql_helper;
    ObString temp_outline_sql;
    if (OB_FAIL(sql_helper.assign_fmt("select %.*s 1 from dual", outline_content.length(),
                                                                 outline_content.ptr()))) {
    } else if (OB_FAIL(ob_write_string(pc_ctx.allocator_, sql_helper.string(), temp_outline_sql))) {
    } else if (OB_FAIL(parser.parse(temp_outline_sql, outline_parse_result))) {
      outline_state.reset();
      ret = OB_SUCCESS;
    }
  }
  NG_TRACE(transform_with_outline_end);
  return ret;
}

int ObSql::get_outline_data(ObPlanCacheCtx &pc_ctx,
                            const ObString &signature_sql,
                            const ObString &signature_format_sql,
                            ObOutlineState &outline_state,
                            ObString &outline_content)
{
  int ret = OB_SUCCESS;
  const ObOutlineInfo *outline_info = NULL;
  ObSchemaGetterGuard *schema_guard = pc_ctx.sql_ctx_.schema_guard_;
  const uint64_t database_id = pc_ctx.sql_ctx_.plan_key_.db_id_;
  const ObString sql_id = pc_ctx.sql_ctx_.plan_key_.sql_id_;
  const ObString format_sql_id = pc_ctx.sql_ctx_.plan_key_.format_sql_id_;
  ObSQLSessionInfo *session = pc_ctx.sql_ctx_.session_info_;
  outline_state.reset();
  int64_t schema_version = OB_INVALID_VERSION;
  if (OB_ISNULL(session) || OB_ISNULL(schema_guard)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(schema_guard->get_schema_version(schema_version))) {
  } else if (OB_CORE_SCHEMA_VERSION >= schema_version) {
    // local schema is fall behind, do not use outline
  } else {
    char *buf = NULL;
    int64_t pos = 0;
    int64_t format_pos = 0;
    int64_t size = signature_sql.get_serialize_size() + signature_format_sql.get_serialize_size();
    ObString outline_key;
    ObString format_outline_key;
    ObIAllocator &allocator = CURRENT_CONTEXT->get_arena_allocator();
    if (0 == size) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(buf = (char *)allocator.alloc(size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("fail to alloc mem", K(ret));
    } else if (OB_FAIL(signature_sql.serialize(buf, size, pos))) {
    } else if (OB_FALSE_IT(outline_key.assign_ptr(buf, static_cast<ObString::obstr_size_t>(pos)))) {
    } else if (!signature_format_sql.empty()
        && OB_FAIL(signature_format_sql.serialize(buf + pos, size, format_pos))) {
    } else if (!signature_format_sql.empty()
        && FALSE_IT(format_outline_key.assign_ptr(buf + pos,
            static_cast<ObString::obstr_size_t>(format_pos)))) {
      // try normal outline
    } else if (OB_FAIL(schema_guard->get_outline_info_with_signature(database_id,
                                                                     outline_key,
                                                                     false, /*normal outline*/
                                                                     outline_info))) {
      ret = OB_SUCCESS;
    // try normal outline
    } else if (NULL == outline_info &&
               OB_FAIL(schema_guard->get_outline_info_with_sql_id(database_id,
                                                                sql_id,
                                                                false, /*normal outline*/
                                                                outline_info))) {
      ret = OB_SUCCESS;
    // try format outline
    } else if (NULL == outline_info && !format_outline_key.empty() &&
              OB_FAIL(schema_guard->get_outline_info_with_signature(database_id,
                                                                     format_outline_key,
                                                                     true, /*format outline*/
                                                                     outline_info))) {
      ret = OB_SUCCESS;

    // try format outline
    } else if (NULL == outline_info && !format_sql_id.empty() &&
               OB_FAIL(schema_guard->get_outline_info_with_sql_id(database_id,
                                                                format_sql_id,
                                                                true, /*format outline*/
                                                                outline_info))) {
      ret = OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret) && NULL != outline_info) {
    ObString outline_content_copy = outline_info->get_outline_content_str();
    if (OB_FAIL(ObSQLUtils::convert_sql_text_from_schema_for_resolve(pc_ctx.allocator_,
                                                                     session->get_dtc_params(),
                                                                     outline_content_copy))) {
      //outline_content is stored using UTF8, if client sql is GBK, convert may failed
      outline_content = ObString::make_empty_string();
      ret = OB_SUCCESS;
    } else {
      outline_content = outline_content_copy;
      outline_state.is_plan_fixed_ = !outline_info->get_outline_content_str().empty();
      outline_state.outline_version_.object_id_ = outline_info->get_outline_id();
      outline_state.outline_version_.version_ = outline_info->get_schema_version();
      outline_state.outline_version_.object_type_ = DEPENDENCY_OUTLINE;
    }
  }
  return ret;
}

int ObSql::parser_and_check(const ObString &outlined_stmt,
                            ObExecContext &exec_ctx,
                            ObPlanCacheCtx &pc_ctx,
                            ParseResult &parse_result,
                            int get_plan_err,
                            bool &add_plan_to_pc,
                            bool &is_enable_transform_tree)
{
  int ret = OB_SUCCESS;
  ObIAllocator &allocator = pc_ctx.allocator_;
  ObSQLSessionInfo *session = exec_ctx.get_my_session();
  ObPhysicalPlanCtx *pctx = exec_ctx.get_physical_plan_ctx();
  bool is_stack_overflow = false;
  bool is_show_variables = false;
  bool is_explain_parameterize = false;
  stmt::StmtType stmt_type = stmt::T_NONE;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
  } else if (OB_ISNULL(pctx)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    pctx->reset_datum_param_store();
    pctx->get_param_store_for_update().reuse();
    ObParser parser(allocator, session->get_sql_mode(), session->get_charsets4parser());
    if (OB_FAIL(parser.parse(outlined_stmt, parse_result,
                             STD_MODE,
                             pc_ctx.sql_ctx_.handle_batched_multi_stmt(),
                             false, NULL != session->get_pl_context()))) {
    } else if ((PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_)
      && OB_FAIL(construct_param_store_from_parameterized_params(
                    pc_ctx, pctx->get_param_store_for_update()))) {
    }
    pc_ctx.sql_ctx_.is_sensitive_ |= parse_result.contain_sensitive_data_;
    if (OB_SUCC(ret)) {
      // parser returns success
      if (OB_ISNULL(parse_result.result_tree_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("parse result tree is NULL", K(ret));
      } else if (OB_UNLIKELY(parse_result.result_tree_->num_child_ < 1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("child count of parse result tree is less than 1",
                  K(ret), K(parse_result.result_tree_->num_child_));
      } else if (OB_ISNULL(parse_result.result_tree_->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("children_[0] ofparse result tree is NULL",
                  K(ret), K(parse_result.result_tree_->num_child_));
      } else {
        ObItemType parse_stmt_type = parse_result.result_tree_->children_[0]->type_;
        if (T_COMMIT == parse_stmt_type || T_ROLLBACK == parse_stmt_type) {
          // Is commit or rollback statement, default disconnect
        } else {
          // Not a commit or rollback statement, default to keep connecting
          exec_ctx.set_need_disconnect(false);
          //FIXME qianfu NG_TRACE_EXT(set_need_disconnect, OB_ID(need_disconnect), false);
        }
      }
    } else if (!ObSQLUtils::check_need_disconnect_parser_err(ret)) {
      exec_ctx.set_need_disconnect(false);
    } else {
      // parser returns an unknown error code, need to disconnect from the client
    }
    if (OB_SUCC(ret)) {
      stmt::StmtType stmt_type = stmt::T_NONE;
      if (OB_ISNULL(parse_result.result_tree_)) {
        ret = OB_INVALID_ARGUMENT;
      } else if (OB_ISNULL(parse_result.result_tree_->children_)
                 || OB_UNLIKELY(parse_result.result_tree_->num_child_ < 1)) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        ParseNode *children_node = parse_result.result_tree_->children_[0];
        if (OB_ISNULL(children_node)) {
          ret = OB_INVALID_ARGUMENT;
        // In addition to ordinary dml stmt, ? in explain stmt also needs to be judged here
        } else if (!(PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_)
                   && (children_node->type_ == T_EXPLAIN || IS_DML_STMT(children_node->type_))
                   && (children_node->value_ > 0)) {
          ret = OB_ERR_PARSE_SQL;//children_node->value_ > 0, indicates that it has a question_mark
          const char *err_msg = "?";
          int32_t str_len = static_cast<int32_t>(strlen(err_msg));
          int32_t line_no = 1;
        } else if (OB_FAIL(ObResolverUtils::resolve_stmt_type(parse_result, stmt_type))) {
        } else {
          ObItemType type = children_node->type_;
          // If it is not a DML statement, then do not enter plan cache
          ObPlanCache *plan_cache = NULL;
          if (T_SHOW_VARIABLES == type) {
            is_show_variables = true;
          }
          if (ObStmt::is_ddl_stmt(stmt_type, true)) {
            THIS_WORKER.set_timeout_ts(session->get_query_start_time() + config::_ob_ddl_timeout());
          }
          if (IS_DML_STMT(type) || is_show_variables) {
            if (OB_UNLIKELY(NULL == (plan_cache = plan_cache_))) {
              ret = OB_ERR_UNEXPECTED;
            } else {
              plan_cache->inc_access_cnt();
              if (OB_SQL_PC_NOT_EXIST == get_plan_err) {
                add_plan_to_pc = true;
              } else {
                add_plan_to_pc = false;
              }
            }
          }
          if (OB_SUCC(ret) && stmt::T_EXPLAIN == stmt_type) {
            if (OB_SQL_PC_NOT_EXIST == get_plan_err) {
              is_explain_parameterize = true;
            } else {
              is_explain_parameterize = false;
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    // Database runtime read-only check.
    if ((session->is_inner() && !session->is_user_session()) || pc_ctx.is_begin_commit_stmt()) {
      // Internal SQL and transaction control are not governed by database read-only state.
    } else if (OB_ISNULL(pc_ctx.sql_ctx_.schema_guard_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(check_read_only_privilege(parse_result, exec_ctx,
        *pc_ctx.sql_ctx_.schema_guard_, pc_ctx.sql_traits_))) {
    }
  }

  if (OB_FAIL(ret)) {
     // do nothing
  } else if (!is_enable_transform_tree) {
    //pc_ctx.fp_result_.pc_key_.name_
    if (OB_FAIL(ob_write_string(allocator,
                                pc_ctx.raw_sql_,
                                pc_ctx.fp_result_.pc_key_.name_))) {
    } else if (OB_FAIL(ob_write_string(allocator,
                                       pc_ctx.raw_sql_,
                                       pc_ctx.sql_ctx_.plan_key_.constructed_sql_))) {
    }
  } else {
    // Outline statements also need syntax-tree transformation so parameter markers in
    // their embedded SQL are validated consistently.
    bool flag = false;
    if ((add_plan_to_pc && !is_show_variables) ||
        is_explain_parameterize ||
        ((T_CREATE_OUTLINE == parse_result.result_tree_->children_[0]->type_ ||
          T_ALTER_OUTLINE == parse_result.result_tree_->children_[0]->type_) &&
         (INT64_MAX != parse_result.result_tree_->children_[0]->value_))) {
      flag = true;
      if (T_CREATE_OUTLINE == parse_result.result_tree_->children_[0]->type_) {
        if (1 != parse_result.result_tree_->children_[0]->children_[2]->value_) {
          flag = false;
        }
      }
    } else if (pc_ctx.sql_ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
      if (T_EXPLAIN == parse_result.result_tree_->children_[0]->type_) {
        flag = true;
      }
    }
    if (flag) {
      bool is_transform_outline = (T_CREATE_OUTLINE == parse_result.result_tree_->children_[0]->type_
                                   || T_ALTER_OUTLINE == parse_result.result_tree_->children_[0]->type_);
      if (is_transform_outline) {
        LOG_WARN("is_transform_outline", K(is_transform_outline));
      }
      if (OB_FAIL(ObSqlParameterization::parameterize_syntax_tree(allocator,
                                                                  is_transform_outline,
                                                                  pc_ctx,
                                                                  parse_result.result_tree_,
                                                                  pctx->get_param_store_for_update(),
                                                                  session->get_charsets4parser()))) {
        bool need_retry_param = true;
        int tmp_ret = OB_SUCCESS;
        tmp_ret = OB_E(EventTable::EN_SQL_PARAM_FP_NP_NOT_SAME_ERROR) OB_SUCCESS;
        if (OB_SUCCESS != tmp_ret) {
          if (OB_NOT_SUPPORTED == ret) {
            need_retry_param = false;
          }
        }
        if (!need_retry_param) {
          // do nothing
        } else if (is_transform_outline) {
        } else {
          // If it is due to parameterization error, then a new parser needs to be performed, generating a new parser tree, the previous parser tree may have been partially parameterized,
          // and mark this query not to go into plan cache, and no need for parameterization next time, thus ensuring that errors during parameterization do not affect normal execution.
          pctx->reset_datum_param_store();
          is_enable_transform_tree = false;
          if (OB_FAIL(SMART_CALL(parser_and_check(outlined_stmt,
                                                  exec_ctx,
                                                  pc_ctx,
                                                  parse_result,
                                                  get_plan_err,
                                                  add_plan_to_pc,
                                                  is_enable_transform_tree)))) {
          }
          add_plan_to_pc = false;
        }
      } else {
        parse_result.question_mark_ctx_.count_ = static_cast<int> (pctx->get_param_store().count());
      }
    }
  }

  return ret;
}

int ObSql::pc_add_plan(ObPlanCacheCtx &pc_ctx,
                       ObResultSet &result,
                       ObOutlineState &outline_state,
                       ObPlanCache *plan_cache,
                       bool& plan_added)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlan *phy_plan = result.get_physical_plan();
  pc_ctx.fp_result_.pc_key_.namespace_ = ObLibCacheNameSpace::NS_CRSR;
  plan_added = false;
  bool is_batch_exec = pc_ctx.sql_ctx_.is_batch_params_execute();
  if (OB_ISNULL(phy_plan) || OB_ISNULL(plan_cache)) {
    ret = OB_NOT_INIT;
    LOG_WARN("Fail to generate plan", K(phy_plan), K(plan_cache));
  } else if (OB_USE_PLAN_CACHE_NONE == phy_plan->get_phy_plan_hint().plan_cache_policy_) {
    if (is_batch_exec) {
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      LOG_WARN("with not use plan_cache hint, batched multi_stmt needs rollback", K(ret));
    }
  } else if (OB_FAIL(result.to_plan(pc_ctx.mode_, phy_plan))) {
  } else if (OB_FAIL(ob_write_string(phy_plan->get_allocator(),
                                     (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_)
                                       ? pc_ctx.raw_sql_ :
                                       pc_ctx.sql_ctx_.plan_key_.constructed_sql_,
                                     phy_plan->stat_.constructed_sql_))) {
  } else if (OB_FAIL(ob_write_string(phy_plan->get_allocator(),
                                     pc_ctx.sql_ctx_.plan_key_.sql_id_,
                                     phy_plan->stat_.sql_id_))) {
  } else if (OB_FAIL(ob_write_string(phy_plan->get_allocator(),
                                     pc_ctx.sql_ctx_.plan_key_.format_sql_id_,
                                     phy_plan->stat_.format_sql_id_))) {
  } else {
    phy_plan->set_outline_state(outline_state);
    phy_plan->stat_.db_id_ = pc_ctx.sql_ctx_.plan_key_.db_id_;
    phy_plan->stat_.is_inner_ = result.get_session().is_inner();

    if (PC_PS_MODE == pc_ctx.mode_ || PC_PL_MODE == pc_ctx.mode_) {
      // pc_key_ may be modified elsewhere, so reset it before adding plan
      pc_ctx.fp_result_.pc_key_.key_id_ = pc_ctx.sql_ctx_.statement_id_;
      ret = plan_cache->add_ps_plan(phy_plan, pc_ctx);
    } else {
      ret = plan_cache->add_plan(phy_plan, pc_ctx);
    }
    plan_added = (OB_SUCCESS == ret);
    int tmp_ret = OB_SUCCESS;
    tmp_ret = OB_E(EventTable::EN_PC_NOT_SWALLOW_ERROR) OB_SUCCESS;
    if (is_batch_exec) {
      // Batch optimization cannot continue for errors other than OB_SQL_PC_PLAN_DUPLICATE.
      if (OB_SQL_PC_PLAN_DUPLICATE == ret) {
        ret = OB_SUCCESS;
      } else if (OB_FAIL(ret)) {
        ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
      } else {
        pc_ctx.sql_ctx_.self_add_plan_ = true;
      }
    } else if (OB_SQL_PC_PLAN_DUPLICATE == ret) {
      ret = OB_SUCCESS;
    } else if (OB_REACH_MEMORY_LIMIT == ret || OB_SQL_PC_PLAN_SIZE_LIMIT == ret) {
      if (REACH_TIME_INTERVAL(1000000)) { //1s, when memory reaches its limit, this log print will be relatively frequent, so it prints at an interval of 1s
        ObTruncatedString trunc_sql(pc_ctx.raw_sql_);
        LOG_INFO("can't add plan to plan cache",
                 K(ret), K(phy_plan->get_mem_size()), K(trunc_sql),
                 K(plan_cache->get_mem_used()));
      }
      ret = OB_SUCCESS;
    } else if (is_not_supported_err(ret)) {
      ret = OB_SUCCESS;
    } else if (OB_FAIL(ret)) {
      if (OB_SUCCESS != tmp_ret) {

      } else {
        ret = OB_SUCCESS; // add plan error, overwrite error code, ensure that failure of plan cache does not affect normal execution path
      }
    } else {
      pc_ctx.sql_ctx_.self_add_plan_ = true;
    }
  }

  return ret;
}
int ObSql::after_get_plan(ObPlanCacheCtx &pc_ctx,
                          ObSQLSessionInfo &session,
                          ObPhysicalPlan *phy_plan,
                          bool from_plan_cache)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *pctx = pc_ctx.exec_ctx_.get_physical_plan_ctx();
  if (NULL != pctx) {
    if (NULL != phy_plan) {
      // record the plan id in trace_event, perf_event and atomic_event
      NG_TRACE_EXT(plan_id, OB_ID(plan_id), phy_plan->get_plan_id());
      if (OB_MAX_SQL_ID_LENGTH != phy_plan->stat_.sql_id_.length()) {
        if (OB_FAIL(ob_write_string(phy_plan->get_allocator(),
                                    pc_ctx.sql_ctx_.sql_id_,
                                    phy_plan->stat_.sql_id_))) {
        }
      }
      // init auto increment param
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(pc_ctx.exec_ctx_.init_physical_plan_ctx(*phy_plan))) {
      } else if (OB_FAIL(DAS_CTX(pc_ctx.exec_ctx_).init(*phy_plan, pc_ctx.exec_ctx_))) {
      } else if (OB_FAIL(pctx->set_autoinc_params(phy_plan->get_autoinc_params()))) {
      } else {
        pctx->set_tablet_autoinc_param(phy_plan->get_tablet_autoinc_param());
        ObIArray<AutoincParam> &autoinc_params = pctx->get_autoinc_params();
        for (int64_t i = 0; OB_SUCC(ret) && i < autoinc_params.count(); ++i) {
          AutoincParam &param = autoinc_params.at(i);
          // Since 4.0, there should be only one autoinc_param
          param.autoinc_increment_ = session.get_local_auto_increment_increment();
          param.autoinc_offset_ = session.get_local_auto_increment_offset();
          if (pc_ctx.sql_ctx_.is_do_insert_batch_opt()) {
            param.total_value_count_ = pc_ctx.sql_ctx_.get_insert_batch_row_cnt();
          } else if (phy_plan->is_plain_insert() && pctx->get_array_param_groups().count() == 1) {
            param.total_value_count_ = pctx->get_array_param_groups().at(0).row_count_;
          }
        }  // end for
      }

    }

    if (OB_SUCC(ret)) {
      // set last_insert_id
      pctx->set_last_insert_id_session(session.get_local_last_insert_id());
    }

    if (OB_SUCC(ret) && NULL != phy_plan) {
      if (from_plan_cache) {
        ObPhyPlanHint &phy_plan_hint = phy_plan->get_phy_plan_hint();
        pc_ctx.sql_ctx_.force_print_trace_ = phy_plan_hint.force_trace_log_;
        //log_level just deal mpquery now.
        if (MpQuery == pc_ctx.sql_ctx_.exec_type_ && phy_plan_hint.log_level_.length() > 0) {
          if (OB_FAIL(process_thread_log_id_level_map(phy_plan_hint.log_level_.ptr(),
                                                      phy_plan_hint.log_level_.length()))) {
          }
        }
      }
    }
    if (OB_SUCC(ret) && NULL != phy_plan) {
      if (OB_FAIL(pctx->set_all_local_session_vars(phy_plan->get_all_local_session_vars()))) {
      }
    }
  } else {
    // not phy_plan, ignore
  }

  return ret;
}

int ObSql::need_add_plan(const ObPlanCacheCtx &pc_ctx,
                         ObResultSet &result,
                         bool is_enable_pc,
                         bool &need_add_plan)
{
  int ret = OB_SUCCESS;
  result.get_exec_context().get_stmt_factory()->get_query_ctx();
  if (false == need_add_plan) {
    // do nothing
  } else if (!is_enable_pc || !pc_ctx.should_add_plan_) {
    need_add_plan = false;
  } else if (OB_ISNULL(result.get_exec_context().get_stmt_factory())
             || OB_ISNULL(result.get_exec_context().get_stmt_factory()->get_query_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (result.get_exec_context().get_stmt_factory()->get_query_ctx()->has_hybrid_search()) {
    need_add_plan = false;
  }
  return ret;
}

OB_NOINLINE int ObSql::handle_physical_plan(const ObString &trimed_stmt,
                                            ObSqlCtx &context,
                                            ObResultSet &result,
                                            ObPlanCacheCtx &pc_ctx,
                                            const int get_plan_err)
{
  int ret = OB_SUCCESS;
  bool is_valid = true;
  PlanCacheMode mode = pc_ctx.mode_;
  ObString outlined_stmt = trimed_stmt;//use outline if available
  ObString signature_sql;
  ObString signature_format_sql;
  ObOutlineState outline_state;
  ParseResult parse_result;
  MEMSET(&parse_result, 0, SIZEOF(ParseResult));
  ParseResult outline_parse_result;
  MEMSET(&outline_parse_result, 0, SIZEOF(ParseResult));
  bool add_plan_to_pc = false;
  ObSQLSessionInfo &session = result.get_session();
  ObPlanCache *plan_cache = plan_cache_;
  bool use_plan_cache = session.get_local_ob_enable_plan_cache();
  // record whether needs to do parameterization at this time,
  // if exact mode is on, not do parameterizaiton
  bool is_enable_transform_tree = !session.get_enable_exact_mode();
  // Reset these two flags before reparsing to avoid issues with non-idempotency of these parameters after regenerating the plan due to previous plan cache lookup operations
  pc_ctx.not_param_index_.reset();
  pc_ctx.neg_param_index_.reset();
  bool plan_added = false;
  // for batched multi stmt, we only parse and optimize the first statement
  // only in multi_query, need do this
  if (!(PC_PS_MODE == mode || PC_PL_MODE == mode) &&
      (context.is_batch_params_execute() || pc_ctx.exec_ctx_.has_dynamic_values_table()) &&
      OB_FAIL(get_reconstructed_batch_stmt(pc_ctx, outlined_stmt))) {
  } else if (OB_FAIL(handle_parser(outlined_stmt,
                                   result.get_exec_context(),
                                   pc_ctx,
                                   parse_result,
                                   get_plan_err,
                                   add_plan_to_pc,
                                   is_enable_transform_tree))) {
  } else if (context.is_batch_params_execute() &&
             !(PC_PS_MODE == mode || PC_PL_MODE == mode) &&
             OB_FAIL(check_batched_multi_stmt_after_parser(pc_ctx,
                                                           parse_result,
                                                           add_plan_to_pc,
                                                           is_valid))) {
  } else if (!is_valid) {
    ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
    LOG_WARN("batched multi_stmt needs rollback", K(ret));
  }
  generate_sql_id(pc_ctx, add_plan_to_pc, parse_result, signature_sql, signature_format_sql, ret);

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_FAIL(get_outline_data(context, pc_ctx, signature_sql, signature_format_sql,
                                      outline_state, outline_parse_result))) {
  } else if (OB_FAIL(generate_physical_plan(parse_result,
                                            &pc_ctx,
                                            context,
                                            result,
                                            pc_ctx.is_begin_commit_stmt(),
                                            mode,
                                            &outline_parse_result))) {
  } else if (OB_FAIL(need_add_plan(pc_ctx,
                                   result,
                                   use_plan_cache,
                                   add_plan_to_pc))) {
  } else if (!add_plan_to_pc) {
    // do nothing
  } else if (OB_FAIL(pc_add_plan(pc_ctx, result, outline_state, plan_cache, plan_added))) {
  }
  //if the error code is ob_timeout, we add more error info msg for dml query.
  if (OB_UNLIKELY(OB_TIMEOUT == ret && session.is_user_session())) {
    LOG_USER_ERROR(OB_TIMEOUT, THIS_WORKER.get_timeout_ts() - result.get_session().get_query_start_time());
  }
  return ret;
}

int ObSql::handle_parser(const ObString &sql,
                         ObExecContext &exec_ctx,
                         ObPlanCacheCtx &pc_ctx,
                         ParseResult &parse_result,
                         int get_plan_err,
                         bool &add_plan_to_pc,
                         bool &is_enable_transform_tree)

{
  int ret = OB_SUCCESS;
  int64_t last_mem_usage = pc_ctx.allocator_.total();
  int64_t parser_mem_usage = 0;
  ObPhysicalPlanCtx *pctx = exec_ctx.get_physical_plan_ctx();
  const ObSqlCtx *sql_ctx = exec_ctx.get_sql_ctx();
  if (OB_ISNULL(pc_ctx.sql_ctx_.session_info_) || OB_ISNULL(pctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(SMART_CALL(parser_and_check(sql, exec_ctx, pc_ctx, parse_result,
                                      get_plan_err, add_plan_to_pc, is_enable_transform_tree)))) {
  }
  if (OB_SUCC(ret)) {
    if (exec_ctx.has_dynamic_values_table()) {
      if (OB_FAIL(ObValuesTableCompression::resolve_params_for_values_clause(pc_ctx))) {
      }
    }
    if (OB_SUCC(ret)) {
      pctx->set_original_param_cnt(pctx->get_param_store().count());
    }
  }
  return ret;
}

int ObSql::check_batched_multi_stmt_after_parser(ObPlanCacheCtx &pc_ctx,
                                                 ParseResult &parse_result,
                                                 bool add_plan_to_pc,
                                                 bool &is_valid)
{
  int ret = OB_SUCCESS;
  is_valid = false;
  ObItemType type = parse_result.result_tree_->children_[0]->type_;
  if (add_plan_to_pc ||
      (ObSQLUtils::is_enable_explain_batched_multi_statement() && T_EXPLAIN == type)) {
    is_valid = true;
    // only update support batched multi-stmt optimization
    if (OB_ISNULL(parse_result.result_tree_) ||
        OB_ISNULL(parse_result.result_tree_->children_)) {
      ret = OB_ERR_UNEXPECTED;;
    } else if (OB_UNLIKELY(parse_result.result_tree_->num_child_ < 1)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(parse_result.result_tree_->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!(ObSQLUtils::is_support_batch_exec(type) || T_EXPLAIN == type)) {
      is_valid = false;
    } else { /*do nothing*/ }

    if (OB_SUCC(ret) && is_valid && !pc_ctx.not_param_info_.empty()) {
      if (pc_ctx.sql_ctx_.is_do_insert_batch_opt()) {
        if (OB_FAIL(ObPlanCacheValue::check_insert_multi_values_param(pc_ctx, is_valid))) {
        }
      } else if (pc_ctx.sql_ctx_.multi_stmt_item_.is_batched_multi_stmt()) {
        if (OB_FAIL(ObPlanCacheValue::check_multi_stmt_not_param_value(pc_ctx.multi_stmt_fp_results_,
                                                                       pc_ctx.not_param_info_,
                                                                       is_valid))) {
        }
      }
    }
  }
  return ret;
}

int ObSql::before_resolve_array_params(ObPlanCacheCtx &pc_ctx,
                                       int64_t query_num,
                                       int64_t param_num,
                                       ParamStore *&ab_params,
                                       ObBitSet<> &neg_param_index,
                                       ObBitSet<> &not_param_index,
                                       ObBitSet<> &must_be_positive_index,
                                       ObBitSet<> &fmt_int_or_ch_decint_idx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ab_params = static_cast<ParamStore *>(pc_ctx.allocator_.alloc(sizeof(ParamStore))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(ab_params = new(ab_params)ParamStore(ObWrapperAllocator(pc_ctx.allocator_)))) {
   // do nothing
  } else if (OB_FAIL(ObSQLUtils::create_multi_stmt_param_store(pc_ctx.allocator_,
                                                               query_num,
                                                               param_num,
                                                               *ab_params))) {
  } else if (OB_FAIL(neg_param_index.add_members2(pc_ctx.neg_param_index_))) {
  } else if (OB_FAIL(not_param_index.add_members2(pc_ctx.not_param_index_))) {
  } else if (OB_FAIL(must_be_positive_index.add_members2(pc_ctx.must_be_positive_index_))) {
  } else if (OB_FAIL(fmt_int_or_ch_decint_idx.add_members2(pc_ctx.fmt_int_or_ch_decint_idx_))) {
  }
  return ret;
}

int ObSql::resolve_ins_multi_row_params(ObPlanCacheCtx &pc_ctx, const ObStmt &stmt, ParamStore *&ab_params)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  ObBitSet<> neg_param_index;
  ObBitSet<> not_param_index;
  ObBitSet<> must_be_positive_index;
  ObBitSet<> formalize_prec_idx;
  int64_t query_num = pc_ctx.sql_ctx_.get_insert_batch_row_cnt();
  int64_t param_num = 0;
  if (OB_ISNULL(plan_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(param_num = plan_ctx->get_param_store().count())) {
    // do nothing
  } else if (OB_FAIL(before_resolve_array_params(pc_ctx,
                                                 query_num,
                                                 param_num,
                                                 ab_params,
                                                 neg_param_index,
                                                 not_param_index,
                                                 must_be_positive_index,
                                                 formalize_prec_idx))) {
  } else if (OB_FAIL(ObPlanCacheValue::resolve_insert_multi_values_param(pc_ctx,
                                                                         stmt.get_stmt_type(),
                                                                         pc_ctx.param_charset_type_,
                                                                         neg_param_index,
                                                                         not_param_index,
                                                                         must_be_positive_index,
                                                                         formalize_prec_idx,
                                                                         param_num,
                                                                         *ab_params))) {
  }
  return ret;
}

int ObSql::resolve_multi_query_params(ObPlanCacheCtx &pc_ctx, const ObStmt &stmt, ParamStore *&ab_params)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  ObBitSet<> neg_param_index;
  ObBitSet<> not_param_index;
  ObBitSet<> must_be_positive_index;
  ObBitSet<> formalize_prec_index;
  int64_t query_num = pc_ctx.sql_ctx_.get_batch_params_count();
  int64_t param_num = 0;
  if (OB_ISNULL(plan_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(param_num = plan_ctx->get_param_store().count())) {
    // do nothing
  } else if (OB_FAIL(before_resolve_array_params(pc_ctx,
                                                 query_num,
                                                 param_num,
                                                 ab_params,
                                                 neg_param_index,
                                                 not_param_index,
                                                 must_be_positive_index,
                                                 formalize_prec_index))) {
  } else if (OB_FAIL(ObPlanCacheValue::check_multi_stmt_param_type(pc_ctx,
                                                                   stmt.get_stmt_type(),
                                                                   pc_ctx.param_charset_type_,
                                                                   neg_param_index,
                                                                   not_param_index,
                                                                   must_be_positive_index,
                                                                   formalize_prec_index,
                                                                   *ab_params))) {
  }

  return ret;
}

int ObSql::check_batched_multi_stmt_after_resolver(ObPlanCacheCtx &pc_ctx,
                                                   const ObStmt &stmt,
                                                   bool &is_valid)
{
  int ret = OB_SUCCESS;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  is_valid = true;
  if (OB_ISNULL(plan_ctx = pc_ctx.exec_ctx_.get_physical_plan_ctx())
      || OB_ISNULL(pc_ctx.sql_ctx_.session_info_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!(stmt.is_support_batch_exec_stmt() || stmt.is_explain_stmt())) {
    is_valid = false;
  } else {
    const ObDelUpdStmt &delupd_stmt = stmt.is_explain_stmt()
        ? static_cast<const ObDelUpdStmt&>(*(static_cast<const ObExplainStmt&>(stmt).get_explain_query_stmt()))
        : static_cast<const ObDelUpdStmt&>(stmt);
    if (delupd_stmt.is_update_stmt() || delupd_stmt.is_delete_stmt()) {
      if (1 != delupd_stmt.get_table_items().count() ||
          !delupd_stmt.get_table_items().at(0)->is_basic_table()) {
        is_valid = false;
      }
    }
    if (delupd_stmt.has_order_by() || delupd_stmt.has_limit()) {
      is_valid = false;
    }

    // make sure type of all the parameters are the same
    if (OB_SUCC(ret) && is_valid) {
      ParamStore *ab_params = NULL;
      ObBitSet<> neg_param_index;
      ObBitSet<> not_param_index;
      ObBitSet<> must_be_positive_index;
      if (pc_ctx.sql_ctx_.is_do_insert_batch_opt()) {
        if (OB_FAIL(resolve_ins_multi_row_params(pc_ctx, stmt, ab_params))) {
        } else {
          pc_ctx.ab_params_ = ab_params;
          ParamStore &param_store = plan_ctx->get_param_store_for_update();
          for (int64_t i = 0; OB_SUCC(ret) && i < param_store.count(); i++) {
            ObObjParam &obj_param = param_store.at(i);
            obj_param.get_param_flag().is_batch_parameter_ = true;
          }
        }
      } else if (!pc_ctx.sql_ctx_.multi_stmt_item_.is_ab_batch_opt()) {
        if (OB_FAIL(resolve_multi_query_params(pc_ctx, stmt, ab_params))) {
        } else {
          pc_ctx.ab_params_ = ab_params;
          ParamStore &param_store = plan_ctx->get_param_store_for_update();
          for (int64_t i = 0; OB_SUCC(ret) && i < param_store.count(); i++) {
            ObObjParam &obj_param = param_store.at(i);
            obj_param.get_param_flag().is_batch_parameter_ = true;
          }
        }
      }
    }
  }
  return ret;
}

void ObSql::generate_ps_sql_id(const ObString &raw_sql,
                               ObSqlCtx &context)
{
  (void)ObSQLUtils::md5(raw_sql, context.sql_id_, (int32_t)sizeof(context.sql_id_));
}

void ObSql::generate_sql_id(ObPlanCacheCtx &pc_ctx,
                           bool add_plan_to_pc,
                           ParseResult &parse_result,
                           ObString &signature_sql,
                           ObString &signature_format_sql,
                           int err_code)
{
  // It has been checked during parser_and_check, there is no need to check again here
  if (OB_SUCCESS == err_code
      && PC_TEXT_MODE == pc_ctx.mode_
      && T_SP_CALL_STMT == parse_result.result_tree_->children_[0]->type_) {
    signature_sql = pc_ctx.fp_result_.pc_key_.name_;
  } else if (add_plan_to_pc == false
            || PC_PS_MODE == pc_ctx.mode_
            || PC_PL_MODE == pc_ctx.mode_
            || OB_SUCCESS != err_code) {
    signature_sql = pc_ctx.raw_sql_;
   // if err happens in parameterization, not generate format_sql;
    signature_format_sql.reset();
  } else {
    signature_sql = pc_ctx.sql_ctx_.plan_key_.constructed_sql_;
    signature_format_sql = pc_ctx.sql_ctx_.plan_key_.format_sql_;
  }
  (void)ObSQLUtils::md5(signature_sql,
                        pc_ctx.sql_ctx_.sql_id_,
                        (int32_t)sizeof(pc_ctx.sql_ctx_.sql_id_));
  pc_ctx.sql_ctx_.plan_key_.sql_id_.
    assign_ptr(pc_ctx.sql_ctx_.sql_id_, static_cast<ObString::obstr_size_t>(strlen(pc_ctx.sql_ctx_.sql_id_)));

  if (!signature_format_sql.empty()) {
    (void)ObSQLUtils::md5(signature_format_sql,
                          pc_ctx.sql_ctx_.format_sql_id_,
                          (int32_t)sizeof(pc_ctx.sql_ctx_.format_sql_id_));
    pc_ctx.sql_ctx_.plan_key_.format_sql_id_.
      assign_ptr(pc_ctx.sql_ctx_.format_sql_id_, static_cast<ObString::obstr_size_t>(strlen(pc_ctx.sql_ctx_.format_sql_id_)));
  }
}


int ObSql::calc_pre_calculable_exprs(const ObDMLStmt &stmt,
                                 const ObIArray<ObHiddenColumnItem> &calculable_exprs,
                                 const bool is_ignore_stmt,
                                 ObExecContext &exec_ctx,
                                 ObPhysicalPlan &phy_plan,
                                 const uint64_t calc_types) /* default PRE_CALC_DEFAULT */
{
  int ret = OB_SUCCESS;
  OB_ASSERT(NULL != exec_ctx.get_physical_plan_ctx() &&
            NULL != exec_ctx.get_my_session() &&
            NULL != exec_ctx.get_stmt_factory() &&
            NULL != exec_ctx.get_stmt_factory()->get_query_ctx());
  ObPhysicalPlanCtx *phy_plan_ctx = exec_ctx.get_physical_plan_ctx();
  ObPreCalcExprFrameInfo *pre_calc_frame = NULL;
  void *frame_buf = NULL;
  bool need_fetch_cur_time = false;
  if (OB_ISNULL(phy_plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(frame_buf = phy_plan.get_allocator().alloc(
                                                            sizeof(ObPreCalcExprFrameInfo)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    phy_plan_ctx->set_ignore_stmt(is_ignore_stmt);
    DatumParamStore &datum_param_store = phy_plan_ctx->get_datum_param_store();
    ObStaticEngineExprCG expr_cg(phy_plan.get_allocator(),
                                 exec_ctx.get_my_session(),
                                 exec_ctx.get_sql_ctx()->schema_guard_,
                                 phy_plan_ctx->get_original_param_cnt(),
                                 datum_param_store.count());
    pre_calc_frame = new(frame_buf)ObPreCalcExprFrameInfo(phy_plan.get_allocator());
    if (OB_FAIL(expr_cg.generate_calculable_exprs(calculable_exprs,
                                                  *pre_calc_frame))) {
    } else {
      phy_plan.set_fetch_cur_time(stmt.get_fetch_cur_time());
      phy_plan.set_stmt_type(stmt.get_stmt_type());
      phy_plan.set_literal_stmt_type(exec_ctx.get_stmt_factory()->get_query_ctx()->get_literal_stmt_type());

      need_fetch_cur_time = phy_plan.get_fetch_cur_time() && !phy_plan_ctx->has_cur_time();
    }
    if (OB_FAIL(ret)) {
      // set current time before do pre calculation
    } else if (need_fetch_cur_time && FALSE_IT(phy_plan_ctx->set_cur_time(
                                  ObTimeUtility::current_time(), *(exec_ctx.get_my_session())))) {
      // do nothing
    } else if (OB_FAIL(ObPlanCacheObject::pre_calculation(is_ignore_stmt,
                                                          *pre_calc_frame, exec_ctx,
                                                          calc_types))) {
    } else if (OB_UNLIKELY(PRE_CALC_DEFAULT == calc_types &&
                   !phy_plan.get_pre_calc_frames().add_last(pre_calc_frame))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      // do nothing
    }
  }
  return ret;
}

int ObSql::create_expr_constraints(ObQueryCtx &query_ctx, ObExecContext &exec_ctx)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObHiddenColumnItem, 4> pre_calc_exprs;
  ObHiddenColumnItem hidden_column_item;
  int64_t idx = -1;
  if (query_ctx.all_expr_constraints_.empty()) {
    // do nothing
  } else {
    ObIArray<ObExprConstraint> &expr_constraints = query_ctx.all_expr_constraints_;
    ObSEArray<ObHiddenColumnItem, 4> pre_calc_exprs;
    ObHiddenColumnItem hidden_column_item;
    int64_t idx = -1;
    const int64_t dummy_count = -1;
    for (int64_t i = PRE_CALC_RESULT_NULL; OB_SUCC(ret) && i <= PRE_CALC_NOT_PRECISE; ++i) {
      PreCalcExprExpectResult expect_result = static_cast<PreCalcExprExpectResult>(i);
      pre_calc_exprs.reuse();
      for (int64_t j = 0; OB_SUCC(ret) && j < expr_constraints.count(); ++j) {
        if (expr_constraints.at(j).expect_result_ == expect_result) {
          hidden_column_item.expr_ = expr_constraints.at(j).pre_calc_expr_;
          hidden_column_item.hidden_idx_ = ++idx;
          if (OB_ISNULL(hidden_column_item.expr_)) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(hidden_column_item.expr_->extract_info())) {
          } else if (!expr_constraints.at(j).ignore_const_check_ &&
                     OB_UNLIKELY(!ObOptEstUtils::is_calculable_expr(*hidden_column_item.expr_, dummy_count))) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(pre_calc_exprs.push_back(hidden_column_item))) {
          }
        }
      }
      if (OB_SUCC(ret) && !pre_calc_exprs.empty()) {
        if (OB_FAIL(create_expr_constraint(query_ctx,
                                           exec_ctx,
                                           pre_calc_exprs,
                                           expect_result))) {
        }
      }
    }
  }
  return ret;
}

int ObSql::create_expr_constraint(ObQueryCtx &query_ctx,
                                  ObExecContext &exec_ctx,
                                  const ObIArray<ObHiddenColumnItem> &pre_calc_exprs,
                                  const PreCalcExprExpectResult expect_result)
{
  int ret = OB_SUCCESS;
  ObPreCalcExprConstraint *pre_calc_constraint = NULL;
  void *cons_buf = NULL;
  ObPhysicalPlanCtx *plan_ctx = NULL;
  if (OB_ISNULL(exec_ctx.get_sql_ctx()) || OB_ISNULL(plan_ctx = exec_ctx.get_physical_plan_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(cons_buf = exec_ctx.get_allocator().alloc(sizeof(ObPreCalcExprConstraint)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ObStaticEngineExprCG expr_cg(exec_ctx.get_allocator(), exec_ctx.get_my_session(),
                                 exec_ctx.get_sql_ctx()->schema_guard_,
                                 plan_ctx->get_original_param_cnt(),
                                 plan_ctx->get_datum_param_store().count());
    pre_calc_constraint = new(cons_buf)ObPreCalcExprConstraint(exec_ctx.get_allocator());
    pre_calc_constraint->expect_result_ = expect_result;
    if (OB_FAIL(expr_cg.generate_calculable_exprs(pre_calc_exprs,
                                                  pre_calc_constraint->pre_calc_expr_info_))) {
    } else if (OB_UNLIKELY(!query_ctx.all_pre_calc_constraints_.add_last(pre_calc_constraint))) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

// handle execute in text protocol
int ObSql::handle_text_execute(const ObStmt *basic_stmt,
                               ObSqlCtx &sql_ctx,
                               ObResultSet &result)
{
  int ret = OB_SUCCESS;
  const ObExecuteStmt *exec_stmt = static_cast<const ObExecuteStmt*>(basic_stmt);
  sql_ctx.is_text_ps_mode_ = true;
  if (OB_ISNULL(exec_stmt)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObIAllocator &alloc = result.get_exec_context().get_allocator();
    ParamStore param_store((ObWrapperAllocator(alloc)));
    const ObRawExpr *raw_expr = NULL;
    const ObIArray<const ObRawExpr*> &raw_expr_params = exec_stmt->get_params();
    if (OB_FAIL(param_store.reserve(raw_expr_params.count()))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < raw_expr_params.count(); ++i) {
      ObObjParam obj_param;
      if (OB_ISNULL(raw_expr = raw_expr_params.at(i))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObSQLUtils::calc_const_expr(result.get_exec_context(),
                                                     raw_expr,
                                                     obj_param,
                                                     alloc,
                                                     param_store))) {
      } else {
        obj_param.set_accuracy(raw_expr->get_accuracy());
        obj_param.set_result_flag(raw_expr->get_result_flag());
        obj_param.set_collation_level(CS_LEVEL_COERCIBLE);
        obj_param.set_param_meta(obj_param.get_meta());
        if (OB_FAIL(param_store.push_back(obj_param))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(handle_ps_execute(exec_stmt->get_prepare_id(),
                                    exec_stmt->get_prepare_type(),
                                    param_store,
                                    sql_ctx,
                                    result,
                                    false/*is_inner_sql*/))) {
      } else if (OB_FAIL(construct_param_store(param_store, result.get_ps_params()))) {
      }
    }
  }
  return ret;
}

int ObSql::get_reconstructed_batch_stmt(ObPlanCacheCtx &pc_ctx, ObString& stmt_sql)
{
  int ret = OB_SUCCESS;
  if (pc_ctx.sql_ctx_.is_do_insert_batch_opt()) {
    // Restore the original SQL according to the first row of parameters
    if (OB_FAIL(ObPlanCache::restore_param_to_truncated_sql(pc_ctx))) {
      // if rebuild origin sql fail, this sql would rollback
      ret = OB_BATCHED_MULTI_STMT_ROLLBACK;
    } else {
      stmt_sql = pc_ctx.insert_batch_opt_info_.new_reconstruct_sql_;
    }
  } else if (pc_ctx.sql_ctx_.handle_batched_multi_stmt()) {
    if (OB_ISNULL(pc_ctx.sql_ctx_.multi_stmt_item_.get_queries())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_UNLIKELY(pc_ctx.sql_ctx_.multi_stmt_item_.get_queries()->empty())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      stmt_sql = pc_ctx.sql_ctx_.multi_stmt_item_.get_queries()->at(0);
    }
  } else if (pc_ctx.exec_ctx_.has_dynamic_values_table()) {
    stmt_sql = pc_ctx.new_raw_sql_;
  }
  return ret;
}

// If handel failed, rollback implicit started txn by current stmt
// if autocommit is on.
// this is because the txn may be started during generate plan for some
// strange Query.
// We have found one situation:
// udf contains DML stmt and with deterministic property in MySQL mode
void ObSql::rollback_implicit_trans_when_fail(ObResultSet &result, int &ret)
{
  bool ac = false;
  result.get_session().get_autocommit(ac);
  transaction::ObTxDesc *tx = result.get_session().get_tx_desc();
  if (ac && tx && data_plane::tx_desc_id(tx).is_valid()
      && !data_plane::tx_desc_is_explicit(tx)) {
    const transaction::ObTransID txid = data_plane::tx_desc_id(tx);
    int tmp_ret = OB_SUCCESS;
    bool need_disconnect = false;
    if (OB_TMP_FAIL(ObSqlTransControl::rollback_trans(&result.get_session(), need_disconnect))) {
      LOG_WARN("rollback transaction fail, will disconnect", K(tmp_ret), K(result.get_session()), K(txid));
      result.get_exec_context().set_need_disconnect(true);
      ret = tmp_ret;
    } else {
      LOG_INFO("rollback transaction started during get-plan success", K(ret), K(txid));
    }
  }
}


}//end of namespace sql
}//end of namespace oceanbase
