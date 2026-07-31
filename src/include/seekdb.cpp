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

#define USING_LOG_PREFIX CLIENT
#include "seekdb.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>
#include "lib/utility/ob_common_utility.h"  // For set_stackattr()
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/string/ob_string.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/resource/ob_resource_mgr.h"
#include "observer/ob_inner_sql_connection.h"
#include "observer/ob_inner_sql_result.h"
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"
#include "share/ob_server_struct.h"
#include "lib/file/file_directory_utils.h"
#include "lib/utility/utility.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "lib/oblog/ob_log.h"
#include "lib/string/ob_sql_string.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_server_schema_service.h"
#include "lib/container/ob_array.h"
#include "sql/parser/ob_parser.h"
#include "sql/parser/parse_node.h"
#include "share/schema/ob_priv_type.h"
#include "share/ob_define.h"
#include "sql/session/ob_system_variable.h"
#include "share/system_variable/ob_sys_var_class_type.h"
#include "share/ob_errno.h"  // For ob_strerror
#include "sql/ob_sql_utils.h"  // For ObSQLUtils::update_session_last_schema_version (DDL visibility)
#include "lib/ob_define.h"  // For OB_SYS_USER_ID
#include "lib/profile/ob_trace_id.h"  // For ObCurTraceId
#include "lib/worker.h"  // For lib::Worker
#include "lib/thread/threads.h"  // For global_thread_stack_size, THREAD_STACK_RESERVED_SIZE
#include "lib/utility/alloc_assist.h"  // For ACHUNK_PRESERVE_SIZE
#include "lib/utility/ob_smart_call.h"  // For CALL_WITH_NEW_STACK
#ifdef _WIN32
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <process.h>
#include <direct.h>
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#else
#include <unistd.h>
#include <fcntl.h>
#ifdef __APPLE__
#include <sys/mount.h>  // statfs on macOS
#else
#include <sys/statfs.h>
#endif
#include <sys/mman.h>  // For mmap/munmap
#endif
#include <climits>
#include <csignal>
#include <cstdlib>
#ifndef _WIN32
#include <signal.h>  // sigaction, SIGBUS (not used on Windows)
#endif

#ifdef _WIN32
#define SEEKDB_CHDIR(p) ::_chdir(p)
#define SEEKDB_GETCWD(buf, sz) ::_getcwd((buf), static_cast<int>(sz))
#define SEEKDB_GETPID() (static_cast<long>(::_getpid()))
#define SEEKDB_UNLINK(p) ::_unlink(p)
#else
#define SEEKDB_CHDIR(p) ::chdir(p)
#define SEEKDB_GETCWD(buf, sz) ::getcwd((buf), (sz))
#define SEEKDB_GETPID() (static_cast<long>(::getpid()))
#define SEEKDB_UNLINK(p) ::unlink(p)
#endif

static void *seekdb_mmap_anonymous_stack(size_t stack_size)
{
#ifdef _WIN32
  return ::VirtualAlloc(nullptr, stack_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  void *const p =
      ::mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return p;
#endif
}

static void seekdb_munmap_anonymous_stack(void *addr, size_t stack_size)
{
#ifdef _WIN32
  (void)stack_size;
  if (addr != nullptr) {
    ::VirtualFree(addr, 0, MEM_RELEASE);
  }
#else
  (void)::munmap(addr, stack_size);
#endif
}

using namespace oceanbase::common;
using namespace oceanbase::sqlclient;
using namespace oceanbase::observer;
using namespace oceanbase::sql;
using namespace oceanbase::share;
namespace share = oceanbase::share;
using namespace oceanbase::lib;

static oceanbase::observer::ObInnerSQLConnectionPool *get_embed_inner_sql_conn_pool()
{
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    return nullptr;
  }
  return static_cast<oceanbase::observer::ObInnerSQLConnectionPool *>(GCTX.sql_proxy_->get_pool());
}

// RAII: redirect stdout/stderr to /dev/null for the scope so LOG_STDOUT (e.g. "successfully init log writer") does not print.
struct SuppressLogStdoutScope {
    int saved_stdout = -1;
    int saved_stderr = -1;
    SuppressLogStdoutScope() {
#ifdef _WIN32
        saved_stdout = ::_dup(::_fileno(stdout));
        saved_stderr = ::_dup(::_fileno(stderr));
        const int fd = ::_open("NUL", _O_WRONLY);
        if (fd >= 0) {
            ::_dup2(fd, ::_fileno(stdout));
            ::_dup2(fd, ::_fileno(stderr));
            ::_close(fd);
        }
#else
        saved_stdout = dup(STDOUT_FILENO);
        saved_stderr = dup(STDERR_FILENO);
        int fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) {
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);
        }
#endif
    }
    ~SuppressLogStdoutScope() {
#ifdef _WIN32
        if (saved_stderr >= 0) {
            ::_dup2(saved_stderr, ::_fileno(stderr));
            ::_close(saved_stderr);
        }
        if (saved_stdout >= 0) {
            ::_dup2(saved_stdout, ::_fileno(stdout));
            ::_close(saved_stdout);
        }
#else
        if (saved_stderr >= 0) {
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stderr);
        }
        if (saved_stdout >= 0) {
            dup2(saved_stdout, STDOUT_FILENO);
            close(saved_stdout);
        }
#endif
    }
};

// Thread-local error storage for improved error handling
thread_local static std::string g_thread_last_error;
thread_local static int g_thread_last_error_code = SEEKDB_SUCCESS;

// Forward declaration for embedded connection
namespace oceanbase {
namespace observer {
class ObInnerSQLConnection;
} // namespace observer
} // namespace oceanbase

// Forward declarations for internal structures
struct SeekdbResultSet;
struct SeekdbRowData;
struct SeekdbConnection;

// Use OBSERVER macro directly like Python embed does
// OBSERVER is defined in observer/ob_server.h as ObServer::get_instance()

// Store last affected rows per connection for seekdb_affected_rows().
// Must not use thread_local: Go CGO can call seekdb_query and seekdb_affected_rows on different OS threads.

// Internal structures - define SeekdbRowData first, then SeekdbResultSet
// so that SeekdbResultSet can properly delete SeekdbRowData in destructor
struct SeekdbRowData {
    SeekdbResultSet* result_set;
    int64_t row_index;
    bool freed;  // Flag to detect double free
    
    SeekdbRowData(SeekdbResultSet* rs, int64_t idx) 
        : result_set(rs), row_index(idx), freed(false) {}
};

// Structure to store field strings for lifetime management
struct SeekdbFieldStrings {
    std::string col_name;
    std::string org_col_name;
    std::string table_name;
    std::string org_table_name;
    std::string db_name;
};

struct SeekdbResultSet {
    std::vector<std::vector<std::string>> rows;
    std::vector<std::vector<bool>> row_nulls;   // row_nulls[r][c] true iff cell (r,c) is SQL NULL (distinct from empty string)
    std::vector<std::string> column_names;
    int64_t current_row;
    int64_t row_count;
    int32_t column_count;
    std::vector<unsigned long> current_lengths;  // For mysql_fetch_lengths() compatibility
    SeekdbRowData* current_row_data;  // Current row data for fetch_lengths
    std::vector<SeekdbField> fields;  // For mysql_fetch_fields() compatibility
    std::vector<SeekdbFieldStrings> field_strings;  // Store strings for field lifetime management
    unsigned int current_field;  // Current field position for seekdb_fetch_field()
    bool use_result_mode;  // true for use_result (streaming), false for store_result (buffered)
    struct SeekdbConnection* owner_conn;  // Connection that owns this result set (for cleanup)
    bool freed;  // Flag to detect double free
    
    SeekdbResultSet() : current_row(-1), row_count(0), column_count(0), current_row_data(nullptr), 
                        current_field(0), use_result_mode(false), owner_conn(nullptr), freed(false) {}
    
    ~SeekdbResultSet() {
        if (current_row_data) {
            // Check if already freed to prevent double free
            if (!current_row_data->freed) {
                delete current_row_data;
            }
            current_row_data = nullptr;
        }
    }
};

struct SeekdbConnection {
    ObInnerSQLConnection* embed_conn;  // Embedded connection
    ObSQLSessionInfo* embed_session;  // Session for transaction management
    ObCommonSqlProxy::ReadResult* embed_result;  // Query result
    SeekdbResultSet* last_result_set;  // Last result set for mysql_store_result() compatibility
    SeekdbResultSet* use_result_set;  // Result set for mysql_use_result() (streaming mode)
    std::vector<SeekdbResultSet*> result_sets;  // Multiple result sets queue
    int current_result_index;  // Current result set index for multiple results
    std::string last_error;
    bool initialized;
    unsigned long long last_affected_rows;
    
    SeekdbConnection() : embed_conn(nullptr), embed_session(nullptr), 
                         embed_result(nullptr), last_result_set(nullptr), 
                         use_result_set(nullptr), current_result_index(-1), initialized(false),
                         last_affected_rows(0) {}
    ~SeekdbConnection() {
        // Cleanup last result set (only if still owned by connection)
        // If it was transferred to user via seekdb_store_result(), it's nullptr
        if (last_result_set) {
            // Check if already freed to prevent double free
            if (!last_result_set->freed) {
                delete last_result_set;
            }
            last_result_set = nullptr;
        }
        // Cleanup use result set
        if (use_result_set) {
            // Check if already freed to prevent double free
            if (!use_result_set->freed) {
                delete use_result_set;
            }
            use_result_set = nullptr;
        }
        // Cleanup multiple result sets
        for (auto* rs : result_sets) {
            if (rs) {
                // Check if already freed to prevent double free
                if (!rs->freed) {
                    delete rs;
                }
            }
        }
        result_sets.clear();
        // Cleanup embedded result
        if (embed_result) {
            embed_result->close();
            embed_result->~ReadResult();
            ob_free(embed_result);
            embed_result = nullptr;
        }
        // Release connection (using OBSERVER macro like Python embed)
        if (embed_conn) {
            oceanbase::observer::ObInnerSQLConnectionPool *pool = get_embed_inner_sql_conn_pool();
            if (OB_NOT_NULL(pool)) {
                pool->release(embed_conn, true);
            }
            embed_conn = nullptr;
        }
        // Release session (like Python embed does)
        if (embed_session) {
            GCTX.session_mgr_->revert_session(embed_session);
            embed_session = nullptr;
        }
    }
};

struct SeekdbStmtData {
    SeekdbConnection* conn;
    std::string sql;
    std::vector<SeekdbBind> param_binds;
    std::vector<SeekdbBind> result_binds;
    uint64_t ps_stmt_id;  // Prepared statement ID from OceanBase
    unsigned long param_count;
    unsigned long result_count;
    SeekdbResultSet* result_set;
    std::string last_error;
    std::vector<oceanbase::common::ObObjType> param_column_types;  // Column types for parameters (from table schema)
    bool prepared;
    bool executed;
    
    SeekdbStmtData(SeekdbConnection* c) 
        : conn(c), ps_stmt_id(0), param_count(0), result_count(0), 
          result_set(nullptr), prepared(false), executed(false) {}
    
    ~SeekdbStmtData() {
        if (result_set) {
            delete result_set;
            result_set = nullptr;
        }
    }
};

// VARBINARY(512) length for _id column; semantics from bind type (SEEKDB_TYPE_VARBINARY_ID), no SQL parsing
static const unsigned int VARBINARY_ID_LENGTH = 512;

// VECTOR read: convert raw float32 binary to JSON string "[v1, v2, ...]" without precision rounding.
// Directly formats each float (e.g. 1.1, 2.2, 3.3) so result is "[1.1, 2.2, 3.3]".
static bool vector_binary_to_json(const char* ptr, int64_t len, std::string& out) {
    if (!ptr || len <= 0 || (len % sizeof(float)) != 0) {
        out.clear();
        return false;
    }
    const int64_t n = len / static_cast<int64_t>(sizeof(float));
    const float* f = reinterpret_cast<const float*>(ptr);
    std::string buf;
    buf.reserve(static_cast<size_t>(n * 16));
    buf.push_back('[');
    char num[64];
    for (int64_t i = 0; i < n; ++i) {
        int wr = snprintf(num, sizeof(num), "%g", f[i]);
        if (wr <= 0 || wr >= static_cast<int>(sizeof(num))) {
            out.clear();
            return false;
        }
        if (i > 0) buf.append(", ");
        buf.append(num, static_cast<size_t>(wr));
    }
    buf.push_back(']');
    out = std::move(buf);
    return true;
}

static std::string seekdb_escape_sql_single_quoted(const std::string& str_val, bool escape_backslash) {
    std::string escaped;
    escaped.reserve(str_val.size() + 16);
    for (char c : str_val) {
        if (escape_backslash && c == '\\') {
            escaped += "\\\\";
        } else if (c == '\'') {
            escaped += "''";
        } else {
            escaped += c;
        }
    }
    return escaped;
}

static bool seekdb_is_json_bind_string(const std::string& str_val) {
    return str_val.size() >= 2 && str_val.front() == '{' && str_val.find('"') != std::string::npos;
}

static bool seekdb_is_sparse_vector_bind_string(const std::string& str_val) {
    return str_val.size() >= 2 && str_val.front() == '{' && str_val.find('"') == std::string::npos &&
           str_val.find(':') != std::string::npos;
}

static std::string seekdb_format_string_bind_param(
    SeekdbConnection* conn,
    const SeekdbStmtData* stmt_data,
    size_t param_idx,
    const std::string& str_val) {
    if (!str_val.empty() && str_val.front() == '[') {
        return str_val;
    }
    if (seekdb_is_sparse_vector_bind_string(str_val)) {
        return "'" + seekdb_escape_sql_single_quoted(str_val, false) + "'";
    }
    size_t col_idx = param_idx;
    if (stmt_data != nullptr && !stmt_data->param_column_types.empty()) {
        col_idx = param_idx % stmt_data->param_column_types.size();
    }
    const bool json_column =
        seekdb_is_json_bind_string(str_val) ||
        (stmt_data != nullptr && col_idx < stmt_data->param_column_types.size() &&
         ob_is_json_tc(stmt_data->param_column_types[col_idx]));
    if (json_column) {
        return "'" + seekdb_escape_sql_single_quoted(str_val, true) + "'";
    }
    const size_t buf_len = str_val.length() * 2 + 1;
    std::vector<char> escaped_buf(buf_len);
    const unsigned long escaped_length = seekdb_real_escape_string(
        static_cast<SeekdbHandle>(conn),
        escaped_buf.data(),
        static_cast<unsigned long>(escaped_buf.size()),
        str_val.c_str(),
        static_cast<unsigned long>(str_val.length()));
    if (escaped_length != static_cast<unsigned long>(-1)) {
        return "'" + std::string(escaped_buf.data(), escaped_length) + "'";
    }
    return "'" + seekdb_escape_sql_single_quoted(str_val, false) + "'";
}

static void seekdb_materialize_string_cell(
    oceanbase::common::ObObj& obj,
    oceanbase::common::ObObjType obj_type,
    oceanbase::common::ObArenaAllocator& lob_allocator,
    std::string& cell_out) {
    using namespace oceanbase::common;
    cell_out.clear();
    if (obj.is_null()) {
        return;
    }
    ObString str_val;
    const int get_ret = obj.get_string(str_val);
    if (OB_SUCCESS == get_ret && str_val.ptr() != nullptr && str_val.length() > 0) {
        cell_out.assign(str_val.ptr(), str_val.length());
        return;
    }
    if (ob_is_text_tc(obj_type) || ob_is_string_type(obj_type)) {
        ObString lob_str;
        if (OB_SUCCESS == obj.read_lob_data(lob_allocator, lob_str) && lob_str.ptr() != nullptr &&
            lob_str.length() > 0) {
            cell_out.assign(lob_str.ptr(), lob_str.length());
            return;
        }
    }
    char buf[4096];
    int64_t pos = 0;
    if (OB_SUCCESS == obj.print_sql_literal(buf, sizeof(buf), pos) && pos > 0) {
        std::string sql_literal(buf, static_cast<size_t>(pos));
        if (sql_literal.length() >= 2 && sql_literal.front() == '\'' && sql_literal.back() == '\'') {
            sql_literal = sql_literal.substr(1, sql_literal.length() - 2);
            for (size_t q = 0; (q = sql_literal.find("''", q)) != std::string::npos; q += 1) {
                sql_literal.replace(q, 2, "'");
            }
        }
        cell_out = std::move(sql_literal);
        return;
    }
    std::vector<char> large_buf(2 * 1024 * 1024, 0);
    pos = 0;
    if (OB_SUCCESS == obj.print_sql_literal(large_buf.data(), large_buf.size(), pos) && pos > 0) {
        std::string sql_literal(large_buf.data(), static_cast<size_t>(pos));
        if (sql_literal.length() >= 2 && sql_literal.front() == '\'' && sql_literal.back() == '\'') {
            sql_literal = sql_literal.substr(1, sql_literal.length() - 2);
            for (size_t q = 0; (q = sql_literal.find("''", q)) != std::string::npos; q += 1) {
                sql_literal.replace(q, 2, "'");
            }
        }
        cell_out = std::move(sql_literal);
    }
}

// Global state
static std::mutex g_init_mutex;
static bool g_initialized = false;
static bool g_embedded_opened = false;
static ObSqlString g_embedded_pid_file;
static bool g_embedded_pid_locked = false;
static char g_embedded_work_dir[PATH_MAX];
static char g_embedded_base_dir[PATH_MAX] = {0};  // Absolute path: opened db path for same-path reuse
static bool g_closing = false;  // Flag to indicate we're in closing process
#ifndef _WIN32
static struct sigaction g_old_segv_handler;  // Store original SIGSEGV handler
static struct sigaction g_old_sigabrt_handler;  // Store original SIGABRT handler
static struct sigaction g_old_sigbus_handler;  // Store original SIGBUS handler
#endif
static bool g_segv_handler_installed = false;
static bool g_sigabrt_handler_installed = false;
static bool g_sigbus_handler_installed = false;

// Set when embedded DB was ever successfully opened; never cleared.
// Used by the signal handler so we can recognize cleanup segfaults even when
// g_embedded_opened has already been set to false by seekdb_close() or during destructors.
static bool g_embedded_ever_opened = false;

#ifndef _WIN32
// Signal handler for SIGSEGV during cleanup
// This allows graceful handling of segfaults during static destructors
// Must be defined before seekdb_library_init() which uses it
static void segv_handler_during_close(int sig, siginfo_t* info, void* context) {
    // If we're in the closing process or database was (or had been) opened, treat as cleanup segfault
    // This is expected during OceanBase static destructor cleanup at program exit
    if (g_closing || g_embedded_opened || g_embedded_ever_opened) {
        // Exit gracefully with success code since cleanup segfault is expected
        _exit(0);
    }

    // If not in closing process and database not opened, restore original handler
    if (g_segv_handler_installed) {
        sigaction(SIGSEGV, &g_old_segv_handler, nullptr);
        g_segv_handler_installed = false;
        raise(SIGSEGV);
    }
}

// Signal handler for SIGABRT during cleanup
// ob_abort() triggers SIGABRT during static destructors (e.g. Node.js N-API on macOS).
// Catch it and exit gracefully when we're in the embedded DB cleanup path.
static void sigabrt_handler_during_close(int sig, siginfo_t* info, void* context) {
    if (g_closing || g_embedded_opened || g_embedded_ever_opened) {
        _exit(0);
    }
    if (g_sigabrt_handler_installed) {
        sigaction(SIGABRT, &g_old_sigabrt_handler, nullptr);
        g_sigabrt_handler_installed = false;
        raise(SIGABRT);
    }
}

// Signal handler for SIGBUS during cleanup
// SIGBUS can occur during static destructors (e.g. Rust FFI on macOS).
// Catch it and exit gracefully when we're in the embedded DB cleanup path.
static void sigbus_handler_during_close(int sig, siginfo_t* info, void* context) {
    if (g_closing || g_embedded_opened || g_embedded_ever_opened) {
        _exit(0);
    }
    if (g_sigbus_handler_installed) {
        sigaction(SIGBUS, &g_old_sigbus_handler, nullptr);
        g_sigbus_handler_installed = false;
        raise(SIGBUS);
    }
}
#endif  // !_WIN32

// Use OBSERVER macro directly like Python embed does
// No need to cache since ObServer::get_instance() is a singleton

// =============================================================================
// Absolute path handling: same-process reuse when multiple clients use same path
// =============================================================================
// - to_absolute_path: normalize db_dir to absolute for opts and for comparison
// - same_embedded_path: compare two paths (e.g. requested vs g_embedded_base_dir)
// - g_embedded_base_dir: stored absolute path of opened db; reuse open if same path

static bool is_path_absolute(const char* p)
{
    if (p == nullptr || p[0] == '\0') {
        return false;
    }
    if (p[0] == '/') {
        return true;
    }
#ifdef _WIN32
    // "C:\..." or "C:/..."
    if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':') {
        return true;
    }
    // UNC "\\server\share\..."
    if (p[0] == '\\' && p[1] == '\\') {
        return true;
    }
#endif
    return false;
}

static int to_absolute_path(const char* cwd, ObSqlString& dir) {
    int ret = OB_SUCCESS;
    if (!dir.empty() && dir.ptr()[0] != '\0' && !is_path_absolute(dir.ptr())) {
        char abs_path[OB_MAX_FILE_NAME_LENGTH] = {0};
        if (snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, dir.ptr()) >= static_cast<int>(sizeof(abs_path))) {
            ret = OB_SIZE_OVERFLOW;
        } else if (OB_FAIL(dir.assign(abs_path))) {
            // Error
        }
    }
    return ret;
}

// Normalize path for comparison: strip trailing slash (except for "/")
static void path_normalize_for_cmp(const char* path_in, char* out, size_t out_size) {
    if (!path_in || out_size == 0) return;
    size_t len = strlen(path_in);
    while (len > 1 && path_in[len - 1] == '/') len--;
    size_t n = len < out_size - 1 ? len : out_size - 1;
    memcpy(out, path_in, n);
    out[n] = '\0';
}

static bool same_embedded_path(const char* a, const char* b) {
    char na[PATH_MAX], nb[PATH_MAX];
    path_normalize_for_cmp(a, na, sizeof(na));
    path_normalize_for_cmp(b, nb, sizeof(nb));
    return strcmp(na, nb) == 0;
}

static int read_pid_from_file(const char* pidfile, long& pid_out) {
#ifdef _WIN32
    const int fd = ::_open(pidfile, _O_RDONLY);
    if (fd < 0) return -1;
    char buf[64];
    const int n = ::_read(fd, buf, sizeof(buf) - 1);
    ::_close(fd);
    if (n <= 0) return -1;
#else
    int fd = open(pidfile, O_RDONLY);
    if (fd < 0) return -1;
    char buf[64];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
#endif
    buf[n] = '\0';
    char* end = nullptr;
    long pid = strtol(buf, &end, 10);
    if (end == buf || pid <= 0) return -1;
    pid_out = pid;
    return 0;
}

// Helper function to convert ObString to std::string
static std::string obstring_to_string(const ObString& str) {
    return std::string(str.ptr(), str.length());
}

// Helper function to set error message
static void set_error(SeekdbConnection* conn, const char* msg) {
    if (conn) {
        conn->last_error = msg ? msg : "Unknown error";
    }
    // Also update thread-local error
    g_thread_last_error = msg ? msg : "Unknown error";
}

// Helper function to set error code and message
static void set_error_code(int code, const char* msg) {
    g_thread_last_error_code = code;
    g_thread_last_error = msg ? msg : "Unknown error";
}

extern "C" {

// Constructor function to initialize global_thread_stack_size when library is loaded
// This is critical for Node.js worker processes where the library is loaded via dlopen
// before seekdb_open() is called. Without this, thread creation may fail with
// "pthread_create: Invalid argument" because global_thread_stack_size is not set.
//
// CRITICAL: Since libseekdb.so depends on liboceanbase.so, liboceanbase.so will be
// loaded first when libseekdb.so is loaded. If liboceanbase.so's static initializers
// create threads, they will execute before this constructor. However, liboceanbase.so
// typically doesn't create threads during static initialization - it only creates
// threads when observer.init() or similar functions are called.
//
// The real issue is that when koffi loads the library, the library's static initializers
// may trigger code paths that eventually try to create threads (e.g., through singleton
// initialization). By setting global_thread_stack_size here, we ensure it's set before
// any such code paths execute.
//
// Note: We use a lower priority (200) to ensure this runs after liboceanbase.so's
// constructors, but before any code that might create threads.
__attribute__((constructor(200)))
static void seekdb_library_init() {
    // Set global_thread_stack_size to a safe default when library is loaded
    // This ensures threads can be created even if seekdb_open() hasn't been called yet
    // Use a larger default (2MB) to ensure it works in all scenarios
    const int64_t default_stack_size = (1LL << 21);  // 2MB
    int64_t calculated_size = default_stack_size - THREAD_STACK_RESERVED_SIZE - ACHUNK_PRESERVE_SIZE;
    
    // Ensure stack size is at least 1MB for better compatibility
    // This is larger than the typical minimum (512KB) to handle edge cases
    if (calculated_size < (1L << 20)) {  // 1MB minimum
        calculated_size = (1L << 20);
    }
    
    // Only set if not already set (to avoid overwriting a value set by liboceanbase.so)
    if (global_thread_stack_size <= 0 || global_thread_stack_size < (512L << 10)) {
        global_thread_stack_size = calculated_size;
    }

#ifndef _WIN32
    // Install global SIGSEGV handler to catch segfaults during static destructors
    // This allows graceful handling of OceanBase static destructor issues at program exit
    struct sigaction sa;
    sa.sa_sigaction = segv_handler_during_close;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_SIGINFO;

    if (sigaction(SIGSEGV, &sa, &g_old_segv_handler) == 0) {
        g_segv_handler_installed = true;
    }
    sa.sa_sigaction = sigabrt_handler_during_close;
    if (sigaction(SIGABRT, &sa, &g_old_sigabrt_handler) == 0) {
        g_sigabrt_handler_installed = true;
    }
    sa.sa_sigaction = sigbus_handler_during_close;
    if (sigaction(SIGBUS, &sa, &g_old_sigbus_handler) == 0) {
        g_sigbus_handler_installed = true;
    }
#endif
}

// Internal implementation of seekdb_open, called on a dedicated stack
// Matches Python embed's do_open_() behavior:
// - If port > 0: embed_mode = false (server mode)
// - If port <= 0: embed_mode = true (embedded mode)
static int do_seekdb_open_inner(const char* db_dir, int port) {
    if (g_embedded_opened) {
        // Absolute path: same-process reuse if requested path equals g_embedded_base_dir.
        bool same_path = false;
        if (g_embedded_base_dir[0] != '\0') {
            char cwd_buf[PATH_MAX];
            if (SEEKDB_GETCWD(cwd_buf, sizeof(cwd_buf)) != nullptr) {
                ObSqlString req_abs;
                if (req_abs.assign(db_dir) == OB_SUCCESS && to_absolute_path(cwd_buf, req_abs) == OB_SUCCESS &&
                    same_embedded_path(req_abs.ptr(), g_embedded_base_dir)) {
                    same_path = true;
                }
            }
        }
        if (same_path) {
            return SEEKDB_SUCCESS;
        }
        // Different path: close current embedded DB, then reopen the new path below
        if (g_embedded_pid_locked) {
            char pid_path[PATH_MAX];
            snprintf(pid_path, sizeof(pid_path), "%s/run/seekdb.pid", g_embedded_base_dir);
            SEEKDB_UNLINK(pid_path);
            g_embedded_pid_locked = false;
        }
        g_embedded_opened = false;
        g_embedded_base_dir[0] = '\0';
        if (GCTX.is_inited()) {
            OBSERVER.destroy();
            ob_usleep(100 * 1000);  // 100ms
        }
    }

    // Get observer instance
    // CRITICAL: We need to call observer.destroy() to clean up any previous state,
    // but this sets stop_ = true. However, observer.start() checks stop_ status
    // during startup (line 1144, 1184), and only resets it after successful startup (line 1099-1101).
    // The issue is that if stop_ is true, observer.start() may fail or exit early.
    // 
    // Solution: Call observer.destroy() to clean up, but we need to ensure that
    // observer.start() can handle stop_ = true initially. Looking at the code,
    // observer.start() resets stop_ after successful startup, so if we can get
    // through the startup process, stop_ will be reset. However, the checks at
    // line 1144 and 1184 may cause early exit if stop_ is true.
    //
    // Actually, from the code, observer.start() at line 1144 checks stop_ and sets
    // ret = OB_SERVER_IS_STOPPING if stop_ is true. This causes the startup to fail.
    // 
    // We need to call observer.destroy() to clean up, but then we need a way to reset
    // stop_ before calling observer.start(). Since stop_ is private, we can't access it.
    // Use OBSERVER macro directly like Python embed does
    // Only destroy if observer was previously initialized
    // This avoids setting stop_ = true unnecessarily
    if (GCTX.is_inited()) {
        OBSERVER.destroy();
        ob_usleep(100 * 1000);  // 100ms
    }
    
    // Set memory limit to unlimited before init (aligned with main.cpp inner_main)
    // This is critical for fork scenarios where memory limits may be inherited
    oceanbase::lib::set_memory_limit(INT_MAX64);
    
    // Note: global_thread_stack_size is already set by the library constructor
    // (seekdb_library_init) when the library is loaded. This ensures it's set
    // even in Node.js worker processes where the library is loaded via dlopen
    // before seekdb_open() is called.
    
    // CRITICAL: Explicitly initialize memory allocator and resource manager after fork
    // In fork scenarios, the child process inherits the parent's memory state,
    // but the memory allocator and resource manager may need explicit initialization to function correctly.
    // Calling get_instance() ensures the singletons are initialized.
    // This must be done BEFORE any memory allocations (including thread stack allocations).
    try {
        oceanbase::lib::ObMallocAllocator::get_instance();
    } catch (const std::exception& e) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    try {
        oceanbase::lib::ObResourceMgr::get_instance();
    } catch (const std::exception& e) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    int ret = OB_SUCCESS;
    ObServerOptions opts;
    // Match Python embed's behavior:
    // - Default: embed_mode = true, port = 2881
    // - If port > 0: embed_mode = false (server mode), port = specified port
    opts.port_ = 2881;
    bool embed_mode = true;  // Default to embed mode
    if (port > 0) {
        opts.port_ = port;
        embed_mode = false;  // Server mode when port is specified
    }
    opts.embedded_ = embed_mode;
    opts.use_ipv6_ = false;
    
    
    // Set default parameters
    const char* params[][2] = {
        {"memory_limit", "1G"},
        {"log_disk_size", "2G"}
    };
    try {
        for (int i = 0; OB_SUCC(ret) && i < 2; i++) {
            ObString key = ObString::make_string(params[i][0]);
            ObString value = ObString::make_string(params[i][1]);
            if (OB_FAIL(opts.parameters_.push_back(std::make_pair(key, value)))) {
                break;
            }
        }
    } catch (const std::exception& e) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    // Note: global_thread_stack_size is already set earlier (before memory allocator init)
    // observer.init() will update it based on config (default is 512K), but we've set a safe default (2MB)
    // If stack_size parameter is not set, observer.init() will use default 512K, but our library_init
    // has already set a safe default, so threads can be created before observer.init()
    
    
    char buffer[PATH_MAX];
    ObSqlString work_abs_dir;
    ObSqlString slog_dir;
    ObSqlString sstable_dir;
    int64_t start_time = ObTimeUtility::current_time();
    
    ObWarningBuffer::set_warn_log_on(true);
    
    if (OB_FAIL(ret)) {
    } else if (SEEKDB_GETCWD(buffer, sizeof(buffer)) == nullptr) {
        ret = OB_ERR_UNEXPECTED;
        set_error(nullptr, "getcwd failed");
    } else {
    }
    
    if (OB_SUCC(ret)) {
    }
    if (OB_FAIL(work_abs_dir.assign(buffer))) {
        ret = OB_ERR_UNEXPECTED;
    } else {
    }
    
    if (OB_SUCC(ret)) {
        try {
            if (OB_FAIL(opts.base_dir_.assign(db_dir))) {
                set_error(nullptr, "assign base dir failed");
            } else {
            }
        } catch (const std::exception& e) {
            return SEEKDB_ERROR_MEMORY_ALLOC;
        }
    }
    
    // Continue with data_dir, redo_dir assignments
    try {
        if (OB_SUCC(ret) && OB_FAIL(opts.data_dir_.assign_fmt("%s/store", opts.base_dir_.ptr()))) {
            set_error(nullptr, "assign data dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(opts.redo_dir_.assign_fmt("%s/store/redo", opts.data_dir_.ptr()))) {
            set_error(nullptr, "assign redo dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(to_absolute_path(work_abs_dir.ptr(), opts.base_dir_))) {
            set_error(nullptr, "get base dir absolute path failed");
        } else if (OB_SUCC(ret) && OB_FAIL(to_absolute_path(work_abs_dir.ptr(), opts.data_dir_))) {
            set_error(nullptr, "get data dir absolute path failed");
        } else if (OB_SUCC(ret) && OB_FAIL(to_absolute_path(work_abs_dir.ptr(), opts.redo_dir_))) {
            set_error(nullptr, "get redo dir absolute path failed");
        } else if (OB_SUCC(ret) && OB_FAIL(g_embedded_pid_file.assign("./run/seekdb.pid"))) {
            // Note: pid file path is relative to base_dir after chdir
            set_error(nullptr, "get pidfile path failed");
        } else if (OB_SUCC(ret)) {
        }
    } catch (const std::exception& e) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    
#ifndef _WIN32
    struct statfs fs_info;
#ifndef TMPFS_MAGIC
    const long TMPFS_MAGIC = 0x01021994;
#endif
#endif
    try {
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.base_dir_.ptr()))) {
            set_error(nullptr, "create base dir failed");
#ifndef _WIN32
        } else if (statfs(opts.base_dir_.ptr(), &fs_info) != 0) {
            ret = OB_ERR_UNEXPECTED;
            set_error(nullptr, "stat base dir failed");
        } else if (fs_info.f_type == TMPFS_MAGIC) {
            ret = OB_NOT_SUPPORTED;
            set_error(nullptr, "not support tmpfs directory");
#endif
        } else if (-1 == SEEKDB_CHDIR(opts.base_dir_.ptr())) {
            ret = OB_ERR_UNEXPECTED;
            set_error(nullptr, "change dir failed");
        } else {
        }
        
        if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path(opts.data_dir_.ptr()))) {
            set_error(nullptr, "create data dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path(opts.redo_dir_.ptr()))) {
            set_error(nullptr, "create redo dir failed");
        } else if (OB_SUCC(ret)) {
        }
        
        if (OB_SUCC(ret) && (OB_FAIL(slog_dir.assign_fmt("%s/slog", opts.data_dir_.ptr())) ||
                   OB_FAIL(sstable_dir.assign_fmt("%s/sstable", opts.data_dir_.ptr())))) {
            set_error(nullptr, "calculate slog and sstable dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path(slog_dir.ptr()))) {
            set_error(nullptr, "create slog dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path(sstable_dir.ptr()))) {
            set_error(nullptr, "create sstable dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path("./run"))) {
            set_error(nullptr, "create run dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path("./etc"))) {
            set_error(nullptr, "create etc dir failed");
        } else if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path("./log"))) {
            set_error(nullptr, "create log dir failed");
        } else if (OB_SUCC(ret)) {
        }
    } catch (const std::exception& e) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    try {
        if (OB_SUCC(ret) && OB_FAIL(start_daemon(g_embedded_pid_file.ptr(), true))) {
            // Same-process reuse: if pid file is locked by us, db is already open (e.g. absolute path
            // used by multiple clients in same process, or race between concurrent open() calls).
            long pid_in_file = 0;
            int read_ret = read_pid_from_file(g_embedded_pid_file.ptr(), pid_in_file);
            if (read_ret == 0 && pid_in_file == SEEKDB_GETPID()) {
                ret = OB_SUCCESS;
                g_embedded_opened = true;
                g_embedded_ever_opened = true;
                strncpy(g_embedded_work_dir, work_abs_dir.ptr(), sizeof(g_embedded_work_dir) - 1);
                g_embedded_work_dir[sizeof(g_embedded_work_dir) - 1] = '\0';
                strncpy(g_embedded_base_dir, opts.base_dir_.ptr(), sizeof(g_embedded_base_dir) - 1);
                g_embedded_base_dir[sizeof(g_embedded_base_dir) - 1] = '\0';
                return SEEKDB_SUCCESS;
            }
            if (read_ret != 0) {
                set_error(nullptr, "database already opened in this process (pid file locked)");
            } else {
                set_error(nullptr, "database opened by another process");
            }
        } else if (OB_SUCC(ret)) {
        }
    } catch (const std::exception& e) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    if (OB_SUCC(ret)) {
        
        g_embedded_pid_locked = true;
        strncpy(g_embedded_work_dir, work_abs_dir.ptr(), sizeof(g_embedded_work_dir) - 1);
        g_embedded_work_dir[sizeof(g_embedded_work_dir) - 1] = '\0';
        strncpy(g_embedded_base_dir, opts.base_dir_.ptr(), sizeof(g_embedded_base_dir) - 1);
        g_embedded_base_dir[sizeof(g_embedded_base_dir) - 1] = '\0';
        
        OB_LOGGER.set_log_level("INFO");
        // set_file_name for log file (same as Python embed ob_embed_impl.cpp do_open_)
        ObSqlString log_file;
        try {
            if (OB_FAIL(log_file.assign_fmt("%s/log/seekdb.log", opts.base_dir_.ptr()))) {
                set_error(nullptr, "calculate log file failed");
                return SEEKDB_ERROR_MEMORY_ALLOC;
            }
            OB_LOGGER.set_file_name(log_file.ptr(), true, false);
        } catch (const std::exception& e) {
            return SEEKDB_ERROR_MEMORY_ALLOC;
        }
        
        // Create worker to make this thread having a binding worker (aligned with main.cpp)
        oceanbase::lib::Worker worker;
        oceanbase::lib::Worker::set_worker_to_thread_local(&worker);
        
        ObPLogWriterCfg log_cfg;
        
        if (OB_FAIL(ret)) {
        } else {
            try {
                ret = OBSERVER.init(opts, log_cfg);
            } catch (const std::exception& e) {
                return SEEKDB_ERROR_MEMORY_ALLOC;
            }
        }
        
        int ret_check = ret;
        
        if (ret_check != 0) {
            
            // If OB_INIT_TWICE, it means some static initialization was already done
            // This can happen if sql::init_sql_expr_static_var() was called before
            // However, if observer.init() returns OB_INIT_TWICE early, startup_accel_handler_.init()
            // may not have been called, causing startup_accel_handler_.start() to fail.
            // We need to ensure that all initialization steps are completed even if OB_INIT_TWICE
            // is returned. However, since startup_accel_handler_ is a private member, we cannot
            // directly call its init() method. Instead, we need to ensure that observer.init()
            // completes all initialization steps even when OB_INIT_TWICE is returned.
            //
            // Actually, from the code, observer.init() uses FAILEDx() macro which continues
            // execution even if a function returns OB_INIT_TWICE. So if sql::init_sql_expr_static_var()
            // returns OB_INIT_TWICE, observer.init() will continue and call startup_accel_handler_.init().
            // The issue is that observer.init() may return OB_INIT_TWICE at the end, but all
            // initialization steps should have been completed.
            //
            // Let's check if startup_accel_handler_ is initialized by checking if observer.start()
            // can proceed. If startup_accel_handler_.start() fails with OB_NOT_INIT, we know
            // that startup_accel_handler_.init() was not called.
            if (OB_INIT_TWICE == ret) {
                LOG_WARN("observer init returned OB_INIT_TWICE, continuing anyway", K(ret));
                ret = OB_SUCCESS;  // Ignore OB_INIT_TWICE and continue
                // Note: observer.start() will reset stop_ flags (prepare_stop_, stop_, has_stopped_)
                // at line 1099-1101 in ob_server.cpp, but only after all startup steps succeed.
                // However, observer.start() checks stop_ status at line 1144 and 1184.
                // If stop_ is true, it may cause some steps to fail or exit early.
                // Since we removed the observer.destroy() call at the beginning, stop_ should
                // be in its initial state (true from constructor, but observer.start() should
                // handle this). However, if observer was previously destroyed, stop_ might be true.
                // We rely on observer.start() to reset stop_ after successful startup.
                // Continue to observer.start() below
            } else {
                LOG_WARN("observer init failed", K(ret));
                const char* err_msg = ob_strerror(ret);
                set_error(nullptr, "observer init failed");
                // Clean up partially initialized observer
                OBSERVER.destroy();
            }
        }
        
        // Continue with observer.start() if init succeeded (or OB_INIT_TWICE was ignored)
        if (OB_SUCC(ret) && OB_FAIL(OBSERVER.start())) {
            // stdout already restored above
            LOG_WARN("observer start failed", K(ret));
            const char* err_msg = ob_strerror(ret);
            set_error(nullptr, "observer start failed");
            // Clean up partially initialized observer
            OBSERVER.destroy();
        } else if (-1 == SEEKDB_CHDIR(g_embedded_work_dir)) {
            ret = OB_ERR_UNEXPECTED;
            set_error(nullptr, "change dir failed");
        } else {
            FLOG_INFO("observer start finish wait service ", "cost", ObTimeUtility::current_time() - start_time);
            // Wait until observer reports serving (replaces legacy ObRootService readiness checks).
            while (GCTX.start_service_time_ <= 0) {
                ob_usleep(100 * 1000);  // 100ms
            }
            FLOG_INFO("seekdb start success ", "cost", ObTimeUtility::current_time() - start_time);
#ifdef OB_BUILD_EMBED_MODE
            // Ensure Change Stream threads leave bootstrap wait (embed may stop at IN_SERVICE).
            if (GCTX.start_service_time_ <= 0) {
              GCTX.start_service_time_ = ObTimeUtility::current_time();
            }
            GCTX.in_bootstrap_ = false;
#endif
        }
        // stdout already restored above
    }
    
    if (OB_SUCCESS == ret) {
        g_embedded_opened = true;
        g_embedded_ever_opened = true;
        return SEEKDB_SUCCESS;
    } else {
        if (g_embedded_pid_locked) {
            SEEKDB_UNLINK(g_embedded_pid_file.ptr());
            g_embedded_pid_locked = false;
        }
        return SEEKDB_ERROR_CONNECTION_FAILED;
    }
}

int seekdb_open(const char* db_dir) {
    if (!db_dir) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (g_embedded_opened) {
        // Absolute path: reuse open if same path.
        if (g_embedded_base_dir[0] != '\0') {
            char cwd_buf[PATH_MAX];
            if (SEEKDB_GETCWD(cwd_buf, sizeof(cwd_buf)) != nullptr) {
                ObSqlString req_abs;
                if (req_abs.assign(db_dir) == OB_SUCCESS && to_absolute_path(cwd_buf, req_abs) == OB_SUCCESS &&
                    same_embedded_path(req_abs.ptr(), g_embedded_base_dir)) {
                    return SEEKDB_SUCCESS;
                }
            }
        }
        return SEEKDB_SUCCESS;
    }
    
    // Use CALL_WITH_NEW_STACK to execute on a dedicated stack (aligned with Python embed)
    // This avoids issues with pthread_getattr_np returning invalid values in FFI environments
    // The dedicated stack has known size and address, so OceanBase's stack overflow checks work correctly
    const size_t stack_size = 1LL << 20;  // 1MB (same as Python embed)
    void* stack_addr = seekdb_mmap_anonymous_stack(stack_size);
#ifdef _WIN32
    if (stack_addr == nullptr) {
#else
    if (MAP_FAILED == stack_addr) {
#endif
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    // CRITICAL: Set a valid stack attribute before calling CALL_WITH_NEW_STACK
    // call_with_new_stack() internally calls get_stackattr() to save the original stack attrs,
    // but in FFI environments (Node.js/V8), pthread_getattr_np may return invalid values.
    // By pre-setting a valid stack attribute, get_stackattr() will use our cached values.
    // We use the mmap'd stack as the "original" stack - this is safe because:
    // 1. All OceanBase code will run on the new stack anyway
    // 2. After CALL_WITH_NEW_STACK returns, the stack attrs will be restored to this value
    oceanbase::common::set_stackattr(stack_addr, stack_size);
    
    int result;
    { SuppressLogStdoutScope _; result = CALL_WITH_NEW_STACK(do_seekdb_open_inner(db_dir, 0), stack_addr, stack_size); }
    
    // CRITICAL: After CALL_WITH_NEW_STACK returns, we're back on the original (Node.js) stack.
    // Instead of clearing the stack attribute cache (which would cause pthread_getattr_np
    // to return invalid values), we set a reasonable default stack attribute for subsequent
    // operations. This allows connect/execute/execute_update to run on the main stack without
    // needing CALL_WITH_NEW_STACK, aligning with Python embed behavior.
    // 
    // We calculate a reasonable stack address from the current stack pointer and use a
    // default stack size (8MB, Linux default).
    const size_t default_stack_size = 8ULL << 20;  // 8MB
    char dummy;
    uintptr_t cur_sp = (uintptr_t)&dummy;
    // Align stack address down to page boundary, assume we're near top of stack
    void* default_stack_addr = (void*)((cur_sp - default_stack_size + (1ULL << 20)) & ~((uintptr_t)0xFFF));
    oceanbase::common::set_stackattr(default_stack_addr, default_stack_size);
    
    seekdb_munmap_anonymous_stack(stack_addr, stack_size);
    
    return result;
}

int seekdb_open_with_service(const char* db_dir, int port) {
    if (!db_dir) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    std::lock_guard<std::mutex> lock(g_init_mutex);
    
    if (g_embedded_opened) {
        if (g_embedded_base_dir[0] != '\0') {
            char cwd_buf[PATH_MAX];
            if (SEEKDB_GETCWD(cwd_buf, sizeof(cwd_buf)) != nullptr) {
                ObSqlString req_abs;
                if (req_abs.assign(db_dir) == OB_SUCCESS && to_absolute_path(cwd_buf, req_abs) == OB_SUCCESS &&
                    same_embedded_path(req_abs.ptr(), g_embedded_base_dir)) {
                    return SEEKDB_SUCCESS;
                }
            }
        }
        return SEEKDB_SUCCESS;
    }
    
    // Use CALL_WITH_NEW_STACK to execute on a dedicated stack (aligned with Python embed)
    // This avoids issues with pthread_getattr_np returning invalid values in FFI environments
    // The dedicated stack has known size and address, so OceanBase's stack overflow checks work correctly
    const size_t stack_size = 1LL << 20;  // 1MB (same as Python embed)
    void* stack_addr = seekdb_mmap_anonymous_stack(stack_size);
#ifdef _WIN32
    if (stack_addr == nullptr) {
#else
    if (MAP_FAILED == stack_addr) {
#endif
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    // CRITICAL: Set a valid stack attribute before calling CALL_WITH_NEW_STACK
    // call_with_new_stack() internally calls get_stackattr() to save the original stack attrs,
    // but in FFI environments (Node.js/V8), pthread_getattr_np may return invalid values.
    // By pre-setting a valid stack attribute, get_stackattr() will use our cached values.
    // We use the mmap'd stack as the "original" stack - this is safe because:
    // 1. All OceanBase code will run on the new stack anyway
    // 2. After CALL_WITH_NEW_STACK returns, the stack attrs will be restored to this value
    oceanbase::common::set_stackattr(stack_addr, stack_size);
    
    int result;
    { SuppressLogStdoutScope _; result = CALL_WITH_NEW_STACK(do_seekdb_open_inner(db_dir, port), stack_addr, stack_size); }
    
    // CRITICAL: After CALL_WITH_NEW_STACK returns, we're back on the original (Node.js) stack.
    // Instead of clearing the stack attribute cache (which would cause pthread_getattr_np
    // to return invalid values), we set a reasonable default stack attribute for subsequent
    // operations. This allows connect/execute/execute_update to run on the main stack without
    // needing CALL_WITH_NEW_STACK, aligning with Python embed behavior.
    // 
    // We calculate a reasonable stack address from the current stack pointer and use a
    // default stack size (8MB, Linux default).
    const size_t default_stack_size = 8ULL << 20;  // 8MB
    char dummy;
    uintptr_t cur_sp = (uintptr_t)&dummy;
    // Align stack address down to page boundary, assume we're near top of stack
    void* default_stack_addr = (void*)((cur_sp - default_stack_size + (1ULL << 20)) & ~((uintptr_t)0xFFF));
    oceanbase::common::set_stackattr(default_stack_addr, default_stack_size);
    
    seekdb_munmap_anonymous_stack(stack_addr, stack_size);
    
    return result;
}

// Gracefully stop embedded observer background threads without calling destroy().
// destroy() tears down static singletons and can double-free at process exit.
static void embed_observer_shutdown()
{
    OBSERVER.embed_shutdown();
}

void seekdb_close(void) {
    std::lock_guard<std::mutex> lock(g_init_mutex);
    if (g_embedded_opened) {
        // Set closing flag to indicate we're in cleanup process
        // This allows the signal handler to recognize cleanup-related segfaults
        g_closing = true;

#ifndef _WIN32
        // Re-install our SIGSEGV, SIGABRT and SIGBUS handlers so they are active during atexit/static destructors.
        // Other runtimes (e.g. Rust, Node) may overwrite the handler; after seekdb_close()
        // the process often exits and C++ destructors can trigger segfaults, ob_abort() or bus errors in worker threads.
        struct sigaction sa;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;
        if (g_segv_handler_installed) {
            sa.sa_sigaction = segv_handler_during_close;
            (void)sigaction(SIGSEGV, &sa, &g_old_segv_handler);
        }
        if (g_sigabrt_handler_installed) {
            sa.sa_sigaction = sigabrt_handler_during_close;
            (void)sigaction(SIGABRT, &sa, &g_old_sigabrt_handler);
        }
        if (g_sigbus_handler_installed) {
            sa.sa_sigaction = sigbus_handler_during_close;
            (void)sigaction(SIGBUS, &sa, &g_old_sigbus_handler);
        }
#endif

        embed_observer_shutdown();

        // Skip observer.destroy(): static singleton ordering can segfault at exit.
        // PID file cleanup below; exit-probe scripts remain as a last-resort fallback.
        
        // Only clean up the PID file
        if (g_embedded_pid_locked) {
            SEEKDB_UNLINK(g_embedded_pid_file.ptr());
            g_embedded_pid_locked = false;
        }
        g_embedded_opened = false;
        g_embedded_base_dir[0] = '\0';
        
        // Note: We keep g_closing = true to allow signal handler to catch
        // segfaults during static destructors at program exit
        // The signal handler will exit gracefully if segfault occurs
    }
}

// =============================================================================
// DDL refresh visibility: so listCollections / SHOW TABLES see latest tables after DDL
// =============================================================================
// - refresh_session_schema_version: refresh_and_add_schema then set session last_schema_version
// - check_and_refresh_schema_for_embed: align with MySQL protocol (ObMPBase::check_and_refresh_schema):
//   only refresh when tenant local_version < session last_version; else skip to avoid heavy refresh on every read/write.
// - is_write_sql: DDL/DML (create/drop/alter/insert/update/delete/truncate) -> refresh after execute_read

static void refresh_session_schema_version(oceanbase::sql::ObSQLSessionInfo* session) {
    if (OB_ISNULL(session) || OB_ISNULL(GCTX.schema_service_)) return;
    (void)GCTX.schema_service_->refresh_and_add_schema(false);
    int64_t schema_version = OB_INVALID_VERSION;
    if (OB_SUCCESS != GCTX.schema_service_->get_runtime_refreshed_schema_version(schema_version)
        || OB_INVALID_VERSION == schema_version) {
        (void)oceanbase::sql::ObSQLUtils::update_session_last_schema_version(*GCTX.schema_service_, *session);
        return;
    }
    session->set_last_ddl_schema_version(schema_version);
}

// Align with MySQL protocol: ObMPBase::check_and_refresh_schema. Only refresh when server (local) is behind session (last).
static void check_and_refresh_schema_for_embed(oceanbase::sql::ObSQLSessionInfo* session) {
    if (OB_ISNULL(session) || OB_ISNULL(GCTX.schema_service_)) return;
    int64_t local_version = OB_INVALID_VERSION;
    int64_t last_version = OB_INVALID_VERSION;
    if (OB_SUCCESS != GCTX.schema_service_->get_runtime_refreshed_schema_version(local_version)) {
        refresh_session_schema_version(session);
        return;
    }
    last_version = session->get_last_ddl_schema_version();
    if (OB_INVALID_VERSION == last_version) {
        refresh_session_schema_version(session);
        return;
    }
    if (local_version >= last_version) {
        return;  // skip: server already has at least what session has
    }
    refresh_session_schema_version(session);
}

static bool is_write_sql(const char* sql) {
    if (!sql) return false;
    std::string s(sql);
    size_t start = s.find_first_not_of(" \t\n\r;");
    if (start == std::string::npos) return false;
    std::string lower = s.substr(start);
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("create ") == 0 || lower.find("drop ") == 0 || lower.find("alter ") == 0
        || lower.find("insert ") == 0 || lower.find("update ") == 0 || lower.find("delete ") == 0
        || lower.find("truncate ") == 0;
}

// Internal implementation of seekdb_connect, called on a dedicated stack
struct ConnectParams {
    SeekdbHandle* handle;
    const char* database;
    bool autocommit;
    int result;
};

static int do_seekdb_connect_inner(ConnectParams* params) {
    SeekdbHandle* handle = params->handle;
    const char* database = params->database;
    bool autocommit = params->autocommit;
    
    if (!GCTX.is_inited() || !GCTX.sql_proxy_ || !GCTX.session_mgr_ || !GCTX.schema_service_) {
        params->result = SEEKDB_ERROR_NOT_INITIALIZED;
        return OB_SUCCESS;
    }
    
    SeekdbConnection* conn = new (std::nothrow) SeekdbConnection();
    if (!conn) {
        params->result = SEEKDB_ERROR_MEMORY_ALLOC;
        return OB_SUCCESS;
    }
    
    int ret = OB_SUCCESS;
    sqlclient::ObISQLConnection* inner_conn = nullptr;
    uint32_t sid = ObSQLSessionInfo::INVALID_SESSID;
    ObSQLSessionInfo* session = nullptr;
    const schema::ObUserInfo* user_info = nullptr;
    schema::ObSchemaGetterGuard schema_guard;
    ObPrivSet db_priv_set = OB_PRIV_SET_EMPTY;
    const schema::ObDatabaseSchema* database_schema = nullptr;
    
    if (OB_FAIL(GCTX.session_mgr_->create_sessid(sid))) {
        set_error(conn, "Failed to create sess id");
        delete conn;
        params->result = SEEKDB_ERROR_CONNECTION_FAILED;
        return OB_SUCCESS;
    } else if (OB_FAIL(GCTX.session_mgr_->create_session(sid, session))) {
        session = nullptr;
        set_error(conn, "Failed to create session");
        delete conn;
        params->result = SEEKDB_ERROR_CONNECTION_FAILED;
        return OB_SUCCESS;
    } else if (FALSE_IT(ob_setup_tsi_warning_buffer(&session->get_warnings_buffer()))) {
    } else if (FALSE_IT(conn->embed_session = session)) {
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
        set_error(conn, "failed to get schema guard");
        delete conn;
        params->result = SEEKDB_ERROR_CONNECTION_FAILED;
        return OB_SUCCESS;
    } else if (OB_FAIL(schema_guard.get_user_info(OB_SYS_USER_ID, user_info))) {
        set_error(conn, "failed to get user info");
        delete conn;
        params->result = SEEKDB_ERROR_CONNECTION_FAILED;
        return OB_SUCCESS;
    } else if (OB_ISNULL(user_info)) {
        set_error(conn, "schema user info is null");
        delete conn;
        params->result = SEEKDB_ERROR_CONNECTION_FAILED;
        return OB_SUCCESS;
    } else if (OB_NOT_NULL(database) && STRLEN(database) > 0) {
        if (OB_FAIL(schema_guard.get_database_schema(ObString(database), database_schema))) {
            set_error(conn, "failed to get database");
            delete conn;
            params->result = SEEKDB_ERROR_CONNECTION_FAILED;
            return OB_SUCCESS;
        } else if (OB_ISNULL(database_schema)) {
            set_error(conn, "database is null");
            delete conn;
            params->result = SEEKDB_ERROR_CONNECTION_FAILED;
            return OB_SUCCESS;
        }
    }
    
    if (OB_SUCC(ret)) {
        if (OB_FAIL(session->load_default_sys_variable(false, true))) {
            set_error(conn, "load_default_sys_variable failed");
        } else if (OB_FAIL(session->load_default_configs_in_pc())) {
            set_error(conn, "load_default_configs_in_pc failed");
        } else if (OB_FAIL(session->load_all_sys_vars(schema_guard))) {
            set_error(conn, "load_all_sys_vars failed");
        } else {
            if (OB_NOT_NULL(database) && STRLEN(database) > 0) {
                if (OB_FAIL(session->set_default_database(database))) {
                    set_error(conn, "set_default_database failed");
                }
            }
            if (OB_SUCC(ret)) {
                session->set_user_session();
                if (OB_FAIL(session->set_autocommit(autocommit))) {
                    set_error(conn, "set_autocommit failed");
                } else if (OB_FAIL(session->set_user(user_info->get_user_name_str(), 
                                                      user_info->get_host_name_str(), 
                                                      user_info->get_user_id()))) {
                    set_error(conn, "set_user failed");
                } else if (OB_FAIL(session->set_real_client_ip_and_port("127.0.0.1", 0))) {
                    set_error(conn, "set_real_client_ip_and_port failed");
                } else {
                    session->set_priv_user_id(user_info->get_user_id());
                    session->set_user_priv_set(user_info->get_priv_set());
                    ObObj param_val;
                    param_val.set_int(60 * 1000 * 1000);
                    if (OB_FAIL(session->update_sys_variable(oceanbase::SYS_VAR_OB_QUERY_TIMEOUT, param_val))) {
                        // Non-critical, continue
                    }
                    if (OB_NOT_NULL(database) && STRLEN(database) > 0) {
                        if (OB_FAIL(schema_guard.get_db_priv_set(user_info->get_user_id(), 
                                                                  database, db_priv_set))) {
                            // Non-critical, continue
                        } else {
                            session->set_db_priv_set(db_priv_set);
                        }
                    }
                    // Set enable role array (aligned with Python embed)
                    session->get_enable_role_array().reuse();
                    for (int i = 0; OB_SUCC(ret) && i < user_info->get_role_id_array().count(); ++i) {
                        if (user_info->get_disable_option(user_info->get_role_id_option_array().at(i)) == 0) {
                            if (OB_FAIL(session->get_enable_role_array().push_back(user_info->get_role_id_array().at(i)))) {
                                // Non-critical, continue
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
    
    // Use OBSERVER macro directly like Python embed does
    if (OB_SUCC(ret)) {
        oceanbase::observer::ObInnerSQLConnectionPool *inner_sql_pool = get_embed_inner_sql_conn_pool();
        if (OB_ISNULL(inner_sql_pool)) {
            set_error(conn, "inner sql conn pool not ready");
            if (session) {
                GCTX.session_mgr_->revert_session(session);
            }
            delete conn;
            params->result = SEEKDB_ERROR_CONNECTION_FAILED;
            return OB_SUCCESS;
        } else if (OB_FAIL(inner_sql_pool->acquire(session, inner_conn))) {
            set_error(conn, "acquire conn failed");
            if (session) {
                GCTX.session_mgr_->revert_session(session);
            }
            delete conn;
            params->result = SEEKDB_ERROR_CONNECTION_FAILED;
            return OB_SUCCESS;
        } else {
            conn->embed_conn = static_cast<ObInnerSQLConnection*>(inner_conn);
            conn->initialized = true;
            *handle = static_cast<SeekdbHandle>(conn);
            // Align with MySQL protocol: no schema refresh on connect; first query will do check_and_refresh_schema_for_embed.
            // Reset warning buffer after connect (aligned with Python embed)
            ob_setup_tsi_warning_buffer(NULL);
            params->result = SEEKDB_SUCCESS;
            return OB_SUCCESS;
        }
    } else {
        if (session) {
            GCTX.session_mgr_->revert_session(session);
        }
        delete conn;
        params->result = SEEKDB_ERROR_CONNECTION_FAILED;
        return OB_SUCCESS;
    }
}

int seekdb_connect(SeekdbHandle* handle, const char* database, bool autocommit) {
    if (!handle || !database) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!g_embedded_opened) {
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Execute directly on main stack (aligned with Python embed)
    // Stack attributes were set to reasonable defaults in seekdb_open()
    ConnectParams params;
    params.handle = handle;
    params.database = database;
    params.autocommit = autocommit;
    params.result = SEEKDB_ERROR_CONNECTION_FAILED;
    
    do_seekdb_connect_inner(&params);
    
    // Explicit USE database so the physical connection is in the requested database (multi-connection
    // same-path: each connection must run USE to avoid wrong-database reads).
    if (params.result == SEEKDB_SUCCESS && database && strlen(database) > 0) {
        (void)seekdb_select_db(*handle, database);
    }
    return params.result;
}

void seekdb_connect_close(SeekdbHandle handle) {
    if (handle) {
        SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
        delete conn;
    }
}

// Internal implementation of seekdb_query/seekdb_real_query
struct ExecuteParams {
    SeekdbHandle handle;
    const char* sql;
    SeekdbResult* result;
    int ret_code;
};

// Helper function to infer column names from SQL statement
// Returns true if inference was successful, false otherwise
static bool infer_column_names_from_sql(
    const char* sql,
    int column_count,
    std::vector<std::string>& column_names
) {
    if (!sql || column_count <= 0) {
        return false;
    }
    
    std::string sql_str(sql);
    // Convert to lowercase for case-insensitive matching
    std::string sql_lower = sql_str;
    std::transform(sql_lower.begin(), sql_lower.end(), sql_lower.begin(), ::tolower);
    
    // Trim leading whitespace
    size_t start = sql_lower.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) {
        return false;
    }
    sql_lower = sql_lower.substr(start);
    
    // Handle SHOW TABLES
    if (sql_lower.find("show tables") == 0) {
        column_names.push_back("Tables_in_" + std::string("database")); // Will be replaced if we know the database
        return true;
    }
    
    // Handle SHOW CREATE TABLE
    if (sql_lower.find("show create table") == 0) {
        column_names.push_back("Table");
        column_names.push_back("Create Table");
        return column_count == 2;
    }
    
    // Handle DESCRIBE / DESC
    if (sql_lower.find("describe ") == 0 || sql_lower.find("desc ") == 0) {
        // DESCRIBE returns: Field, Type, Null, Key, Default, Extra
        column_names.push_back("Field");
        column_names.push_back("Type");
        column_names.push_back("Null");
        column_names.push_back("Key");
        column_names.push_back("Default");
        column_names.push_back("Extra");
        return column_count == 6;
    }
    
    // Handle SELECT statements
    if (sql_lower.find("select") == 0) {
        // Find SELECT ... FROM pattern
        size_t select_pos = sql_lower.find("select");
        size_t from_pos = sql_lower.find(" from ");
        
        if (from_pos == std::string::npos) {
            // No FROM clause, might be SELECT without FROM (MySQL allows this)
            from_pos = sql_lower.length();
        }
        
        if (select_pos != std::string::npos && from_pos > select_pos) {
            // Extract SELECT clause
            size_t select_start = select_pos + 6; // "select" length
            std::string select_clause = sql_str.substr(select_start, from_pos - select_start);
            
            // Trim whitespace
            size_t clause_start = select_clause.find_first_not_of(" \t\n\r");
            if (clause_start != std::string::npos) {
                select_clause = select_clause.substr(clause_start);
            }
            size_t clause_end = select_clause.find_last_not_of(" \t\n\r");
            if (clause_end != std::string::npos) {
                select_clause = select_clause.substr(0, clause_end + 1);
            }
            
            // Handle SELECT *
            if (select_clause == "*" || select_clause == " *") {
                return false; // Cannot infer column names from SELECT *
            }
            
            // Split by comma, handling nested parentheses and quotes
            std::vector<std::string> parts;
            int depth = 0;
            bool in_single_quote = false;
            bool in_double_quote = false;
            bool in_backtick = false;
            std::string current_part;
            
            for (size_t i = 0; i < select_clause.length(); i++) {
                char c = select_clause[i];
                
                if (c == '\'' && !in_double_quote && !in_backtick) {
                    in_single_quote = !in_single_quote;
                    current_part += c;
                } else if (c == '"' && !in_single_quote && !in_backtick) {
                    in_double_quote = !in_double_quote;
                    current_part += c;
                } else if (c == '`' && !in_single_quote && !in_double_quote) {
                    in_backtick = !in_backtick;
                    current_part += c;
                } else if (c == '(' && !in_single_quote && !in_double_quote && !in_backtick) {
                    depth++;
                    current_part += c;
                } else if (c == ')' && !in_single_quote && !in_double_quote && !in_backtick) {
                    depth--;
                    current_part += c;
                } else if (c == ',' && depth == 0 && !in_single_quote && !in_double_quote && !in_backtick) {
                    // Split point
                    if (!current_part.empty()) {
                        parts.push_back(current_part);
                        current_part.clear();
                    }
                } else {
                    current_part += c;
                }
            }
            
            if (!current_part.empty()) {
                parts.push_back(current_part);
            }
            
            // Extract column names from parts
            for (const std::string& part : parts) {
                std::string trimmed = part;
                // Trim whitespace
                size_t trim_start = trimmed.find_first_not_of(" \t\n\r");
                if (trim_start != std::string::npos) {
                    trimmed = trimmed.substr(trim_start);
                }
                size_t trim_end = trimmed.find_last_not_of(" \t\n\r");
                if (trim_end != std::string::npos) {
                    trimmed = trimmed.substr(0, trim_end + 1);
                }
                
                if (trimmed.empty()) {
                    continue;
                }
                
                // Look for AS alias
                std::string col_name;
                size_t as_pos = std::string::npos;
                
                // Case-insensitive search for AS
                std::string trimmed_lower = trimmed;
                std::transform(trimmed_lower.begin(), trimmed_lower.end(), trimmed_lower.begin(), ::tolower);
                
                // Try to find " AS " or " as "
                size_t as_pos1 = trimmed_lower.find(" as ");
                if (as_pos1 != std::string::npos) {
                    as_pos = as_pos1;
                }
                
                if (as_pos != std::string::npos) {
                    // Has AS alias
                    std::string alias = trimmed.substr(as_pos + 4);
                    // Trim alias
                    size_t alias_start = alias.find_first_not_of(" \t\n\r");
                    if (alias_start != std::string::npos) {
                        alias = alias.substr(alias_start);
                    }
                    size_t alias_end = alias.find_last_not_of(" \t\n\r");
                    if (alias_end != std::string::npos) {
                        alias = alias.substr(0, alias_end + 1);
                    }
                    
                    // Remove quotes if present
                    if ((alias.front() == '\'' && alias.back() == '\'') ||
                        (alias.front() == '"' && alias.back() == '"') ||
                        (alias.front() == '`' && alias.back() == '`')) {
                        alias = alias.substr(1, alias.length() - 2);
                    }
                    
                    col_name = alias;
                } else {
                    // No AS alias, try to extract column name
                    // Remove table prefix if present (e.g., "table.column")
                    size_t dot_pos = trimmed.find_last_of('.');
                    if (dot_pos != std::string::npos && dot_pos < trimmed.length() - 1) {
                        col_name = trimmed.substr(dot_pos + 1);
                    } else {
                        col_name = trimmed;
                    }
                    
                    // Remove quotes if present
                    if ((col_name.front() == '\'' && col_name.back() == '\'') ||
                        (col_name.front() == '"' && col_name.back() == '"') ||
                        (col_name.front() == '`' && col_name.back() == '`')) {
                        col_name = col_name.substr(1, col_name.length() - 2);
                    }
                    
                    // Trim whitespace
                    size_t name_start = col_name.find_first_not_of(" \t\n\r");
                    if (name_start != std::string::npos) {
                        col_name = col_name.substr(name_start);
                    }
                    size_t name_end = col_name.find_last_not_of(" \t\n\r");
                    if (name_end != std::string::npos) {
                        col_name = col_name.substr(0, name_end + 1);
                    }
                }
                
                if (!col_name.empty()) {
                    column_names.push_back(col_name);
                } else {
                    // Fallback: use a generic name
                    char gen_name[64];
                    snprintf(gen_name, sizeof(gen_name), "col_%zu", column_names.size());
                    column_names.push_back(std::string(gen_name));
                }
            }
            
            return column_names.size() == static_cast<size_t>(column_count);
        }
    }
    
    return false;
}

static int do_seekdb_execute_inner(ExecuteParams* params) {
    SeekdbHandle handle = params->handle;
    const char* sql = params->sql;
    SeekdbResult* result = params->result;
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        params->ret_code = SEEKDB_ERROR_INVALID_PARAM;
        return OB_SUCCESS;
    }
    
    SeekdbResultSet* result_set = new (std::nothrow) SeekdbResultSet();
    if (!result_set) {
        params->ret_code = SEEKDB_ERROR_MEMORY_ALLOC;
        return OB_SUCCESS;
    }
    
    // Set owner connection for proper cleanup
    result_set->owner_conn = conn;
    
    int ret = OB_SUCCESS;
    sqlclient::ObMySQLResult* sql_result = nullptr;
    
    // Embedded mode only
    if (!conn->embed_conn) {
        delete result_set;
        params->ret_code = SEEKDB_ERROR_INVALID_PARAM;
        return OB_SUCCESS;
    }
    
    ObString sql_string(sql);
    ObMemAttr mem_attr("FFIEmbedAlloc");
    
    // Initialize trace ID (aligned with Python embed)
    ObCurTraceId::init(GCTX.self_addr());
    
    // Setup warning buffer (aligned with Python embed)
    if (OB_NOT_NULL(conn->embed_session)) {
        ob_setup_tsi_warning_buffer(&conn->embed_session->get_warnings_buffer());
    }
    
    // Reset previous result if exists
    if (conn->embed_result) {
        conn->embed_result->close();
        conn->embed_result->~ReadResult();
        ob_free(conn->embed_result);
        conn->embed_result = nullptr;
    }
    
    // Allocate result
    conn->embed_result = static_cast<ObCommonSqlProxy::ReadResult*>(
        ob_malloc(sizeof(ObCommonSqlProxy::ReadResult), mem_attr));
    if (!conn->embed_result) {
        delete result_set;
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    new (conn->embed_result) ObCommonSqlProxy::ReadResult();
    
    // DDL / read-after-write visibility: align with MySQL protocol — check and refresh only when server is behind session.
    if (OB_NOT_NULL(conn->embed_session)) {
        check_and_refresh_schema_for_embed(conn->embed_session);
    }
    
    ret = conn->embed_conn->execute_read(sql_string, *conn->embed_result, true);
    
    // Reset warning buffer after execute (aligned with Python embed)
    if (OB_NOT_NULL(conn->embed_session)) {
        conn->embed_session->reset_warnings_buf();
    }
    ob_setup_tsi_warning_buffer(NULL);
    
    if (OB_SUCCESS != ret) {
        delete result_set;
        // Get detailed error message (aligned with Python embed)
        std::string errmsg;
        const oceanbase::common::ObWarningBuffer *wb = oceanbase::common::ob_get_tsi_warning_buffer();
        if (nullptr != wb) {
            if (wb->get_err_code() == ret ||
                (ret >= OB_MIN_RAISE_APPLICATION_ERROR && ret <= OB_MAX_RAISE_APPLICATION_ERROR)) {
                if (wb->get_err_msg() != nullptr && wb->get_err_msg()[0] != '\0') {
                    errmsg = std::string(wb->get_err_msg());
                }
            }
        }
        if (errmsg.empty()) {
            errmsg = std::string(ob_errpkt_strerror(ret));
        }
        if (errmsg.empty()) {
            errmsg = "Query execution failed";
        }
        // Append OB error code so logs always carry numeric ret even when the message is non-empty.
        char code_suffix[32];
        snprintf(code_suffix, sizeof(code_suffix), " (ret=%d)", ret);
        errmsg += code_suffix;
        set_error(conn, errmsg.c_str());
        if (conn->embed_result) {
            conn->embed_result->close();
            conn->embed_result->~ReadResult();
            ob_free(conn->embed_result);
            conn->embed_result = nullptr;
        }
        params->ret_code = SEEKDB_ERROR_QUERY_FAILED;
        return OB_SUCCESS;
    }

    // DDL / read-after-write visibility: after DDL/DML via execute_read, one refresh + sync session (align with MySQL DDL path).
    if (is_write_sql(params->sql) && OB_NOT_NULL(conn->embed_session) && OB_NOT_NULL(GCTX.schema_service_)) {
        refresh_session_schema_version(conn->embed_session);
        (void)oceanbase::sql::ObSQLUtils::update_session_last_schema_version(*GCTX.schema_service_, *conn->embed_session);
    }
    
    sql_result = conn->embed_result->get_result();
    
    if (!sql_result) {
        delete result_set;
        set_error(conn, "Result is null");
        params->ret_code = SEEKDB_ERROR_QUERY_FAILED;
        return OB_SUCCESS;
    }
    
    // Get column count
    int64_t column_count = sql_result->get_column_count();
    
    // Validate column_count to prevent vector::reserve errors
    // Note: 
    // - column_count can be 0 for DML statements (INSERT/UPDATE/DELETE), which is normal
    // - column_count can be -1 when result set is not opened or row_ is NULL (for DML statements), treat as 0
    if (column_count < -1 || column_count > INT32_MAX) {
        // Invalid column count (less than -1 or too large), return error
        delete result_set;
        set_error(conn, "Invalid column count");
        params->ret_code = SEEKDB_ERROR_QUERY_FAILED;
        return OB_SUCCESS;
    }
    
    // Treat -1 as 0 (for DML statements with no result set)
    if (column_count == -1) {
        column_count = 0;
    }
    
    result_set->column_count = static_cast<int32_t>(column_count);
    
    // Get column names - try multiple approaches
    // For DML statements, column_count may be 0, which is normal
    result_set->column_names.clear();
    if (column_count > 0) {
        result_set->column_names.reserve(static_cast<size_t>(column_count));
    }
    
    // Approach 1: Try to get column names from ObInnerSQLResult (aligned with MySQL mysql_fetch_fields)
    oceanbase::observer::ObInnerSQLResult* inner_result = 
        static_cast<oceanbase::observer::ObInnerSQLResult*>(sql_result);
    bool got_column_names = false;
    
    // Store field information for seekdb_fetch_fields() (aligned with MySQL)
    // For DML statements, fields may be empty, which is normal
    result_set->fields.clear();
    if (column_count > 0) {
        result_set->fields.reserve(static_cast<size_t>(column_count));
    }
    
    if (inner_result) {
        // Try to get field columns from result set
        const oceanbase::common::ColumnsFieldIArray* fields = nullptr;
        if (OB_NOT_NULL(inner_result->get_result_set())) {
            fields = inner_result->result_set().get_field_columns();
        }
        
        if (fields && fields->count() == column_count) {
            // Build field information structures (aligned with MySQL MYSQL_FIELD)
            for (int64_t i = 0; i < column_count; ++i) {
                const oceanbase::common::ObField& ob_field = fields->at(i);
                
                // Create SeekdbField structure (aligned with MYSQL_FIELD)
                SeekdbField field;
                memset(&field, 0, sizeof(SeekdbField));
                
                // Store strings in result_set for lifetime management
                std::string col_name;
                std::string org_col_name;
                std::string table_name;
                std::string org_table_name;
                std::string db_name;
                
                // Extract column name
                if (ob_field.cname_.ptr() && ob_field.cname_.length() > 0) {
                    col_name = std::string(ob_field.cname_.ptr(), ob_field.cname_.length());
                    field.name = col_name.c_str();
                    field.name_length = static_cast<uint32_t>(col_name.length());
                    got_column_names = true;
                } else {
                    got_column_names = false;
                    break;
                }
                
                // Extract original column name
                if (ob_field.org_cname_.ptr() && ob_field.org_cname_.length() > 0) {
                    org_col_name = std::string(ob_field.org_cname_.ptr(), ob_field.org_cname_.length());
                    field.org_name = org_col_name.c_str();
                    field.org_name_length = static_cast<uint32_t>(org_col_name.length());
                } else {
                    field.org_name = field.name;
                    field.org_name_length = field.name_length;
                }
                
                // Extract table name
                if (ob_field.tname_.ptr() && ob_field.tname_.length() > 0) {
                    table_name = std::string(ob_field.tname_.ptr(), ob_field.tname_.length());
                    field.table = table_name.c_str();
                    field.table_length = static_cast<uint32_t>(table_name.length());
                }
                
                // Extract original table name
                if (ob_field.org_tname_.ptr() && ob_field.org_tname_.length() > 0) {
                    org_table_name = std::string(ob_field.org_tname_.ptr(), ob_field.org_tname_.length());
                    field.org_table = org_table_name.c_str();
                    field.org_table_length = static_cast<uint32_t>(org_table_name.length());
                }
                
                // Extract database name
                if (ob_field.dname_.ptr() && ob_field.dname_.length() > 0) {
                    db_name = std::string(ob_field.dname_.ptr(), ob_field.dname_.length());
                    field.db = db_name.c_str();
                    field.db_length = static_cast<uint32_t>(db_name.length());
                }
                
                // Set catalog (MySQL default is "def")
                field.catalog = "def";
                field.catalog_length = 3;
                
                // Extract type information: map ObObjType to SeekdbFieldType (seekdb.h).
                // OceanBase ObObjType (e.g. ObFloatType=11, ObDoubleType=12) does not match SEEKDB_TYPE (5, 6);
                // bindings (e.g. js-bindings) rely on SEEKDB_TYPE_FLOAT/DOUBLE (5/6) to return number.
                if (!ob_field.type_.is_null()) {
                    oceanbase::common::ObObjType obj_type = ob_field.type_.get_type();
                    if (oceanbase::common::ob_is_valid_obj_type(obj_type)) {
                        if (obj_type == oceanbase::common::ObCollectionSQLType) {
                            field.type = static_cast<int32_t>(SEEKDB_TYPE_VECTOR);
                        } else if (obj_type == oceanbase::common::ObFloatType || obj_type == oceanbase::common::ObUFloatType) {
                            field.type = static_cast<int32_t>(SEEKDB_TYPE_FLOAT);
                        } else if (obj_type == oceanbase::common::ObDoubleType || obj_type == oceanbase::common::ObUDoubleType) {
                            field.type = static_cast<int32_t>(SEEKDB_TYPE_DOUBLE);
                        } else {
                            // Map other ObObjType to SEEKDB_TYPE where they align or have a clear mapping
                            switch (obj_type) {
                                case oceanbase::common::ObNullType:        field.type = static_cast<int32_t>(SEEKDB_TYPE_NULL); break;
                                case oceanbase::common::ObTinyIntType:      field.type = static_cast<int32_t>(SEEKDB_TYPE_TINY); break;
                                case oceanbase::common::ObSmallIntType:     field.type = static_cast<int32_t>(SEEKDB_TYPE_SHORT); break;
                                case oceanbase::common::ObMediumIntType:
                                case oceanbase::common::ObInt32Type:        field.type = static_cast<int32_t>(SEEKDB_TYPE_LONG); break;
                                case oceanbase::common::ObIntType:          field.type = static_cast<int32_t>(SEEKDB_TYPE_LONGLONG); break;
                                case oceanbase::common::ObUTinyIntType:     field.type = static_cast<int32_t>(SEEKDB_TYPE_TINY); break;
                                case oceanbase::common::ObUSmallIntType:     field.type = static_cast<int32_t>(SEEKDB_TYPE_SHORT); break;
                                case oceanbase::common::ObUMediumIntType:
                                case oceanbase::common::ObUInt32Type:
                                case oceanbase::common::ObUInt64Type:       field.type = static_cast<int32_t>(SEEKDB_TYPE_LONGLONG); break;
                                case oceanbase::common::ObDateTimeType:     field.type = static_cast<int32_t>(SEEKDB_TYPE_DATETIME); break;
                                case oceanbase::common::ObTimestampType:   field.type = static_cast<int32_t>(SEEKDB_TYPE_TIMESTAMP); break;
                                case oceanbase::common::ObDateType:         field.type = static_cast<int32_t>(SEEKDB_TYPE_DATE); break;
                                case oceanbase::common::ObTimeType:         field.type = static_cast<int32_t>(SEEKDB_TYPE_TIME); break;
                                case oceanbase::common::ObYearType:         field.type = static_cast<int32_t>(SEEKDB_TYPE_LONGLONG); break;
                                case oceanbase::common::ObVarcharType:
                                case oceanbase::common::ObCharType:
                                case oceanbase::common::ObHexStringType:
                                case oceanbase::common::ObNumberType:
                                case oceanbase::common::ObUNumberType:
                                case oceanbase::common::ObTinyTextType:
                                case oceanbase::common::ObTextType:
                                case oceanbase::common::ObMediumTextType:
                                case oceanbase::common::ObLongTextType:
                                case oceanbase::common::ObJsonType:
                                case oceanbase::common::ObDecimalIntType:   field.type = static_cast<int32_t>(SEEKDB_TYPE_STRING); break;
                                case oceanbase::common::ObBitType:
                                case oceanbase::common::ObEnumType:
                                case oceanbase::common::ObSetType:          field.type = static_cast<int32_t>(SEEKDB_TYPE_STRING); break;
                                case oceanbase::common::ObGeometryType:
                                case oceanbase::common::ObUserDefinedSQLType:
                                case oceanbase::common::ObMySQLDateType:
                                case oceanbase::common::ObMySQLDateTimeType:
                                case oceanbase::common::ObTimestampLTZType:
                                case oceanbase::common::ObTimestampNanoType:
                                default:                                    field.type = static_cast<int32_t>(SEEKDB_TYPE_STRING); break;
                            }
                        }
                    }
                }
                
                // Extract flags
                field.flags = ob_field.flags_;
                
                // Extract length
                field.length = static_cast<uint32_t>(ob_field.length_);
                
                // Extract charset
                field.charsetnr = ob_field.charsetnr_;
                
                // Extract decimals from accuracy
                // ObAccuracy doesn't have is_valid(), but we can check if scale is valid (>= 0)
                if (ob_field.accuracy_.get_scale() >= 0) {
                    field.decimals = static_cast<uint32_t>(ob_field.accuracy_.get_scale());
                }
                
                // Store strings in result_set for lifetime management
                result_set->field_strings.push_back({
                    col_name, org_col_name, table_name, org_table_name, db_name
                });
                
                // Update field pointers to point to stored strings
                const auto& stored = result_set->field_strings.back();
                field.name = stored.col_name.c_str();
                field.org_name = stored.org_col_name.empty() ? field.name : stored.org_col_name.c_str();
                field.table = stored.table_name.empty() ? nullptr : stored.table_name.c_str();
                field.org_table = stored.org_table_name.empty() ? nullptr : stored.org_table_name.c_str();
                field.db = stored.db_name.empty() ? nullptr : stored.db_name.c_str();
                
                // Store field in result_set
                result_set->fields.push_back(field);
                result_set->column_names.push_back(col_name);
            }
        }
    }
    
    // Approach 2: If we didn't get column names, try SQL parsing inference
    if (!got_column_names && column_count > 0) {
        // Store SQL for inference (we'll use it if needed)
        std::string sql_for_inference = sql;
        
        // Try to infer column names from SQL
        std::vector<std::string> inferred_names;
        if (infer_column_names_from_sql(sql_for_inference.c_str(), column_count, inferred_names)) {
            if (inferred_names.size() == static_cast<size_t>(column_count)) {
                result_set->column_names = inferred_names;
                got_column_names = true;
            }
        }
    }
    
    // Approach 3: Fallback to default names (col_0, col_1, etc.)
    if (!got_column_names) {
        for (int64_t i = 0; i < column_count; ++i) {
            char col_name_buf[64];
            snprintf(col_name_buf, sizeof(col_name_buf), "col_%ld", i);
            result_set->column_names.push_back(std::string(col_name_buf));
        }
    }
    
    // Fetch all rows (embed mode: modules are ready after observer start)
    int64_t row_count = 0;
    {
    while (OB_SUCCESS == sql_result->next()) {
        std::vector<std::string> row;
        std::vector<bool> row_null;
        oceanbase::common::ObArenaAllocator row_lob_allocator(ObModIds::OB_MODULE_PAGE_ALLOCATOR);
        for (int64_t i = 0; i < column_count; ++i) {
            ObObj obj;
            if (OB_SUCCESS == sql_result->get_obj(i, obj)) {
                oceanbase::common::ObObjType col_type = oceanbase::common::ObNullType;
                if (static_cast<size_t>(i) < result_set->fields.size()) {
                    col_type = static_cast<oceanbase::common::ObObjType>(result_set->fields[i].type);
                }
                if (obj.is_null()) {
                    row.push_back("");
                    row_null.push_back(true);
                } else {
                    // Larger buffer for JSON/metadata (print_sql_literal may produce long escaped string)
                    char buf[32768];
                    int64_t pos = 0;
                    oceanbase::common::ObObjType obj_type = obj.get_type();
                    
                    // Get raw value based on type (without SQL literal quotes or JSON format)
                    if (ob_is_integer_type(obj_type) || ob_is_enumset_tc(obj_type)) {
                        // Integer types: get value based on specific type using type-checked getters
                        int64_t int_val = 0;
                        int ret = OB_OBJ_TYPE_ERROR;
                        
                        // Try signed integer types first
                        if (obj_type == ObTinyIntType) {
                            int8_t val = 0;
                            ret = obj.get_tinyint(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObSmallIntType) {
                            int16_t val = 0;
                            ret = obj.get_smallint(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObMediumIntType) {
                            int32_t val = 0;
                            ret = obj.get_mediumint(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObInt32Type) {
                            int32_t val = 0;
                            ret = obj.get_int32(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObIntType) {
                            ret = obj.get_int(int_val);
                        }
                        // Try unsigned integer types
                        else if (obj_type == ObUTinyIntType) {
                            uint8_t val = 0;
                            ret = obj.get_utinyint(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObUSmallIntType) {
                            uint16_t val = 0;
                            ret = obj.get_usmallint(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObUMediumIntType) {
                            uint32_t val = 0;
                            ret = obj.get_umediumint(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObUInt32Type) {
                            uint32_t val = 0;
                            ret = obj.get_uint32(val);
                            int_val = static_cast<int64_t>(val);
                        } else if (obj_type == ObUInt64Type) {
                            uint64_t val = 0;
                            ret = obj.get_uint64(val);
                            int_val = static_cast<int64_t>(val);
                        }
                        
                        if (OB_SUCCESS == ret) {
                            pos = snprintf(buf, sizeof(buf), "%ld", int_val);
                            if (pos > 0 && pos < static_cast<int64_t>(sizeof(buf))) {
                                row.push_back(std::string(buf, pos));
                            } else {
                                row.push_back("");
                            }
                            row_null.push_back(false);
                        } else {
                            // Fallback: use print_sql_literal and remove quotes
                            pos = 0;
                            if (OB_SUCCESS == obj.print_sql_literal(buf, sizeof(buf), pos)) {
                                std::string sql_literal(buf, pos);
                                // Remove surrounding quotes if present
                                if (sql_literal.length() >= 2 && 
                                    sql_literal.front() == '\'' && sql_literal.back() == '\'') {
                                    sql_literal = sql_literal.substr(1, sql_literal.length() - 2);
                                    size_t quote_pos = 0;
                                    while ((quote_pos = sql_literal.find("''", quote_pos)) != std::string::npos) {
                                        sql_literal.replace(quote_pos, 2, "'");
                                        quote_pos += 1;
                                    }
                                }
                                row.push_back(sql_literal);
                            } else {
                                row.push_back("");
                            }
                            row_null.push_back(false);
                        }
                    } else if (ob_is_json_tc(obj_type)) {
                        // JSON type: get_string() returns binary JSON (JSON_BIN). Always use print_sql_literal
                        // to get text JSON so JS JSON.parse() works (handles special chars in metadata).
                        pos = 0;
                        if (OB_SUCCESS == obj.print_sql_literal(buf, sizeof(buf), pos) && pos > 0) {
                            std::string sql_literal(buf, static_cast<size_t>(pos));
                            if (sql_literal.length() >= 2 && sql_literal.front() == '\'' && sql_literal.back() == '\'') {
                                sql_literal = sql_literal.substr(1, sql_literal.length() - 2);
                                for (size_t q = 0; (q = sql_literal.find("''", q)) != std::string::npos; q += 1)
                                    sql_literal.replace(q, 2, "'");
                            }
                            row.push_back(sql_literal);
                        } else {
                            row.push_back("");
                        }
                        row_null.push_back(false);
                    } else if (ob_is_string_type(obj_type) || ob_is_text_tc(obj_type)) {
                        std::string cell_str;
                        seekdb_materialize_string_cell(obj, obj_type, row_lob_allocator, cell_str);
                        row.push_back(std::move(cell_str));
                        row_null.push_back(false);
                    } else if (ob_is_float_tc(obj_type)) {
                        // Float types
                        float float_val = 0;
                        if (OB_SUCCESS == obj.get_float(float_val)) {
                            pos = snprintf(buf, sizeof(buf), "%.6g", float_val);
                            if (pos > 0 && pos < static_cast<int64_t>(sizeof(buf))) {
                                row.push_back(std::string(buf, pos));
                            } else {
                                row.push_back("");
                            }
                        } else {
                            row.push_back("");
                        }
                        row_null.push_back(false);
                    } else if (ob_is_double_tc(obj_type)) {
                        // Double types
                        double double_val = 0;
                        if (OB_SUCCESS == obj.get_double(double_val)) {
                            pos = snprintf(buf, sizeof(buf), "%.15g", double_val);
                            if (pos > 0 && pos < static_cast<int64_t>(sizeof(buf))) {
                                row.push_back(std::string(buf, pos));
                            } else {
                                row.push_back("");
                            }
                        } else {
                            row.push_back("");
                        }
                        row_null.push_back(false);
                    } else if (obj_type == oceanbase::common::ObCollectionSQLType) {
                        // VECTOR: engine returns raw float32 binary; convert to JSON string "[v1, v2, ...]" without rounding.
                        // Directly format each float so e.g. "[1.1, 2.2, 3.3]".
                        ObString str_val;
                        if (OB_SUCCESS == obj.get_string(str_val)) {
                            if (str_val.length() > 0 && str_val.ptr()) {
                                std::string json_vec;
                                if (vector_binary_to_json(str_val.ptr(), str_val.length(), json_vec)) {
                                    row.push_back(std::move(json_vec));
                                } else {
                                    row.push_back(std::string(str_val.ptr(), str_val.length()));
                                }
                            } else {
                                row.push_back("");
                            }
                        } else {
                            row.push_back("");
                        }
                        row_null.push_back(false);
                    } else {
                        // For other types, use print_sql_literal and remove quotes if present
                        if (OB_SUCCESS == obj.print_sql_literal(buf, sizeof(buf), pos)) {
                            std::string sql_literal(buf, pos);
                            // Remove surrounding quotes if present (for string literals)
                            if (sql_literal.length() >= 2 && 
                                sql_literal.front() == '\'' && sql_literal.back() == '\'') {
                                sql_literal = sql_literal.substr(1, sql_literal.length() - 2);
                                // Unescape single quotes ('' -> ')
                                size_t quote_pos = 0;
                                while ((quote_pos = sql_literal.find("''", quote_pos)) != std::string::npos) {
                                    sql_literal.replace(quote_pos, 2, "'");
                                    quote_pos += 1;
                                }
                            }
                            row.push_back(sql_literal);
                        } else {
                            row.push_back("");
                        }
                        row_null.push_back(false);
                    }
                }
            } else {
                row.push_back("");
                row_null.push_back(false);
            }
        }
        result_set->rows.push_back(row);
        result_set->row_nulls.push_back(row_null);
        row_count++;
    }
    result_set->row_count = row_count;
    }
    
    // Update affected rows from result set for DML statements (INSERT/UPDATE/DELETE)
    // This allows seekdb_affected_rows() to return correct value even when using seekdb_query()
    // Aligned with Python embed implementation
    oceanbase::observer::ObInnerSQLResult* inner_result_dml = 
        static_cast<oceanbase::observer::ObInnerSQLResult*>(sql_result);
    if (inner_result_dml) {
        oceanbase::sql::stmt::StmtType stmt_type = inner_result_dml->result_set().get_stmt_type();
        if (stmt_type == oceanbase::sql::stmt::T_SELECT) {
            // For SELECT, affected_rows is not meaningful, keep previous value
        } else {
            // For DML statements (INSERT/UPDATE/DELETE), get affected rows from result set
            int64_t affected_rows = inner_result_dml->result_set().get_affected_rows();
            if (affected_rows >= 0) {
                conn->last_affected_rows = static_cast<unsigned long long>(affected_rows);
            }
        }
    }
    
        // Store result in connection for mysql_store_result() compatibility
        // Note: conn is already defined at the beginning of this function
        if (conn) {
            // Free previous result set if exists and still owned by connection
            // Note: If last_result_set is nullptr, it means it was transferred to user
            // via seekdb_store_result(), so we don't need to free it
            if (conn->last_result_set) {
                // Check if already freed to prevent double free
                if (!conn->last_result_set->freed) {
                    // Mark as freed before deleting to prevent double free
                    conn->last_result_set->freed = true;
                    delete conn->last_result_set;
                }
                // Clear the reference
                conn->last_result_set = nullptr;
            }
            result_set->owner_conn = conn;
            conn->last_result_set = result_set;
        }
    
    *result = static_cast<SeekdbResult>(result_set);
    params->ret_code = SEEKDB_SUCCESS;
    return OB_SUCCESS;
}

int seekdb_query(SeekdbHandle handle, const char* query, SeekdbResult* result) {
    if (!handle || !query || !result) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Use real_query with strlen
    unsigned long length = static_cast<unsigned long>(strlen(query));
    return seekdb_real_query(handle, query, length, result);
}

int seekdb_real_query(SeekdbHandle handle, const char* stmt_str, unsigned long length, SeekdbResult* result) {
    if (!handle || !stmt_str || !result || length == 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Create a null-terminated string for ObString
    // Note: ObString can work with non-null-terminated strings, but we need to be careful
    std::string sql_str(stmt_str, length);
    
    // Execute directly on main stack (aligned with Python embed)
    // Stack attributes were set to reasonable defaults in seekdb_open()
    ExecuteParams params;
    params.handle = handle;
    params.sql = sql_str.c_str();
    params.result = result;
    params.ret_code = SEEKDB_ERROR_QUERY_FAILED;
    
    do_seekdb_execute_inner(&params);
    
    return params.ret_code;
}


// Helper function to convert SeekdbBind to parameter value string (for fallback)
// This is used when we need to build SQL string (not recommended, but provided for compatibility)
static std::string bind_to_string_value(SeekdbHandle handle, const SeekdbBind& bind) {
    if (bind.is_null && *bind.is_null) {
        return "NULL";
    }
    
    switch (bind.buffer_type) {
        case SEEKDB_TYPE_TINY:
            if (bind.buffer) {
                int8_t val = *static_cast<int8_t*>(bind.buffer);
                return std::to_string(val);
            }
            break;
        case SEEKDB_TYPE_SHORT:
            if (bind.buffer) {
                int16_t val = *static_cast<int16_t*>(bind.buffer);
                return std::to_string(val);
            }
            break;
        case SEEKDB_TYPE_LONG:
            if (bind.buffer) {
                int32_t val = *static_cast<int32_t*>(bind.buffer);
                return std::to_string(val);
            }
            break;
        case SEEKDB_TYPE_LONGLONG:
            if (bind.buffer) {
                int64_t val = *static_cast<int64_t*>(bind.buffer);
                return std::to_string(val);
            }
            break;
        case SEEKDB_TYPE_FLOAT:
            if (bind.buffer) {
                float val = *static_cast<float*>(bind.buffer);
                return std::to_string(val);
            }
            break;
        case SEEKDB_TYPE_DOUBLE:
            if (bind.buffer) {
                double val = *static_cast<double*>(bind.buffer);
                return std::to_string(val);
            }
            break;
        case SEEKDB_TYPE_STRING:
            if (bind.buffer && bind.length && *bind.length > 0) {
                std::string str_val(static_cast<const char*>(bind.buffer), *bind.length);
                // Escape string
                size_t escaped_len = str_val.length() * 2 + 1;
                std::vector<char> escaped_buf(escaped_len);
                
                unsigned long escaped_length = seekdb_real_escape_string(
                    handle,
                    escaped_buf.data(),
                    static_cast<unsigned long>(escaped_len),
                    str_val.c_str(),
                    static_cast<unsigned long>(str_val.length())
                );
                
                if (escaped_length != static_cast<unsigned long>(-1)) {
                    return "'" + std::string(escaped_buf.data(), escaped_length) + "'";
                }
            }
            break;
        case SEEKDB_TYPE_BLOB:
            if (bind.buffer && bind.length && *bind.length > 0) {
                size_t hex_len = *bind.length * 2 + 1;
                std::vector<char> hex_buf(hex_len);
                
                unsigned long hex_length = seekdb_hex_string(
                    hex_buf.data(),
                    static_cast<unsigned long>(hex_len),
                    static_cast<const char*>(bind.buffer),
                    *bind.length
                );
                
                if (hex_length != static_cast<unsigned long>(-1)) {
                    return "0x" + std::string(hex_buf.data(), hex_length);
                }
            }
            break;
        case SEEKDB_TYPE_VARBINARY_ID:
            if (bind.buffer && bind.length) {
                size_t data_len = *bind.length;
                size_t copy_len = (data_len > VARBINARY_ID_LENGTH) ? VARBINARY_ID_LENGTH : data_len;
                std::vector<char> padded(VARBINARY_ID_LENGTH, 0);
                if (copy_len > 0) {
                    memcpy(padded.data(), bind.buffer, copy_len);
                }
                size_t hex_len = VARBINARY_ID_LENGTH * 2 + 1;
                std::vector<char> hex_buf(hex_len);
                unsigned long hex_length = seekdb_hex_string(
                    hex_buf.data(),
                    static_cast<unsigned long>(hex_len),
                    padded.data(),
                    VARBINARY_ID_LENGTH
                );
                if (hex_length != static_cast<unsigned long>(-1)) {
                    return "0x" + std::string(hex_buf.data(), hex_length);
                }
            }
            break;
        default:
            break;
    }
    
    return "NULL";
}

int seekdb_query_with_params(
    SeekdbHandle handle,
    const char* query,
    SeekdbResult* result,
    SeekdbBind* bind,
    unsigned int param_count
) {
    if (!handle || !query || !result) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    unsigned long length = static_cast<unsigned long>(strlen(query));
    return seekdb_real_query_with_params(handle, query, length, result, bind, param_count);
}

// Helper function to get column types from INSERT statement using Schema Service API
// Returns true if column types were successfully retrieved
// Uses ObParser to parse SQL instead of manual string parsing
static bool get_insert_column_types(
    SeekdbConnection* conn,
    const std::string& sql,
    std::vector<oceanbase::common::ObObjType>& param_types) {
    
    if (!conn || !conn->embed_session || !conn->initialized) {
        return false;
    }
    
    // Use ObParser to parse SQL instead of manual string parsing
    // This is more reliable and handles complex SQL statements correctly
    ObString sql_str;
    sql_str.assign_ptr(sql.c_str(), static_cast<int32_t>(sql.length()));
    ObArenaAllocator allocator(ObModIds::OB_SQL_PARSER);
    ObParser parser(allocator, conn->embed_session->get_sql_mode(), 
                    conn->embed_session->get_charsets4parser());
    ParseResult parse_result;
    ParseMode parse_mode = STD_MODE;
    
    int parse_ret = parser.parse(sql_str, parse_result, parse_mode);
    if (parse_ret != OB_SUCCESS || parse_result.result_tree_ == nullptr) {
        // Parsing failed, cannot extract table/column information
        return false;
    }
    
    // Check if it's an INSERT statement
    const ParseNode* root = parse_result.result_tree_;
    if (root->type_ != T_INSERT) {
        return false;
    }
    
    // Extract table name and column names from parse tree
    std::string table_name;
    std::vector<std::string> column_names;
    
    // T_INSERT structure: children[0] = insert_into (T_INSERT_INTO)
    if (root->num_child_ > 0 && root->children_[0] != nullptr) {
        const ParseNode* insert_into = root->children_[0];
        // T_INSERT_INTO structure: children[0] = table_node, children[1] = column_list (optional)
        if (insert_into->num_child_ > 0 && insert_into->children_[0] != nullptr) {
            const ParseNode* table_node = insert_into->children_[0];
            // Extract table name from table_node
            if (table_node->str_value_ != nullptr && table_node->str_len_ > 0) {
                table_name = std::string(table_node->str_value_, table_node->str_len_);
            }
        }
        
        // Extract column list if specified
        if (insert_into->num_child_ > 1 && insert_into->children_[1] != nullptr) {
            const ParseNode* column_list = insert_into->children_[1];
            // Column list is typically T_COLUMN_LIST or similar
            if (column_list->num_child_ > 0) {
                for (int32_t i = 0; i < column_list->num_child_; i++) {
                    const ParseNode* col_node = column_list->children_[i];
                    if (col_node != nullptr && col_node->str_value_ != nullptr && col_node->str_len_ > 0) {
                        std::string col_name(col_node->str_value_, col_node->str_len_);
                        // Remove backticks if present
                        if (col_name.length() >= 2 && col_name[0] == '`' && col_name[col_name.length()-1] == '`') {
                            col_name = col_name.substr(1, col_name.length() - 2);
                        }
                        column_names.push_back(col_name);
                    }
                }
            }
        }
    }
    
    if (table_name.empty()) {
        return false;
    }
    
    // Get column types using Schema Service API (more efficient than executing SQL query)
    // This approach directly accesses table schema without executing any SQL statements
    // which is more efficient and avoids potential thread safety issues
    
    // Get database name from session
    ObString database_name = conn->embed_session->get_database_name();
    if (database_name.empty()) {
        // Database name is required for Schema Service API
        return false;
    }
    
    // Get schema guard from schema service
    schema::ObSchemaGetterGuard schema_guard;
    int schema_ret = GCTX.schema_service_->get_runtime_schema_guard(schema_guard);
    if (schema_ret != OB_SUCCESS) {
        // Schema service is required
        return false;
    }
    
    // Get table schema by database name and table name
    const schema::ObTableSchema* table_schema = nullptr;
    ObString table_name_str;
    table_name_str.assign_ptr(table_name.c_str(), static_cast<int32_t>(table_name.length()));
    schema_ret = schema_guard.get_table_schema(database_name, table_name_str, false, table_schema);
    if (schema_ret != OB_SUCCESS || table_schema == nullptr) {
        // Table schema is required
        return false;
    }
    
    // Extract column types from table schema
    param_types.clear();
    
    if (!column_names.empty()) {
        // If column list is specified, get types for those specific columns in order
        param_types.reserve(column_names.size());
        for (size_t i = 0; i < column_names.size(); i++) {
            // Convert std::string to ObString for get_column_schema()
            ObString column_name_str;
            column_name_str.assign_ptr(column_names[i].c_str(), static_cast<int32_t>(column_names[i].length()));
            const schema::ObColumnSchemaV2* column_schema = table_schema->get_column_schema(column_name_str);
            if (column_schema != nullptr) {
                oceanbase::common::ObObjType obj_type = column_schema->get_data_type();
                param_types.push_back(obj_type);
            } else {
                // Column not found in schema
                return false;
            }
        }
    } else {
        // If column list is not specified, get all user columns in table order
        // Note: This may not match the parameter order, so it's better to specify column list
        const schema::ObColumnSchemaV2* column_schema = nullptr;
        int64_t column_count = table_schema->get_column_count();
        param_types.reserve(static_cast<size_t>(column_count));
        
        for (int64_t i = 0; i < column_count; i++) {
            column_schema = table_schema->get_column_schema_by_idx(i);
            if (column_schema != nullptr && !column_schema->is_hidden()) {
                oceanbase::common::ObObjType obj_type = column_schema->get_data_type();
                param_types.push_back(obj_type);
            }
        }
    }
    
    return param_types.size() > 0;
}

int seekdb_real_query_with_params(
    SeekdbHandle handle,
    const char* stmt_str,
    unsigned long length,
    SeekdbResult* result,
    SeekdbBind* bind,
    unsigned int param_count
) {
    if (!handle || !stmt_str || !result || length == 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Auto-detect VECTOR type based on column type information from table schema
    // Use column type information from prepared statement (obtained via table schema query)
    // instead of hard-parsing parameter values
    if (param_count > 0 && bind) {
        SeekdbStmt stmt = seekdb_stmt_init(handle);
        if (stmt) {
            // Prepare statement to get column type information
            int prep_ret = seekdb_stmt_prepare(stmt, stmt_str, length);
            if (prep_ret == SEEKDB_SUCCESS) {
                SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
                
                // Use column type information from table schema
                // For multi-row INSERT: VALUES (?, ?, ?), (?, ?, ?), ...
                // Parameters repeat in pattern: param[i] corresponds to column[i % column_count]
                // Example: INSERT INTO table (col1, col2, col3, col4) VALUES (?, ?, ?, ?), (?, ?, ?, ?)
                //   param[0,4,8] -> col1, param[1,5,9] -> col2, param[2,6,10] -> col3, param[3,7,11] -> col4
                size_t column_count = stmt_data->param_column_types.size();
                if (column_count > 0) {
                    for (unsigned int i = 0; i < param_count; i++) {
                        // Only auto-detect if type is STRING or not explicitly set
                        if (bind[i].buffer_type == SEEKDB_TYPE_STRING || bind[i].buffer_type == SEEKDB_TYPE_NULL) {
                            // Map parameter index to column index (for multi-row inserts)
                            size_t col_idx = i % column_count;
                            if (col_idx < stmt_data->param_column_types.size()) {
                                oceanbase::common::ObObjType col_type = stmt_data->param_column_types[col_idx];
                                
                                // Check if column type is VECTOR
                                // VECTOR type in OceanBase is represented as ObCollectionSQLType (40)
                                // We need to check if it's a collection type, which includes VECTOR
                                // Note: This may also match other collection types (varray, nested table),
                                // but for INSERT statements, VECTOR is the most common collection type
                                if (col_type == oceanbase::common::ObCollectionSQLType) {
                                    // Auto-detect as VECTOR type based on column schema
                                    bind[i].buffer_type = SEEKDB_TYPE_VECTOR;
                                }
                            }
                        }
                    }
                }
            }
            // Note: We'll recreate the statement below, so we can close this one
            seekdb_stmt_close(stmt);
        }
    }
    
    // Use prepared statement internally (aligned with MySQL C API approach)
    // This is more secure and efficient than string substitution
    SeekdbStmt stmt = seekdb_stmt_init(handle);
    if (!stmt) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    // Prepare statement
    int ret = seekdb_stmt_prepare(stmt, stmt_str, length);
    if (ret != SEEKDB_SUCCESS) {
        seekdb_stmt_close(stmt);
        return ret;
    }
    
    // Bind parameters
    if (param_count > 0 && bind) {
        ret = seekdb_stmt_bind_param(stmt, bind);
        if (ret != SEEKDB_SUCCESS) {
            seekdb_stmt_close(stmt);
            return ret;
        }
    }
    
    // Execute statement
    // Note: seekdb_stmt_execute() builds SQL with parameter substitution, calls seekdb_query(),
    // then transfers result from conn->last_result_set to stmt_data->result_set
    ret = seekdb_stmt_execute(stmt);
    
    // Get result from statement (seekdb_stmt_execute stores it in stmt_data->result_set, not conn)
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    if (ret == SEEKDB_SUCCESS && stmt_data && stmt_data->result_set) {
        *result = static_cast<SeekdbResult>(stmt_data->result_set);
        stmt_data->result_set = nullptr;  // Transfer ownership to caller; stmt_close must not free it
    }
    
    // Close statement
    seekdb_stmt_close(stmt);
    
    return ret;
}

SeekdbResult seekdb_store_result(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    // Return the last result set stored in the connection
    // This is set by seekdb_real_query() or seekdb_query()
    // Transfer ownership to caller by removing reference from connection
    // This prevents the result set from being deleted by subsequent queries
    SeekdbResult result = static_cast<SeekdbResult>(conn->last_result_set);
    if (result) {
        conn->last_result_set = nullptr;  // Transfer ownership to caller
    }
    return result;
}

SeekdbResult seekdb_use_result(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !conn->embed_result) {
        return nullptr;
    }
    
    // Create a streaming result set
    if (!conn->use_result_set) {
        SeekdbResultSet* result_set = new (std::nothrow) SeekdbResultSet();
        if (!result_set) {
            return nullptr;
        }
        result_set->owner_conn = conn;
        
        sqlclient::ObMySQLResult* sql_result = conn->embed_result->get_result();
        if (!sql_result) {
            delete result_set;
            return nullptr;
        }
        
        // Get column count
        int64_t column_count = sql_result->get_column_count();
        result_set->column_count = static_cast<int32_t>(column_count);
        result_set->use_result_mode = true;  // Mark as streaming mode
        
        // Get column names (similar to store_result)
        for (int64_t i = 0; i < column_count; ++i) {
            char col_name_buf[64];
            snprintf(col_name_buf, sizeof(col_name_buf), "col_%ld", i);
            result_set->column_names.push_back(std::string(col_name_buf));
        }
        
        // In streaming mode, we don't pre-fetch rows
        // Rows will be fetched on-demand in seekdb_fetch_row()
        result_set->row_count = -1;  // Unknown row count in streaming mode
        
        result_set->owner_conn = conn;
        conn->use_result_set = result_set;
    }
    
    return static_cast<SeekdbResult>(conn->use_result_set);
}

my_ulonglong seekdb_num_rows(SeekdbResult result) {
    if (!result) {
        return static_cast<my_ulonglong>(-1);
    }
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    return static_cast<my_ulonglong>(rs->row_count);
}

unsigned int seekdb_num_fields(SeekdbResult result) {
    if (!result) {
        return static_cast<unsigned int>(-1);
    }
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    return static_cast<unsigned int>(rs->column_count);
}

unsigned int seekdb_field_count(SeekdbHandle handle) {
    if (!handle) {
        return 0;
    }
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return 0;
    }
    
    // Get field count from last result set
    if (conn->last_result_set) {
        return static_cast<unsigned int>(conn->last_result_set->column_count);
    }
    
    return 0;
}

size_t seekdb_result_column_name_len(SeekdbResult result, int32_t column_index) {
    if (!result || column_index < 0) {
        return static_cast<size_t>(-1);
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    if (column_index >= static_cast<int32_t>(rs->column_names.size())) {
        return static_cast<size_t>(-1);
    }
    
    return rs->column_names[column_index].length();
}

int seekdb_result_column_name(SeekdbResult result, int32_t column_index, char* name, size_t name_len) {
    if (!result || !name || name_len == 0 || column_index < 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    if (column_index >= static_cast<int32_t>(rs->column_names.size())) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    const std::string& col_name = rs->column_names[column_index];
    size_t copy_len = std::min(col_name.length(), name_len - 1);
    strncpy(name, col_name.c_str(), copy_len);
    name[copy_len] = '\0';
    
    return SEEKDB_SUCCESS;
}

SeekdbRow seekdb_fetch_row(SeekdbResult result) {
    if (!result) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    rs->current_row++;
    
    if (rs->current_row >= rs->row_count) {
        return nullptr; // No more rows (MySQL-compatible: returns NULL)
    }
    
    // Align with MySQL: row is borrowed, valid until next seekdb_fetch_row() or seekdb_result_free().
    // Reuse a single current_row_data instead of allocating per row.
    SeekdbRowData* row_data = rs->current_row_data;
    if (!row_data) {
        row_data = new (std::nothrow) SeekdbRowData(rs, rs->current_row);
        if (!row_data) {
            return nullptr;
        }
        rs->current_row_data = row_data;
    } else {
        row_data->row_index = rs->current_row;
    }
    
    // Pre-compute lengths for seekdb_fetch_lengths(); NULL -> 0, non-NULL -> actual byte length
    rs->current_lengths.clear();
    rs->current_lengths.resize(rs->column_count, 0);
    if (rs->current_row < static_cast<int64_t>(rs->rows.size())) {
        const std::vector<std::string>& row_vec = rs->rows[rs->current_row];
        for (int32_t i = 0; i < rs->column_count && i < static_cast<int32_t>(row_vec.size()); i++) {
            bool is_null = (rs->current_row < static_cast<int64_t>(rs->row_nulls.size()) &&
                           i < static_cast<int32_t>(rs->row_nulls[rs->current_row].size()) &&
                           rs->row_nulls[rs->current_row][i]);
            if (!is_null) {
                rs->current_lengths[i] = static_cast<unsigned long>(row_vec[i].length());
            }
        }
    }
    
    return static_cast<SeekdbRow>(row_data);
}

size_t seekdb_row_get_string_len(SeekdbRow row, int32_t column_index) {
    if (!row || column_index < 0) {
        return static_cast<size_t>(-1);
    }
    
    SeekdbRowData* row_data = static_cast<SeekdbRowData*>(row);
    SeekdbResultSet* rs = row_data->result_set;
    
    if (row_data->row_index < 0 || 
        row_data->row_index >= rs->row_count ||
        column_index >= static_cast<int32_t>(rs->column_count)) {
        return static_cast<size_t>(-1);
    }
    
    const std::vector<std::string>& row_vec = rs->rows[row_data->row_index];
    if (column_index >= static_cast<int32_t>(row_vec.size())) {
        return static_cast<size_t>(-1);
    }
    // C ABI contract: NULL returns (size_t)-1; empty string '' returns 0; non-empty returns actual byte length
    if (row_data->row_index < static_cast<int64_t>(rs->row_nulls.size()) &&
        column_index < static_cast<int32_t>(rs->row_nulls[row_data->row_index].size()) &&
        rs->row_nulls[row_data->row_index][column_index]) {
        return static_cast<size_t>(-1);
    }
    return row_vec[column_index].length();
}

int seekdb_row_get_string(SeekdbRow row, int32_t column_index, char* value, size_t value_len) {
    if (!row || !value || value_len == 0 || column_index < 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbRowData* row_data = static_cast<SeekdbRowData*>(row);
    SeekdbResultSet* rs = row_data->result_set;
    
    if (row_data->row_index < 0 || 
        row_data->row_index >= rs->row_count ||
        column_index >= static_cast<int32_t>(rs->column_count)) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    const std::vector<std::string>& row_vec = rs->rows[row_data->row_index];
    if (column_index >= static_cast<int32_t>(row_vec.size())) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    // C ABI contract: NULL -> write '\0' and succeed; non-NULL requires value_len >= len+1 for full copy (no truncation)
    bool is_null = (row_data->row_index < static_cast<int64_t>(rs->row_nulls.size()) &&
                    column_index < static_cast<int32_t>(rs->row_nulls[row_data->row_index].size()) &&
                    rs->row_nulls[row_data->row_index][column_index]);
    if (is_null) {
        value[0] = '\0';
        return SEEKDB_SUCCESS;
    }
    const std::string& str_val = row_vec[column_index];
    size_t len = str_val.length();
    if (value_len < len + 1) {
        return SEEKDB_ERROR_INVALID_PARAM;  // Buffer too small; caller should use seekdb_row_get_string_len first
    }
    if (len > 0 && str_val.data()) {
        memcpy(value, str_val.data(), len);
    }
    value[len] = '\0';
    return SEEKDB_SUCCESS;
}

int seekdb_row_get_int64(SeekdbRow row, int32_t column_index, int64_t* value) {
    if (!row || !value || column_index < 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    char buf[256];
    int ret = seekdb_row_get_string(row, column_index, buf, sizeof(buf));
    if (ret != SEEKDB_SUCCESS) {
        return ret;
    }
    
    *value = strtoll(buf, nullptr, 10);
    return SEEKDB_SUCCESS;
}

int seekdb_row_get_double(SeekdbRow row, int32_t column_index, double* value) {
    if (!row || !value || column_index < 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    char buf[256];
    int ret = seekdb_row_get_string(row, column_index, buf, sizeof(buf));
    if (ret != SEEKDB_SUCCESS) {
        return ret;
    }
    
    *value = strtod(buf, nullptr);
    return SEEKDB_SUCCESS;
}

int seekdb_row_get_bool(SeekdbRow row, int32_t column_index, bool* value) {
    if (!row || !value || column_index < 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    int64_t int_val;
    int ret = seekdb_row_get_int64(row, column_index, &int_val);
    if (ret != SEEKDB_SUCCESS) {
        return ret;
    }
    
    *value = (int_val != 0);
    return SEEKDB_SUCCESS;
}

bool seekdb_row_is_null(SeekdbRow row, int32_t column_index) {
    if (!row || column_index < 0) {
        return true;
    }
    
    SeekdbRowData* row_data = static_cast<SeekdbRowData*>(row);
    SeekdbResultSet* rs = row_data->result_set;
    
    if (row_data->row_index < 0 || 
        row_data->row_index >= rs->row_count ||
        column_index >= static_cast<int32_t>(rs->column_count)) {
        return true;
    }
    
    if (column_index >= static_cast<int32_t>(rs->rows[row_data->row_index].size())) {
        return true;
    }
    // C ABI contract: only true for SQL NULL; empty string '' returns false (distinct from NULL)
    if (row_data->row_index < static_cast<int64_t>(rs->row_nulls.size()) &&
        column_index < static_cast<int32_t>(rs->row_nulls[row_data->row_index].size())) {
        return rs->row_nulls[row_data->row_index][column_index];
    }
    return false;  // Legacy result set without row_nulls (e.g. param metadata)
}

int seekdb_data_seek(SeekdbResult result, my_ulonglong offset) {
    if (!result) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    if (offset >= static_cast<my_ulonglong>(rs->row_count)) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Set current row position (will be incremented on next fetch)
    rs->current_row = static_cast<int64_t>(offset) - 1;
    
    return SEEKDB_SUCCESS;
}

my_ulonglong seekdb_row_tell(SeekdbResult result) {
    if (!result) {
        return static_cast<my_ulonglong>(-1);
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // current_row is -1 before first fetch, so we add 1 to get the actual position
    int64_t current_pos = rs->current_row + 1;
    
    if (current_pos < 0 || current_pos >= rs->row_count) {
        return static_cast<my_ulonglong>(-1);
    }
    
    return static_cast<my_ulonglong>(current_pos);
}

SeekdbRow seekdb_row_seek(SeekdbResult result, SeekdbRow row) {
    if (!result || !row) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    SeekdbRowData* row_data = static_cast<SeekdbRowData*>(row);
    
    // Verify row belongs to this result set
    if (row_data->result_set != rs) {
        return nullptr;
    }
    
    // Set current row position (will be incremented on next fetch)
    rs->current_row = row_data->row_index - 1;
    
    // Return the row handle (can be used for future seeks)
    return row;
}

unsigned long* seekdb_fetch_lengths(SeekdbResult result) {
    if (!result) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // Return lengths for the current row (set by seekdb_fetch_row)
    if (rs->current_lengths.empty() || rs->current_row < 0 || rs->current_row >= rs->row_count) {
        return nullptr;
    }
    
    return rs->current_lengths.data();
}

void seekdb_result_free(SeekdbResult result) {
    if (!result) {
        return;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // Check if already freed to prevent double free
    if (rs->freed) {
        return;  // Already freed, ignore
    }
    
    // Mark as freed first to prevent re-entry
    rs->freed = true;
    
    // Safely get owner connection (may be null if already cleared)
    SeekdbConnection* conn = rs->owner_conn;
    
    // Clear reference in connection to prevent double free
    // Note: After seekdb_store_result(), last_result_set should already be nullptr
    // but we check anyway for safety
    if (conn) {
        if (conn->last_result_set == rs) {
            conn->last_result_set = nullptr;
        }
        if (conn->use_result_set == rs) {
            conn->use_result_set = nullptr;
        }
        // Also remove from result_sets vector if present
        auto it = std::find(conn->result_sets.begin(), conn->result_sets.end(), rs);
        if (it != conn->result_sets.end()) {
            conn->result_sets.erase(it);
        }
    }
    
    delete rs;
}

const char* seekdb_last_error(void) {
    if (g_thread_last_error.empty()) {
        return nullptr;
    }
    return g_thread_last_error.c_str();
}

int seekdb_last_error_code(void) {
    return g_thread_last_error_code;
}

int seekdb_get_last_error(SeekdbHandle handle, char* error_msg, size_t error_msg_len) {
    if (!handle || !error_msg || error_msg_len == 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    size_t copy_len = std::min(conn->last_error.length(), error_msg_len - 1);
    strncpy(error_msg, conn->last_error.c_str(), copy_len);
    error_msg[copy_len] = '\0';
    
    // Also update thread-local error
    g_thread_last_error = conn->last_error;
    
    return SEEKDB_SUCCESS;
}

// Internal implementation of seekdb_execute_update
struct ExecuteUpdateParams {
    SeekdbHandle handle;
    const char* sql;
    int64_t* affected_rows;
    int ret_code;
};

static int do_seekdb_execute_update_inner(ExecuteUpdateParams* params) {
    SeekdbHandle handle = params->handle;
    const char* sql = params->sql;
    int64_t* affected_rows = params->affected_rows;
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        params->ret_code = SEEKDB_ERROR_INVALID_PARAM;
        return OB_SUCCESS;
    }
    
    int ret = OB_SUCCESS;
    int64_t rows = 0;
    
    // Embedded mode only
    if (!conn->embed_conn) {
        params->ret_code = SEEKDB_ERROR_INVALID_PARAM;
        return OB_SUCCESS;
    }
    
    // Initialize trace ID (aligned with Python embed)
    ObCurTraceId::init(GCTX.self_addr());
    
    // Setup warning buffer (aligned with Python embed)
    if (OB_NOT_NULL(conn->embed_session)) {
        ob_setup_tsi_warning_buffer(&conn->embed_session->get_warnings_buffer());
    }
    
    // Reset previous result if exists
    if (conn->embed_result) {
        conn->embed_result->close();
        conn->embed_result->~ReadResult();
        ob_free(conn->embed_result);
        conn->embed_result = nullptr;
    }
    
    // Align with MySQL protocol: check and refresh only when server is behind session.
    if (OB_NOT_NULL(conn->embed_session)) {
        check_and_refresh_schema_for_embed(conn->embed_session);
    }
    
    ObString sql_string(sql);
    ret = conn->embed_conn->execute_write(sql_string, rows, true);
    
    // Reset warning buffer after execute (aligned with Python embed)
    if (OB_NOT_NULL(conn->embed_session)) {
        conn->embed_session->reset_warnings_buf();
    }
    ob_setup_tsi_warning_buffer(NULL);
    
    // DDL / read-after-write visibility: after execute_write, one refresh + sync session (align with MySQL DDL path).
    if (OB_SUCCESS == ret && OB_NOT_NULL(conn->embed_session) && OB_NOT_NULL(GCTX.schema_service_)) {
        refresh_session_schema_version(conn->embed_session);
        (void)oceanbase::sql::ObSQLUtils::update_session_last_schema_version(*GCTX.schema_service_, *conn->embed_session);
    }
    
    if (OB_SUCCESS == ret) {
        *affected_rows = rows;
        params->ret_code = SEEKDB_SUCCESS;
    } else {
        // Get detailed error message (aligned with do_seekdb_execute_inner / Python embed)
        // Surface the real OB error/warning so callers can diagnose failures instead of
        // seeing only a generic "Update execution failed".
        std::string errmsg;
        const oceanbase::common::ObWarningBuffer *wb = oceanbase::common::ob_get_tsi_warning_buffer();
        if (nullptr != wb) {
            if (wb->get_err_code() == ret ||
                (ret >= OB_MIN_RAISE_APPLICATION_ERROR && ret <= OB_MAX_RAISE_APPLICATION_ERROR)) {
                if (wb->get_err_msg() != nullptr && wb->get_err_msg()[0] != '\0') {
                    errmsg = std::string(wb->get_err_msg());
                }
            }
        }
        if (errmsg.empty()) {
            const char *err_str = ob_errpkt_strerror(ret);
            if (nullptr != err_str && err_str[0] != '\0') {
                errmsg = std::string(err_str);
            }
        }
        if (errmsg.empty()) {
            errmsg = "Update execution failed";
        }
        // Append OB error code so logs always carry numeric ret even when the message is non-empty.
        char code_suffix[32];
        snprintf(code_suffix, sizeof(code_suffix), " (ret=%d)", ret);
        errmsg += code_suffix;
        set_error(conn, errmsg.c_str());
        params->ret_code = SEEKDB_ERROR_QUERY_FAILED;
    }
    return OB_SUCCESS;
}

// Internal/legacy: write-only path. MySQL-aligned usage is seekdb_query() for all SQL + seekdb_affected_rows().
int seekdb_execute_update(SeekdbHandle handle, const char* sql, int64_t* affected_rows) {
    if (!handle || !sql || !affected_rows) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Execute directly on main stack (aligned with Python embed)
    // Stack attributes were set to reasonable defaults in seekdb_open()
    ExecuteUpdateParams params;
    params.handle = handle;
    params.sql = sql;
    params.affected_rows = affected_rows;
    params.ret_code = SEEKDB_ERROR_QUERY_FAILED;
    
    do_seekdb_execute_update_inner(&params);
    
    // Store affected rows for seekdb_affected_rows()
    if (params.ret_code == SEEKDB_SUCCESS && affected_rows) {
        SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
        if (conn) {
            conn->last_affected_rows = static_cast<unsigned long long>(*affected_rows);
        }
    }
    
    return params.ret_code;
}

// SeekDB extension: Begin a transaction
// In MySQL 5.7 and 8.0 C API, there is no mysql_begin() function.
// To begin a transaction in MySQL C API:
// - Use mysql_autocommit(mysql, 0) to disable autocommit mode, or
// - Execute "START TRANSACTION" SQL statement using mysql_query() or mysql_real_query()
// This function provides a convenient way to start a transaction, equivalent to executing "START TRANSACTION" SQL statement.
int seekdb_begin(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn->embed_conn || !conn->embed_session) {
        set_error(conn, "Connection not initialized");
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    int ret = OB_SUCCESS;
    
    // Reset any previous result
    if (conn->embed_result) {
        conn->embed_result->close();
        conn->embed_result->~ReadResult();
        ob_free(conn->embed_result);
        conn->embed_result = nullptr;
    }
    
    // If already in transaction, rollback first (like Python embed does)
    if (conn->embed_session->is_in_transaction()) {
        conn->embed_conn->set_is_in_trans(true);
        if (OB_FAIL(conn->embed_conn->rollback())) {
            set_error(conn, "Failed to rollback previous transaction");
            return SEEKDB_ERROR_QUERY_FAILED;
        }
    }
    // Session flag can be cleared while ObInnerSQLConnection::is_in_trans_ is still true (e.g.
    // DDL or Room invalidation trigger paths). start_transaction_inner then fails with
    // "inner conn is already in trans". Roll back to resync before START TRANSACTION.
    if (conn->embed_conn->is_in_trans()) {
        conn->embed_conn->set_is_in_trans(true);
        if (OB_FAIL(conn->embed_conn->rollback())) {
            set_error(conn, "Failed to rollback orphan inner transaction state");
            return SEEKDB_ERROR_QUERY_FAILED;
        }
    }

    // Start new transaction
    // This is equivalent to executing "START TRANSACTION" SQL statement in MySQL 5.7
    if (OB_FAIL(conn->embed_conn->start_transaction())) {
        set_error(conn, "Failed to start transaction");
        return SEEKDB_ERROR_QUERY_FAILED;
    }
    
    return SEEKDB_SUCCESS;
}

int seekdb_commit(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn->embed_conn || !conn->embed_session) {
        set_error(conn, "Connection not initialized");
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Reset any previous result
    if (conn->embed_result) {
        conn->embed_result->close();
        conn->embed_result->~ReadResult();
        ob_free(conn->embed_result);
        conn->embed_result = nullptr;
    }
    
    int ret = OB_SUCCESS;
    
    // Only commit if in transaction
    if (conn->embed_session->is_in_transaction()) {
        conn->embed_conn->set_is_in_trans(true);
        if (OB_FAIL(conn->embed_conn->commit())) {
            set_error(conn, "Failed to commit transaction");
            return SEEKDB_ERROR_QUERY_FAILED;
        }
    }
    
    return SEEKDB_SUCCESS;
}

int seekdb_rollback(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn->embed_conn || !conn->embed_session) {
        set_error(conn, "Connection not initialized");
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Reset any previous result
    if (conn->embed_result) {
        conn->embed_result->close();
        conn->embed_result->~ReadResult();
        ob_free(conn->embed_result);
        conn->embed_result = nullptr;
    }
    
    int ret = OB_SUCCESS;
    
    // Only rollback if in transaction
    if (conn->embed_session->is_in_transaction()) {
        conn->embed_conn->set_is_in_trans(true);
        if (OB_FAIL(conn->embed_conn->rollback())) {
            set_error(conn, "Failed to rollback transaction");
            return SEEKDB_ERROR_QUERY_FAILED;
        }
    }
    
    return SEEKDB_SUCCESS;
}

int seekdb_autocommit(SeekdbHandle handle, bool mode) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn->embed_session) {
        set_error(conn, "Connection not initialized");
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    int ret = OB_SUCCESS;
    if (OB_FAIL(conn->embed_session->set_autocommit(mode))) {
        set_error(conn, "Failed to set autocommit");
        return SEEKDB_ERROR_QUERY_FAILED;
    }
    
    return SEEKDB_SUCCESS;
}

my_ulonglong seekdb_affected_rows(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return 0;
    }
    return conn->last_affected_rows;
}

my_ulonglong seekdb_insert_id(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !conn->embed_session) {
        return 0;
    }
    
    // Execute SELECT LAST_INSERT_ID() to get the last inserted ID
    SeekdbResult result = nullptr;
    int ret = seekdb_query(handle, "SELECT LAST_INSERT_ID() as id", &result);
    if (ret != SEEKDB_SUCCESS || !result) {
        return 0;
    }
    
    my_ulonglong insert_id = 0;
    SeekdbRow row = seekdb_fetch_row(result);
    if (row) {
        int64_t id_value = 0;
        if (seekdb_row_get_int64(row, 0, &id_value) == SEEKDB_SUCCESS) {
            insert_id = static_cast<my_ulonglong>(id_value);
        }
    }
    seekdb_result_free(result);
    return insert_id;
}

int seekdb_ping(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn->embed_conn || !conn->embed_session) {
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Execute a simple query to check if connection is alive
    SeekdbResult result = nullptr;
    int ret = seekdb_query(handle, "SELECT 1", &result);
    if (ret == SEEKDB_SUCCESS && result) {
        seekdb_result_free(result);
        return SEEKDB_SUCCESS;
    }
    
    return SEEKDB_ERROR_CONNECTION_FAILED;
}

// Store server version in connection for seekdb_get_server_info()
static thread_local std::string g_server_version;

const char* seekdb_get_server_info(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    if (!conn->embed_conn || !conn->embed_session) {
        return nullptr;
    }
    
    // Execute SELECT VERSION() to get server version
    SeekdbResult result = nullptr;
    int ret = seekdb_query(handle, "SELECT VERSION() as version", &result);
    if (ret != SEEKDB_SUCCESS || !result) {
        return nullptr;
    }
    
    SeekdbRow row = seekdb_fetch_row(result);
    if (row) {
        char buf[256];
        if (seekdb_row_get_string(row, 0, buf, sizeof(buf)) == SEEKDB_SUCCESS) {
            g_server_version = std::string(buf);
        }
    }
    seekdb_result_free(result);
    return g_server_version.c_str();
}

const char* seekdb_character_set_name(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !conn->embed_session) {
        return nullptr;
    }
    
    // Get character set from session
    ObObj charset_obj;
    if (OB_SUCCESS != conn->embed_session->get_sys_variable(SYS_VAR_CHARACTER_SET_CONNECTION, charset_obj)) {
        return nullptr;
    }
    
    int64_t collation_type = 0;
    if (OB_SUCCESS != charset_obj.get_int(collation_type)) {
        return nullptr;
    }
    
    // Convert collation type to charset name
    ObCollationType coll_type = static_cast<ObCollationType>(collation_type);
    ObCharsetType cs_type = ObCharset::charset_type_by_coll(coll_type);
    const char* cs_name = ObCharset::charset_name(cs_type);
    
    // Store in connection for lifetime management
    static thread_local std::string g_charset_name;
    if (cs_name) {
        g_charset_name = std::string(cs_name);
        return g_charset_name.c_str();
    }
    
    return nullptr;
}

int seekdb_set_character_set(SeekdbHandle handle, const char* csname) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !conn->embed_session || !csname) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Build SET NAMES statement
    char sql[256];
    int ret = snprintf(sql, sizeof(sql), "SET NAMES %s", csname);
    if (ret < 0 || ret >= static_cast<int>(sizeof(sql))) {
        set_error(conn, "Character set name too long");
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Execute SET NAMES
    SeekdbResult result = nullptr;
    int query_ret = seekdb_query(handle, sql, &result);
    if (query_ret != SEEKDB_SUCCESS) {
        set_error(conn, "Failed to set character set");
        return SEEKDB_ERROR_QUERY_FAILED;
    }
    
    if (result) {
        seekdb_result_free(result);
    }
    
    return SEEKDB_SUCCESS;
}

int seekdb_select_db(SeekdbHandle handle, const char* db) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !db) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Build USE statement
    char sql[512];
    int ret = snprintf(sql, sizeof(sql), "USE %s", db);
    if (ret < 0 || ret >= static_cast<int>(sizeof(sql))) {
        set_error(conn, "Database name too long");
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Execute USE
    SeekdbResult result = nullptr;
    int query_ret = seekdb_query(handle, sql, &result);
    if (query_ret != SEEKDB_SUCCESS) {
        set_error(conn, "Failed to select database");
        return SEEKDB_ERROR_QUERY_FAILED;
    }
    
    if (result) {
        seekdb_result_free(result);
    }
    // Align with MySQL protocol: after USE db, check and refresh only when server is behind session.
    if (conn->embed_session) {
        check_and_refresh_schema_for_embed(conn->embed_session);
    }
    
    return SEEKDB_SUCCESS;
}

const char* seekdb_get_host_info(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    // For embedded mode, return "localhost via TCP/IP" or similar
    static thread_local std::string g_host_info = "localhost via embedded connection";
    return g_host_info.c_str();
}

const char* seekdb_get_client_info(void) {
    // Return client version
    static const char* client_info = "SeekDB Embedded Client";
    return client_info;
}

const char* seekdb_info(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    // Get info from last query (e.g., "Rows matched: 5  Changed: 5  Warnings: 0")
    // For now, return a simple message
    static thread_local std::string g_info;
    my_ulonglong affected = seekdb_affected_rows(handle);
    char buf[256];
    snprintf(buf, sizeof(buf), "Rows matched: %llu  Changed: %llu  Warnings: 0", 
             affected, affected);
    g_info = std::string(buf);
    return g_info.c_str();
}

unsigned int seekdb_warning_count(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !conn->embed_session) {
        return 0;
    }
    
    // Get warning count from session
    const oceanbase::common::ObWarningBuffer *wb = oceanbase::common::ob_get_tsi_warning_buffer();
    if (wb) {
        return static_cast<unsigned int>(wb->get_total_warning_count());
    }
    
    return 0;
}

const char* seekdb_sqlstate(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    // Map error code to SQLSTATE
    // For now, return a generic SQLSTATE based on error code
    static thread_local char sqlstate[6] = "00000";
    unsigned int errno_val = seekdb_errno(handle);
    
    // Map common error codes to SQLSTATE
    // This is a simplified mapping
    if (errno_val == SEEKDB_SUCCESS) {
        strcpy(sqlstate, "00000");  // Success
    } else if (errno_val == SEEKDB_ERROR_CONNECTION_FAILED) {
        strcpy(sqlstate, "08001");  // SQLSTATE for connection failure
    } else if (errno_val == SEEKDB_ERROR_QUERY_FAILED) {
        strcpy(sqlstate, "42000");  // SQLSTATE for syntax error or access rule violation
    } else if (errno_val == SEEKDB_ERROR_INVALID_PARAM) {
        strcpy(sqlstate, "HY000");  // General error
    } else {
        strcpy(sqlstate, "HY000");  // General error
    }
    
    return sqlstate;
}

int seekdb_get_character_set_info(SeekdbHandle handle, const char* csname, SeekdbCharsetInfo* charset_info) {
    if (!handle || !csname || !charset_info) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Get charset type from name
    ObCharsetType cs_type = ObCharset::charset_type(ObString(csname));
    if (cs_type == CHARSET_INVALID) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Fill charset info
    memset(charset_info, 0, sizeof(SeekdbCharsetInfo));
    charset_info->number = static_cast<uint32_t>(cs_type);
    
    // Get charset name
    const char* cs_name = ObCharset::charset_name(cs_type);
    if (cs_name) {
        charset_info->name = cs_name;
    }
    
    // Get default collation
    ObCollationType coll_type = ObCharset::get_default_collation(cs_type);
    ObString coll_name;
    if (OB_SUCCESS == ObCharset::collation_name(coll_type, coll_name)) {
        // Store in thread-local buffer
        static thread_local char coll_name_buf[64];
        size_t copy_len = std::min(coll_name.length(), static_cast<int32_t>(sizeof(coll_name_buf) - 1));
        memcpy(coll_name_buf, coll_name.ptr(), copy_len);
        coll_name_buf[copy_len] = '\0';
        charset_info->collation = coll_name_buf;
    }
    
    // Set comment (simplified - use charset name as comment)
    charset_info->comment = cs_name;
    
    return SEEKDB_SUCCESS;
}

unsigned long seekdb_real_escape_string(SeekdbHandle handle, char* to, unsigned long to_len, 
                                        const char* from, unsigned long from_len) {
    if (!handle || !to || to_len == 0 || !from || from_len == 0) {
        return static_cast<unsigned long>(-1);
    }
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return static_cast<unsigned long>(-1);
    }
    
    // Escape special characters: \0, \n, \r, \, ', ", and \x1a
    unsigned long j = 0;
    for (unsigned long i = 0; i < from_len && j < to_len - 1; i++) {
        char c = from[i];
        switch (c) {
            case '\0':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = '0';
                }
                break;
            case '\n':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = 'n';
                }
                break;
            case '\r':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = 'r';
                }
                break;
            case '\\':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = '\\';
                }
                break;
            case '\'':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = '\'';
                }
                break;
            case '"':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = '"';
                }
                break;
            case '\x1a':  // Ctrl+Z
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = 'Z';
                }
                break;
            default:
                to[j++] = c;
                break;
        }
    }
    
    to[j] = '\0';
    return j;
}

unsigned long seekdb_real_escape_string_quote(SeekdbHandle handle, char* to, unsigned long to_len,
                                               const char* from, unsigned long from_len, char quote) {
    if (!handle || !to || to_len == 0 || !from || from_len == 0) {
        return static_cast<unsigned long>(-1);
    }
    
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return static_cast<unsigned long>(-1);
    }
    
    // Escape special characters, considering quote context
    // If quote is '\'', escape single quotes; if quote is '"', escape double quotes
    unsigned long j = 0;
    for (unsigned long i = 0; i < from_len && j < to_len - 1; i++) {
        char c = from[i];
        switch (c) {
            case '\0':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = '0';
                }
                break;
            case '\n':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = 'n';
                }
                break;
            case '\r':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = 'r';
                }
                break;
            case '\\':
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = '\\';
                }
                break;
            case '\'':
                // Escape single quote only if quote context is single quote
                if (quote == '\'') {
                    if (j + 1 < to_len - 1) {
                        to[j++] = '\\';
                        to[j++] = '\'';
                    }
                } else {
                    to[j++] = c;
                }
                break;
            case '"':
                // Escape double quote only if quote context is double quote
                if (quote == '"') {
                    if (j + 1 < to_len - 1) {
                        to[j++] = '\\';
                        to[j++] = '"';
                    }
                } else {
                    to[j++] = c;
                }
                break;
            case '\x1a':  // Ctrl+Z
                if (j + 1 < to_len - 1) {
                    to[j++] = '\\';
                    to[j++] = 'Z';
                }
                break;
            default:
                to[j++] = c;
                break;
        }
    }
    
    to[j] = '\0';
    return j;
}

unsigned long seekdb_hex_string(char* to, unsigned long to_len, const char* from, unsigned long from_len) {
    if (!to || to_len == 0 || !from || from_len == 0) {
        return static_cast<unsigned long>(-1);
    }
    
    // Each byte becomes 2 hex characters
    if (to_len < from_len * 2 + 1) {
        return static_cast<unsigned long>(-1);
    }
    
    static const char hex_chars[] = "0123456789ABCDEF";
    unsigned long j = 0;
    
    for (unsigned long i = 0; i < from_len && j < to_len - 1; i++) {
        unsigned char c = static_cast<unsigned char>(from[i]);
        to[j++] = hex_chars[(c >> 4) & 0x0F];
        to[j++] = hex_chars[c & 0x0F];
    }
    
    to[j] = '\0';
    return j;
}

unsigned long seekdb_get_server_version(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return 0;
    }
    
    // Get server version string
    const char* version_str = seekdb_get_server_info(handle);
    if (!version_str) {
        return 0;
    }
    
    // Parse version string (format: "major.minor.patch" or "major.minor.patch.extra")
    unsigned long major = 0, minor = 0, patch = 0;
    if (sscanf(version_str, "%lu.%lu.%lu", &major, &minor, &patch) >= 2) {
        // Format: major * 10000 + minor * 100 + patch
        return major * 10000 + minor * 100 + patch;
    }
    
    return 0;
}

int seekdb_change_user(SeekdbHandle handle, const char* user, const char* password, const char* database) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized || !user) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // For embedded mode, changing user is not typically needed
    // But we can switch database if provided
    if (database) {
        int ret = seekdb_select_db(handle, database);
        if (ret != SEEKDB_SUCCESS) {
            return ret;
        }
    }
    
    // Note: In embedded mode, user/password authentication is typically not used
    // This function mainly switches the database context
    return SEEKDB_SUCCESS;
}

int seekdb_reset_connection(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn->embed_session) {
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Reset session state
    // This includes: session variables, temporary tables, locks, etc.
    // In embedded mode, we can reset the session by clearing state
    
    // Clear any pending transactions
    if (conn->embed_session->is_in_transaction()) {
        // Rollback any pending transaction
        int ret = seekdb_rollback(handle);
        if (ret != SEEKDB_SUCCESS) {
            return ret;
        }
    }
    
    // Reset autocommit to default (true)
    conn->embed_session->set_autocommit(true);
    
    // Clear last error
    conn->last_error.clear();
    
    // Note: In embedded mode, we don't need to reset network connection state
    // as there's no network connection. Session variables and temporary tables
    // are managed by the session itself.
    
    return SEEKDB_SUCCESS;
}

int seekdb_next_result(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Check if there are more result sets in the queue
    if (conn->current_result_index >= 0 && 
        conn->current_result_index + 1 < static_cast<int>(conn->result_sets.size())) {
        conn->current_result_index++;
        return 0;  // More results available
    }
    
    // For now, we don't support multiple result sets from a single query
    // This would require parsing multi-statement queries or stored procedures
    // Return -1 to indicate no more results
    return -1;
}

bool seekdb_more_results(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return false;
    }
    
    // Check if there are more result sets
    if (conn->current_result_index >= 0 && 
        conn->current_result_index + 1 < static_cast<int>(conn->result_sets.size())) {
        return true;
    }
    
    return false;
}

const char* seekdb_error(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    return conn->last_error.empty() ? nullptr : conn->last_error.c_str();
}

unsigned int seekdb_errno(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return 0;
    }
    
    // Return error code from thread-local storage
    return static_cast<unsigned int>(seekdb_last_error_code());
}

int seekdb_result_get_all_column_names(
    SeekdbResult result,
    char** names,
    char* name_bufs,
    size_t name_buf_size,
    int32_t* column_count
) {
    if (!result || !names || !name_bufs || name_buf_size == 0 || !column_count) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    int32_t actual_count = rs->column_count;
    
    if (actual_count > *column_count) {
        return SEEKDB_ERROR_INVALID_PARAM; // Buffer too small
    }
    
    for (int32_t i = 0; i < actual_count; i++) {
        if (i >= static_cast<int32_t>(rs->column_names.size())) {
            return SEEKDB_ERROR_INVALID_PARAM;
        }
        
        const std::string& col_name = rs->column_names[i];
        size_t copy_len = std::min(col_name.length(), name_buf_size - 1);
        
        char* buf = name_bufs + i * name_buf_size;
        strncpy(buf, col_name.c_str(), copy_len);
        buf[copy_len] = '\0';
        names[i] = buf;
    }
    
    *column_count = actual_count;
    return SEEKDB_SUCCESS;
}

int seekdb_result_get_all_column_names_alloc(
    SeekdbResult result,
    char*** names,
    int32_t* column_count
) {
    if (!result || !names || !column_count) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    int32_t actual_count = rs->column_count;
    
    // Allocate array of char* pointers
    char** name_array = static_cast<char**>(malloc(sizeof(char*) * actual_count));
    if (!name_array) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    // Allocate and copy column names
    for (int32_t i = 0; i < actual_count; i++) {
        if (i >= static_cast<int32_t>(rs->column_names.size())) {
            // Free already allocated names
            for (int32_t j = 0; j < i; j++) {
                free(name_array[j]);
            }
            free(name_array);
            return SEEKDB_ERROR_INVALID_PARAM;
        }
        
        const std::string& col_name = rs->column_names[i];
        size_t name_len = col_name.length() + 1; // +1 for null terminator
        name_array[i] = static_cast<char*>(malloc(name_len));
        if (!name_array[i]) {
            // Free already allocated names
            for (int32_t j = 0; j < i; j++) {
                free(name_array[j]);
            }
            free(name_array);
            return SEEKDB_ERROR_MEMORY_ALLOC;
        }
        strncpy(name_array[i], col_name.c_str(), name_len - 1);
        name_array[i][name_len - 1] = '\0';
    }
    
    *names = name_array;
    *column_count = actual_count;
    return SEEKDB_SUCCESS;
}

void seekdb_free_column_names(char** names, int32_t column_count) {
    if (!names || column_count <= 0) {
        return;
    }
    
    for (int32_t i = 0; i < column_count; i++) {
        if (names[i]) {
            free(names[i]);
        }
    }
    
    free(names);
}

int seekdb_result_fetch_all(
    SeekdbResult result,
    seekdb_cell_callback_t callback,
    void* user_data
) {
    if (!result || !callback) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    for (int64_t row_idx = 0; row_idx < rs->row_count; row_idx++) {
        if (row_idx >= static_cast<int64_t>(rs->rows.size())) {
            break;
        }
        
        const std::vector<std::string>& row = rs->rows[row_idx];
        
        for (int32_t col_idx = 0; col_idx < rs->column_count; col_idx++) {
            if (col_idx >= static_cast<int32_t>(row.size())) {
                break;
            }
            
            const std::string& cell_value = row[col_idx];
            bool is_null = (row_idx < static_cast<int64_t>(rs->row_nulls.size()) &&
                           col_idx < static_cast<int32_t>(rs->row_nulls[row_idx].size()) &&
                           rs->row_nulls[row_idx][col_idx]);
            
            int ret = callback(
                row_idx,
                col_idx,
                is_null,
                is_null ? nullptr : cell_value.c_str(),
                is_null ? 0 : cell_value.length(),
                user_data
            );
            
            if (ret != 0) {
                // Callback requested to stop
                return SEEKDB_SUCCESS;
            }
        }
    }
    
    return SEEKDB_SUCCESS;
}

SeekdbField* seekdb_fetch_fields(SeekdbResult result) {
    if (!result) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // Fields should already be built in do_seekdb_execute_inner() if we got them from database
    // If not, build a minimal version from column_names
    if (rs->fields.empty() && rs->column_count > 0) {
        rs->fields.resize(rs->column_count);
        rs->field_strings.resize(rs->column_count);
        
        for (int32_t i = 0; i < rs->column_count; i++) {
            if (i >= static_cast<int32_t>(rs->column_names.size())) {
                return nullptr;
            }
            
            const std::string& col_name = rs->column_names[i];
            
            // Initialize field structure
            memset(&rs->fields[i], 0, sizeof(SeekdbField));
            
            // Store string in field_strings for lifetime management
            rs->field_strings[i].col_name = col_name;
            
            // Point to internal strings (valid until result is freed)
            rs->fields[i].name = rs->field_strings[i].col_name.c_str();
            rs->fields[i].name_length = static_cast<uint32_t>(col_name.length());
            rs->fields[i].org_name = rs->fields[i].name;
            rs->fields[i].org_name_length = rs->fields[i].name_length;
            rs->fields[i].table = nullptr;
            rs->fields[i].org_table = nullptr;
            rs->fields[i].db = nullptr;
            rs->fields[i].catalog = "def"; // Default catalog (MySQL compatibility)
            rs->fields[i].catalog_length = 3;
            rs->fields[i].def = nullptr;
            rs->fields[i].length = 0; // Will be determined from actual data
            rs->fields[i].max_length = 0;
            rs->fields[i].flags = 0;
            rs->fields[i].decimals = 0;
            rs->fields[i].charsetnr = 0;
            rs->fields[i].type = 0; // Field type (to be determined)
            rs->fields[i].extension = nullptr;
        }
    }
    
    return rs->fields.empty() ? nullptr : rs->fields.data();
}

SeekdbField* seekdb_fetch_field(SeekdbResult result) {
    if (!result) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // Ensure fields are built
    if (rs->fields.empty()) {
        seekdb_fetch_fields(result);
    }
    
    if (rs->fields.empty() || rs->current_field >= rs->fields.size()) {
        return nullptr; // No more fields
    }
    
    // Return current field and advance position
    SeekdbField* field = &rs->fields[rs->current_field];
    rs->current_field++;
    
    return field;
}

SeekdbField* seekdb_fetch_field_direct(SeekdbResult result, unsigned int fieldnr) {
    if (!result) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // Ensure fields are built
    if (rs->fields.empty()) {
        seekdb_fetch_fields(result);
    }
    
    if (fieldnr >= static_cast<unsigned int>(rs->fields.size())) {
        return nullptr;
    }
    
    return &rs->fields[fieldnr];
}

int seekdb_field_seek(SeekdbResult result, unsigned int offset) {
    if (!result) {
        return -1;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    // Ensure fields are built
    if (rs->fields.empty()) {
        seekdb_fetch_fields(result);
    }
    
    if (offset >= static_cast<unsigned int>(rs->fields.size())) {
        return -1;
    }
    
    unsigned int prev_field = rs->current_field;
    rs->current_field = offset;
    
    return static_cast<int>(prev_field);
}

unsigned int seekdb_field_tell(SeekdbResult result) {
    if (!result) {
        return static_cast<unsigned int>(-1);
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    
    return rs->current_field;
}

int seekdb_result_fetch_all_rows(
    SeekdbResult result,
    char*** rows,
    uint64_t* row_count,
    uint32_t* column_count
) {
    if (!result || !rows || !row_count || !column_count) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = static_cast<SeekdbResultSet*>(result);
    uint64_t actual_row_count = static_cast<uint64_t>(rs->row_count);
    uint32_t actual_column_count = static_cast<uint32_t>(rs->column_count);
    
    // Allocate memory for row pointers
    char** row_ptrs = static_cast<char**>(malloc(sizeof(char*) * actual_row_count * actual_column_count));
    if (!row_ptrs) {
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    // Allocate memory for row arrays (each row is char*[])
    // Function signature is char*** rows, so *rows should be char**
    // We'll use row_ptrs directly, organized as rows
    // row_ptrs is a flat array: [row0_col0, row0_col1, ..., row1_col0, row1_col1, ...]
    // We need to set up *rows to point to row_ptrs, but organized as rows
    // Actually, let's allocate a separate array of row pointers
    char** row_arrays = static_cast<char**>(malloc(sizeof(char*) * actual_row_count));
    if (!row_arrays) {
        free(row_ptrs);
        return SEEKDB_ERROR_MEMORY_ALLOC;
    }
    
    for (uint64_t row_idx = 0; row_idx < actual_row_count; row_idx++) {
        if (row_idx >= rs->rows.size()) {
            break;
        }
        
        const std::vector<std::string>& row = rs->rows[row_idx];
        // Each row_arrays[row_idx] points to the start of a row in row_ptrs
        row_arrays[row_idx] = row_ptrs[row_idx * actual_column_count];
        
        for (uint32_t col_idx = 0; col_idx < actual_column_count; col_idx++) {
            char** row_ptr = reinterpret_cast<char**>(&row_ptrs[row_idx * actual_column_count]);
            if (col_idx >= row.size()) {
                row_ptr[col_idx] = nullptr; // NULL value
            } else {
                const std::string& cell_value = row[col_idx];
                if (cell_value.empty()) {
                    // NULL value
                    row_ptr[col_idx] = nullptr;
                } else {
                    // Allocate memory for string value
                    size_t value_len = cell_value.length() + 1; // +1 for null terminator
                    char* value_buf = static_cast<char*>(malloc(value_len));
                    if (!value_buf) {
                        // Free already allocated memory
                        for (uint64_t i = 0; i < row_idx; i++) {
                            for (uint32_t j = 0; j < actual_column_count; j++) {
                                char** rp = reinterpret_cast<char**>(&row_ptrs[i * actual_column_count]);
                                if (rp[j]) {
                                    free(rp[j]);
                                }
                            }
                        }
                        free(row_arrays);
                        free(row_ptrs);
                        return SEEKDB_ERROR_MEMORY_ALLOC;
                    }
                    strncpy(value_buf, cell_value.c_str(), value_len - 1);
                    value_buf[value_len - 1] = '\0';
                    row_ptr[col_idx] = value_buf;
                }
            }
        }
    }
    
    // Function signature: char*** rows, so *rows is char**
    // row_arrays is char**, so assignment should work
    *rows = row_arrays;
    *row_count = actual_row_count;
    *column_count = actual_column_count;
    return SEEKDB_SUCCESS;
}

void seekdb_result_free_all_rows(
    char** rows,
    uint64_t row_count,
    uint32_t column_count
) {
    if (!rows || row_count == 0 || column_count == 0) {
        return;
    }
    
    // Free all string values
    // The structure from seekdb_result_fetch_all_rows:
    // - row_ptrs: flat array of char* (actual_row_count * actual_column_count elements)
    // - row_arrays: array of char*, each pointing to the start of a row in row_ptrs
    // - rows (parameter) is row_arrays, which is char**
    // - rows[i] is char*, pointing to row_ptrs[i * actual_column_count]
    // - To access the j-th element of row i: treat rows[i] as char** and access rows[i][j]
    //   But rows[i] is char*, so we need to reinterpret_cast it to char**
    for (uint64_t i = 0; i < row_count; i++) {
        if (rows[i]) {
            // rows[i] points to the first char* of row i in the flat array
            // We need to treat it as char** to access individual char* elements
            char** row_elements = reinterpret_cast<char**>(rows[i]);
            for (uint32_t j = 0; j < column_count; j++) {
                if (row_elements[j]) {
                    free(row_elements[j]);
                }
            }
        }
    }
    
    // Free the row arrays pointer (row_arrays)
    free(rows);
}

// Prepared Statement Implementation

SeekdbStmt seekdb_stmt_init(SeekdbHandle handle) {
    SeekdbConnection* conn = static_cast<SeekdbConnection*>(handle);
    if (!conn || !conn->initialized) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt = new (std::nothrow) SeekdbStmtData(conn);
    if (!stmt) {
        return nullptr;
    }
    
    return static_cast<SeekdbStmt>(stmt);
}

int seekdb_stmt_prepare(SeekdbStmt stmt, const char* query, unsigned long length) {
    if (!stmt || !query || length == 0) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    SeekdbConnection* conn = stmt_data->conn;
    
    if (!conn || !conn->initialized || !conn->embed_session) {
        stmt_data->last_error = "Connection not initialized";
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Store SQL statement
    stmt_data->sql = std::string(query, length);
    
    // Count placeholders (?)
    unsigned long param_count = 0;
    for (unsigned long i = 0; i < length; i++) {
        if (query[i] == '?') {
            param_count++;
        }
    }
    
    stmt_data->param_count = param_count;
    stmt_data->param_binds.resize(param_count);
    
    // Get column types from table schema (for INSERT statements)
    // Get column type information so we can auto-detect VECTOR type parameters
    if (param_count > 0) {
        get_insert_column_types(conn, stmt_data->sql, stmt_data->param_column_types);
    }
    
    // For now, we'll use a simplified approach: prepare the statement
    // by executing PREPARE statement through SQL
    // Note: This is a simplified implementation. A full implementation
    // would use OceanBase's stmt_prepare API directly
    
    stmt_data->prepared = true;
    return SEEKDB_SUCCESS;
}

int seekdb_stmt_bind_param(SeekdbStmt stmt, SeekdbBind* bind) {
    if (!stmt || !bind) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->prepared) {
        stmt_data->last_error = "Statement not prepared";
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Copy bind structures
    for (unsigned long i = 0; i < stmt_data->param_count; i++) {
        if (i < stmt_data->param_binds.size()) {
            stmt_data->param_binds[i] = bind[i];
        }
    }
    
    return SEEKDB_SUCCESS;
}

int seekdb_stmt_execute(SeekdbStmt stmt) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    SeekdbConnection* conn = stmt_data->conn;
    
    if (!stmt_data->prepared) {
        stmt_data->last_error = "Statement not prepared";
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (!conn || !conn->initialized) {
        stmt_data->last_error = "Connection not initialized";
        return SEEKDB_ERROR_NOT_INITIALIZED;
    }
    
    // Build SQL with parameter substitution (no SQL parsing; format from bind type, like MySQL binary protocol)
    // - SEEKDB_TYPE_BLOB: hex (0x...); SEEKDB_TYPE_STRING: quoted '...'
    // - SEEKDB_TYPE_VARBINARY_ID: _id placeholder, right-pad/truncate to 512 bytes then hex (caller sets type)
    std::string final_sql = stmt_data->sql;
    size_t param_idx = 0;
    
    // Replace ? placeholders with actual values
    for (size_t i = 0; i < final_sql.length() && param_idx < stmt_data->param_binds.size(); i++) {
        if (final_sql[i] == '?') {
            const SeekdbBind& bind = stmt_data->param_binds[param_idx];
            std::string param_value;
            
            if (bind.is_null && *bind.is_null) {
                param_value = "NULL";
            } else {
                switch (bind.buffer_type) {
                    case SEEKDB_TYPE_TINY:
                        if (bind.buffer) {
                            int8_t val = *static_cast<int8_t*>(bind.buffer);
                            param_value = std::to_string(val);
                        }
                        break;
                    case SEEKDB_TYPE_SHORT:
                        if (bind.buffer) {
                            int16_t val = *static_cast<int16_t*>(bind.buffer);
                            param_value = std::to_string(val);
                        }
                        break;
                    case SEEKDB_TYPE_LONG:
                        if (bind.buffer) {
                            int32_t val = *static_cast<int32_t*>(bind.buffer);
                            param_value = std::to_string(val);
                        }
                        break;
                    case SEEKDB_TYPE_LONGLONG:
                        if (bind.buffer) {
                            int64_t val = *static_cast<int64_t*>(bind.buffer);
                            param_value = std::to_string(val);
                        }
                        break;
                    case SEEKDB_TYPE_FLOAT:
                        if (bind.buffer) {
                            float val = *static_cast<float*>(bind.buffer);
                            param_value = std::to_string(val);
                        }
                        break;
                    case SEEKDB_TYPE_DOUBLE:
                        if (bind.buffer) {
                            double val = *static_cast<double*>(bind.buffer);
                            param_value = std::to_string(val);
                        }
                        break;
                    case SEEKDB_TYPE_STRING:
                        if (bind.buffer && bind.length) {
                            const std::string str_val(static_cast<const char*>(bind.buffer), *bind.length);
                            param_value = seekdb_format_string_bind_param(conn, stmt_data, param_idx, str_val);
                        }
                        break;
                    case SEEKDB_TYPE_BLOB:
                        // BLOB type: use hexadecimal format (aligned with MySQL MYSQL_TYPE_BLOB)
                        // BLOB type indicates binary data that should not undergo charset conversion
                        if (bind.buffer && bind.length && *bind.length > 0) {
                            size_t hex_len = *bind.length * 2 + 1;
                            std::vector<char> hex_buf(hex_len);
                            
                            unsigned long hex_length = seekdb_hex_string(
                                hex_buf.data(),
                                static_cast<unsigned long>(hex_len),
                                static_cast<const char*>(bind.buffer),
                                *bind.length
                            );
                            
                            if (hex_length != static_cast<unsigned long>(-1)) {
                                param_value = "0x" + std::string(hex_buf.data(), hex_length);
                            } else {
                                param_value = "NULL";
                            }
                        } else {
                            param_value = "NULL";
                        }
                        break;
                    case SEEKDB_TYPE_VARBINARY_ID:
                        // VARBINARY(512) _id: right-pad or truncate to 512 bytes, output as 0x hex (no SQL parsing)
                        if (bind.buffer && bind.length) {
                            size_t data_len = *bind.length;
                            size_t copy_len = (data_len > VARBINARY_ID_LENGTH) ? VARBINARY_ID_LENGTH : data_len;
                            std::vector<char> padded(VARBINARY_ID_LENGTH, 0);
                            if (copy_len > 0) {
                                memcpy(padded.data(), bind.buffer, copy_len);
                            }
                            size_t hex_len = VARBINARY_ID_LENGTH * 2 + 1;
                            std::vector<char> hex_buf(hex_len);
                            unsigned long hex_length = seekdb_hex_string(
                                hex_buf.data(),
                                static_cast<unsigned long>(hex_len),
                                padded.data(),
                                VARBINARY_ID_LENGTH
                            );
                            if (hex_length != static_cast<unsigned long>(-1)) {
                                param_value = "0x" + std::string(hex_buf.data(), hex_length);
                            } else {
                                param_value = "NULL";
                            }
                        } else {
                            param_value = "NULL";
                        }
                        break;
                    case SEEKDB_TYPE_VECTOR:
                        // VECTOR: treat as string per official example (INSERT '[1.2,0.7,1.1]').
                        // Input: JSON array string from bind (e.g. "[1.1,2.2,3.3]"); embed in SQL as-is.
                        // Read: C ABI returns JSON string via seekdb_row_get_string() (binary→JSON, no rounding).
                        if (bind.buffer && bind.length && *bind.length > 0) {
                            std::string vec_val(static_cast<const char*>(bind.buffer), *bind.length);
                            // Remove surrounding quotes if present (e.g., "'[1,2,3]'" -> "[1,2,3]")
                            // VECTOR values in SQL should be JSON array literals without quotes
                            if (vec_val.length() >= 2 && 
                                ((vec_val[0] == '\'' && vec_val[vec_val.length()-1] == '\'') ||
                                 (vec_val[0] == '"' && vec_val[vec_val.length()-1] == '"'))) {
                                vec_val = vec_val.substr(1, vec_val.length() - 2);
                            }
                            // Embed the JSON array directly (e.g., [1,2,3])
                            param_value = vec_val;
                        } else {
                            param_value = "NULL";
                        }
                        break;
                    default:
                        param_value = "NULL";
                        break;
                }
            }
            
            final_sql.replace(i, 1, param_value);
            i += param_value.length() - 1;
            param_idx++;
        }
    }
    if (is_write_sql(final_sql.c_str())) {
        int64_t affected_rows = 0;
        int ret = seekdb_execute_update(static_cast<SeekdbHandle>(conn), final_sql.c_str(), &affected_rows);
        if (ret != SEEKDB_SUCCESS) {
            stmt_data->last_error = conn->last_error;
            return ret;
        }
        if (stmt_data->result_set) {
            delete stmt_data->result_set;
            stmt_data->result_set = nullptr;
        }
        stmt_data->executed = true;
        return SEEKDB_SUCCESS;
    }

    // Execute the final SQL
    SeekdbResult result = nullptr;
    int ret = seekdb_query(conn, final_sql.c_str(), &result);
    
    if (ret != SEEKDB_SUCCESS) {
        stmt_data->last_error = conn->last_error;
        return ret;
    }
    
    // Get result from connection (seekdb_query stores it in conn->last_result_set)
    // Transfer ownership from connection to statement to prevent double-free
    SeekdbResultSet* result_set = nullptr;
    if (conn->last_result_set) {
        result_set = conn->last_result_set;
        conn->last_result_set = nullptr;  // Transfer ownership to statement
        result_set->owner_conn = nullptr;  // Clear owner reference
    } else if (result) {
        // Fallback: use result directly if last_result_set is null
        result_set = static_cast<SeekdbResultSet*>(result);
        if (result_set) {
            result_set->owner_conn = nullptr;  // Clear owner reference
        }
    }
    
    if (!result_set) {
        stmt_data->last_error = "No result set returned";
        return SEEKDB_ERROR_QUERY_FAILED;
    }
    
    // Store result set (free previous one if exists)
    if (stmt_data->result_set) {
        delete stmt_data->result_set;
    }
    stmt_data->result_set = result_set;
    stmt_data->executed = true;
    
    return SEEKDB_SUCCESS;
}

int seekdb_stmt_bind_result(SeekdbStmt stmt, SeekdbBind* bind) {
    if (!stmt || !bind) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        stmt_data->last_error = "Statement not executed or no result set";
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Store result bind structures
    stmt_data->result_count = static_cast<unsigned long>(stmt_data->result_set->column_count);
    stmt_data->result_binds.resize(stmt_data->result_count);
    
    for (unsigned long i = 0; i < stmt_data->result_count; i++) {
        if (i < stmt_data->result_binds.size()) {
            stmt_data->result_binds[i] = bind[i];
        }
    }
    
    return SEEKDB_SUCCESS;
}

// Helper function to convert string value to appropriate type based on bind buffer type
static void convert_and_copy_value(const std::string& value, SeekdbBind& bind) {
    if (value.empty()) {
        // NULL value
        if (bind.is_null) {
            *bind.is_null = true;
        }
        if (bind.length) {
            *bind.length = 0;
        }
        return;
    }
    
    // Non-NULL value
    if (bind.is_null) {
        *bind.is_null = false;
    }
    
    if (!bind.buffer) {
        // No buffer provided, just set length
        if (bind.length) {
            *bind.length = static_cast<unsigned long>(value.length());
        }
        return;
    }
    
    // Convert and copy based on buffer type
    switch (bind.buffer_type) {
        case SEEKDB_TYPE_TINY:
        case SEEKDB_TYPE_SHORT:
        case SEEKDB_TYPE_LONG:
        case SEEKDB_TYPE_LONGLONG: {
            // Integer types: parse string to integer
            int64_t int_val = 0;
            try {
                int_val = std::stoll(value);
            } catch (...) {
                // Invalid integer - set to 0
                int_val = 0;
            }
            
            // Write to buffer based on type size
            if (bind.buffer_type == SEEKDB_TYPE_TINY) {
                *static_cast<int8_t*>(bind.buffer) = static_cast<int8_t>(int_val);
            } else if (bind.buffer_type == SEEKDB_TYPE_SHORT) {
                *static_cast<int16_t*>(bind.buffer) = static_cast<int16_t>(int_val);
            } else if (bind.buffer_type == SEEKDB_TYPE_LONG) {
                *static_cast<int32_t*>(bind.buffer) = static_cast<int32_t>(int_val);
            } else { // SEEKDB_TYPE_LONGLONG
                *static_cast<int64_t*>(bind.buffer) = int_val;
            }
            
            if (bind.length) {
                *bind.length = sizeof(int64_t);
            }
            break;
        }
        case SEEKDB_TYPE_FLOAT:
        case SEEKDB_TYPE_DOUBLE: {
            // Floating point types: parse string to float/double
            double double_val = 0.0;
            try {
                double_val = std::stod(value);
            } catch (...) {
                double_val = 0.0;
            }
            
            if (bind.buffer_type == SEEKDB_TYPE_FLOAT) {
                *static_cast<float*>(bind.buffer) = static_cast<float>(double_val);
            } else { // SEEKDB_TYPE_DOUBLE
                *static_cast<double*>(bind.buffer) = double_val;
            }
            
            if (bind.length) {
                *bind.length = (bind.buffer_type == SEEKDB_TYPE_FLOAT) ? sizeof(float) : sizeof(double);
            }
            break;
        }
        case SEEKDB_TYPE_STRING:
        default: {
            // String types: copy as string
            size_t data_len = value.length();
            if (bind.length) {
                *bind.length = static_cast<unsigned long>(data_len);
            }
            
            if (bind.buffer_length > 0) {
                size_t copy_len = std::min(data_len, static_cast<size_t>(bind.buffer_length - 1));
                memcpy(bind.buffer, value.c_str(), copy_len);
                static_cast<char*>(bind.buffer)[copy_len] = '\0';
            }
            break;
        }
    }
}

int seekdb_stmt_fetch(SeekdbStmt stmt) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return 1; // No more rows
    }
    
    SeekdbResultSet* rs = stmt_data->result_set;
    rs->current_row++;
    
    if (rs->current_row >= rs->row_count) {
        return 1; // No more rows
    }
    
    // Validate bounds before accessing rows
    if (rs->current_row < 0 || rs->current_row >= static_cast<int64_t>(rs->rows.size())) {
        return 1; // Invalid row index
    }
    
    // Copy data to result bind buffers
    if (!stmt_data->result_binds.empty()) {
        const std::vector<std::string>& row = rs->rows[rs->current_row];
        
        for (unsigned long i = 0; i < stmt_data->result_count && i < row.size(); i++) {
            convert_and_copy_value(row[i], stmt_data->result_binds[i]);
        }
    }
    
    return 0; // Success
}

SeekdbResult seekdb_stmt_result_metadata(SeekdbStmt stmt) {
    if (!stmt) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return nullptr;
    }
    
    return static_cast<SeekdbResult>(stmt_data->result_set);
}

int seekdb_stmt_store_result(SeekdbStmt stmt) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        stmt_data->last_error = "Statement not executed or no result set";
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Result set is already stored in stmt_data->result_set after execution
    // This function is mainly for API compatibility with mysql_stmt_store_result()
    // The result set is already buffered, so we just need to ensure it's accessible
    
    // Reset current_row to beginning for sequential access
    SeekdbResultSet* rs = stmt_data->result_set;
    if (rs) {
        rs->current_row = -1;  // Reset to before first row
    }
    
    return SEEKDB_SUCCESS;
}

unsigned long seekdb_stmt_param_count(SeekdbStmt stmt) {
    if (!stmt) {
        return 0;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    return stmt_data->param_count;
}

my_ulonglong seekdb_stmt_affected_rows(SeekdbStmt stmt) {
    if (!stmt) {
        return 0;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    SeekdbConnection* conn = stmt_data->conn;
    
    if (!conn) {
        return 0;
    }
    
    return seekdb_affected_rows(conn);
}

my_ulonglong seekdb_stmt_insert_id(SeekdbStmt stmt) {
    if (!stmt) {
        return 0;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    SeekdbConnection* conn = stmt_data->conn;
    
    if (!conn) {
        return 0;
    }
    
    return seekdb_insert_id(conn);
}

my_ulonglong seekdb_stmt_num_rows(SeekdbStmt stmt) {
    if (!stmt) {
        return 0;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return 0;
    }
    
    return static_cast<my_ulonglong>(stmt_data->result_set->row_count);
}

int seekdb_stmt_free_result(SeekdbStmt stmt) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (stmt_data->result_set) {
        delete stmt_data->result_set;
        stmt_data->result_set = nullptr;
    }
    
    stmt_data->executed = false;
    return SEEKDB_SUCCESS;
}

void seekdb_stmt_close(SeekdbStmt stmt) {
    if (stmt) {
        SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
        delete stmt_data;
    }
}

const char* seekdb_stmt_error(SeekdbStmt stmt) {
    if (!stmt) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (stmt_data->last_error.empty()) {
        return nullptr;
    }
    
    return stmt_data->last_error.c_str();
}

unsigned int seekdb_stmt_errno(SeekdbStmt stmt) {
    if (!stmt) {
        return 0;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    SeekdbConnection* conn = stmt_data->conn;
    
    if (!conn) {
        return 0;
    }
    
    return seekdb_errno(conn);
}

const char* seekdb_stmt_sqlstate(SeekdbStmt stmt) {
    if (!stmt) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    SeekdbConnection* conn = stmt_data->conn;
    
    if (!conn) {
        return nullptr;
    }
    
    // Get SQLSTATE from connection (MySQL compatibility)
    // For now, return a generic SQLSTATE based on error code
    // In a full implementation, this would map error codes to SQLSTATE values
    const char* error = seekdb_error(conn);
    if (!error) {
        return nullptr;
    }
    
    // Map common error codes to SQLSTATE
    // This is a simplified implementation
    // A full implementation would use a comprehensive error code to SQLSTATE mapping
    unsigned int errno_val = seekdb_errno(conn);
    
    // Return a static SQLSTATE string based on error type
    // SQLSTATE "42000" = syntax error or access rule violation
    // SQLSTATE "HY000" = general error
    static const char* sqlstate_42000 = "42000";  // Syntax error
    static const char* sqlstate_hy000 = "HY000";  // General error
    
    // For now, return generic SQLSTATE
    // In a full implementation, this would be based on actual error codes
    return sqlstate_hy000;
}

int seekdb_stmt_reset(SeekdbStmt stmt) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    // Reset execution state
    stmt_data->executed = false;
    
    // Clear result set
    if (stmt_data->result_set) {
        delete stmt_data->result_set;
        stmt_data->result_set = nullptr;
    }
    
    // Clear error state
    stmt_data->last_error.clear();
    
    // Clear result binds
    stmt_data->result_binds.clear();
    stmt_data->result_count = 0;
    
    // Note: Parameter binds are kept, so the statement can be re-executed
    // with the same or new parameters
    
    return SEEKDB_SUCCESS;
}

unsigned int seekdb_stmt_field_count(SeekdbStmt stmt) {
    if (!stmt) {
        return 0;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return 0;
    }
    
    return static_cast<unsigned int>(stmt_data->result_set->column_count);
}

int seekdb_stmt_data_seek(SeekdbStmt stmt, my_ulonglong offset) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = stmt_data->result_set;
    
    if (offset >= static_cast<my_ulonglong>(rs->row_count)) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    // Set current row position
    rs->current_row = static_cast<int64_t>(offset) - 1; // Will be incremented on next fetch
    
    return SEEKDB_SUCCESS;
}

SeekdbRow seekdb_stmt_row_seek(SeekdbStmt stmt, SeekdbRow row) {
    if (!stmt || !row) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = stmt_data->result_set;
    SeekdbRowData* row_data = static_cast<SeekdbRowData*>(row);
    
    // Verify row belongs to this result set
    if (row_data->result_set != rs) {
        return nullptr;
    }
    
    // Set current row position
    rs->current_row = row_data->row_index - 1; // Will be incremented on next fetch
    
    // Return the row handle (can be used for future seeks)
    return row;
}

SeekdbRow seekdb_stmt_row_tell(SeekdbStmt stmt) {
    if (!stmt) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return nullptr;
    }
    
    SeekdbResultSet* rs = stmt_data->result_set;
    
    // Get current row position
    int64_t current_pos = rs->current_row + 1; // current_row is -1 before first fetch
    
    if (current_pos < 0 || current_pos >= rs->row_count) {
        return nullptr;
    }
    
    // Reuse a single current_row_data (row is borrowed until next fetch or result_free).
    SeekdbRowData* row_data = rs->current_row_data;
    if (!row_data) {
        row_data = new SeekdbRowData(rs, current_pos);
        rs->current_row_data = row_data;
    } else {
        row_data->row_index = current_pos;
    }
    return static_cast<SeekdbRow>(row_data);
}

int seekdb_stmt_fetch_column(SeekdbStmt stmt, SeekdbBind* bind, unsigned int column_index, unsigned long offset) {
    if (!stmt || !bind) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->executed || !stmt_data->result_set) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbResultSet* rs = stmt_data->result_set;
    
    // Check if we have a current row
    if (rs->current_row < 0 || rs->current_row >= rs->row_count) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (column_index >= static_cast<unsigned int>(rs->column_count)) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    if (rs->current_row >= static_cast<int64_t>(rs->rows.size())) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    const std::vector<std::string>& row = rs->rows[rs->current_row];
    
    if (column_index >= row.size()) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    const std::string& cell_value = row[column_index];
    
    if (cell_value.empty()) {
        // NULL value
        if (bind->is_null) {
            *bind->is_null = true;
        }
        if (bind->length) {
            *bind->length = 0;
        }
        return SEEKDB_SUCCESS;
    }
    
    // Non-NULL value
    if (bind->is_null) {
        *bind->is_null = false;
    }
    
    size_t data_len = cell_value.length();
    size_t copy_len = data_len;
    
    // Handle offset (for partial fetch)
    if (offset > 0) {
        if (offset >= data_len) {
            // Offset beyond data length
            if (bind->length) {
                *bind->length = 0;
            }
            return SEEKDB_SUCCESS;
        }
        copy_len = data_len - offset;
    }
    
    if (bind->length) {
        *bind->length = static_cast<unsigned long>(copy_len);
    }
    
    if (bind->buffer && bind->buffer_length > 0) {
        size_t buffer_copy_len = std::min(copy_len, static_cast<size_t>(bind->buffer_length - 1));
        memcpy(bind->buffer, cell_value.c_str() + offset, buffer_copy_len);
        static_cast<char*>(bind->buffer)[buffer_copy_len] = '\0';
    }
    
    return SEEKDB_SUCCESS;
}

SeekdbResult seekdb_stmt_param_metadata(SeekdbStmt stmt) {
    if (!stmt) {
        return nullptr;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    if (!stmt_data->prepared) {
        return nullptr;
    }
    
    // Create a result set with parameter metadata
    // Each row represents one parameter with metadata information
    SeekdbResultSet* result_set = new (std::nothrow) SeekdbResultSet();
    if (!result_set) {
        return nullptr;
    }
    
    // Set column names for parameter metadata result set
    // Similar to MySQL's parameter metadata format
    result_set->column_names.push_back("name");           // Parameter name (if named)
    result_set->column_names.push_back("type");           // Parameter type
    result_set->column_names.push_back("length");         // Parameter length
    result_set->column_names.push_back("flags");          // Parameter flags
    result_set->column_names.push_back("decimals");       // Decimal places
    result_set->column_names.push_back("charsetnr");      // Character set number
    result_set->column_count = 6;
    
    // Create rows for each parameter
    // We use param_column_types if available (from table schema), otherwise use bind types
    for (unsigned long i = 0; i < stmt_data->param_count; i++) {
        std::vector<std::string> row;
        
        // Parameter name (unnamed parameters use "?")
        row.push_back("?");
        
        // Parameter type
        oceanbase::common::ObObjType param_type = oceanbase::common::ObNullType;
        if (i < stmt_data->param_column_types.size()) {
            param_type = stmt_data->param_column_types[i];
        } else if (i < stmt_data->param_binds.size()) {
            // Fallback to bind type if column type not available
            SeekdbFieldType bind_type = stmt_data->param_binds[i].buffer_type;
            // Convert SeekdbFieldType to ObObjType (simplified mapping)
            switch (bind_type) {
                case SEEKDB_TYPE_LONG:
                    param_type = oceanbase::common::ObIntType;
                    break;
                case SEEKDB_TYPE_DOUBLE:
                    param_type = oceanbase::common::ObDoubleType;
                    break;
                case SEEKDB_TYPE_STRING:
                case SEEKDB_TYPE_VARBINARY_ID:
                    param_type = oceanbase::common::ObVarcharType;
                    break;
                case SEEKDB_TYPE_VECTOR:
                    param_type = oceanbase::common::ObCollectionSQLType;
                    break;
                default:
                    param_type = oceanbase::common::ObNullType;
                    break;
            }
        }
        row.push_back(std::to_string(static_cast<int>(param_type)));
        
        // Parameter length (from bind if available)
        unsigned long param_length = 0;
        if (i < stmt_data->param_binds.size()) {
            param_length = stmt_data->param_binds[i].buffer_length;
        }
        row.push_back(std::to_string(param_length));
        
        // Parameter flags (0 for now, can be extended)
        row.push_back("0");
        
        // Decimal places (0 for non-decimal types)
        row.push_back("0");
        
        // Character set number (0 for non-string types)
        row.push_back("0");
        
        result_set->rows.push_back(row);
        result_set->row_nulls.push_back(std::vector<bool>(6, false));  // param metadata: no NULL cells
    }
    
    result_set->row_count = static_cast<int64_t>(result_set->rows.size());
    result_set->current_row = -1;
    
    return static_cast<SeekdbResult>(result_set);
}

int seekdb_stmt_next_result(SeekdbStmt stmt) {
    if (!stmt) {
        return SEEKDB_ERROR_INVALID_PARAM;
    }
    
    SeekdbStmtData* stmt_data = static_cast<SeekdbStmtData*>(stmt);
    
    // For now, we don't support multiple result sets from a single prepared statement
    // This would require support for stored procedures or multi-statement queries
    // Return -1 to indicate no more results
    
    // TODO: Implement multiple result set support
    // This would require tracking multiple result sets from a single execution
    
    return -1; // No more results
}

} // extern "C"

