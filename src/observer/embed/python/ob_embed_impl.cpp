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
#define USING_LOG_PREFIX SERVER
#include <pybind11/stl.h>
#include <memory>
#include "observer/embed/python/ob_embed_impl.h"
#include "observer/ob_server.h"
#include "rpc/obrpc/ob_net_client.h"
#include "observer/ob_inner_sql_result.h"
#include "observer/ob_server_options.h"
#include "lib/string/ob_string.h"
#include "common/ob_version_def.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "sql/engine/expr/ob_expr_sql_udt_utils.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "lib/timezone/ob_time_convert.h"
#include "lib/charset/ob_charset.h"

PYBIND11_MODULE(pyseekdb, m) {
    m.doc() = "OceanBase SeekDB";
    char embed_version_str[oceanbase::common::OB_SERVER_VERSION_LENGTH];
    oceanbase::common::VersionUtil::print_version_str(embed_version_str, sizeof(embed_version_str), DATA_CURRENT_VERSION);
    m.attr("__version__") = embed_version_str;

    const char *default_service_path = "./seekdb";
    m.def("open", &oceanbase::embed::ObLiteEmbed::open, pybind11::arg("db_dir") = default_service_path, "open db");
    m.def("_open_with_service", &oceanbase::embed::ObLiteEmbed::open_with_service, pybind11::arg("db_dir") = default_service_path,
                                                  pybind11::arg("port") = 2881,
                                                 "open db");

    m.def("connect", &oceanbase::embed::ObLiteEmbed::connect, pybind11::arg("database") = "test",
                                                        pybind11::arg("autocommit") = false,
                                                       "connect seekdb");

    pybind11::class_<oceanbase::embed::ObLiteEmbedConn,
                     std::shared_ptr<oceanbase::embed::ObLiteEmbedConn>>(m, "Connection")
        .def(pybind11::init<>())
        .def("cursor", &oceanbase::embed::ObLiteEmbedConn::cursor)
        .def("close", &oceanbase::embed::ObLiteEmbedConn::reset)
        .def("begin", &oceanbase::embed::ObLiteEmbedConn::begin, pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("commit", &oceanbase::embed::ObLiteEmbedConn::commit, pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("rollback", &oceanbase::embed::ObLiteEmbedConn::rollback, pybind11::call_guard<pybind11::gil_scoped_release>());

    pybind11::class_<oceanbase::embed::ObLiteEmbedCursor>(m, "Cursor")
        .def("execute", &oceanbase::embed::ObLiteEmbedCursor::execute, pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("fetchone", &oceanbase::embed::ObLiteEmbedCursor::fetchone)
        .def("fetchall", &oceanbase::embed::ObLiteEmbedCursor::fetchall)
        .def("close", &oceanbase::embed::ObLiteEmbedCursor::close);

    pybind11::object atexit = pybind11::module::import("atexit");
    atexit.attr("register")(pybind11::cpp_function(oceanbase::embed::ObLiteEmbed::close));
}

namespace oceanbase
{
namespace embed
{

using namespace oceanbase::common;
using namespace oceanbase::observer;

static pybind11::object decimal_module = pybind11::module::import("decimal");
static pybind11::object decimal_class = decimal_module.attr("Decimal");
static pybind11::object datetime_module = pybind11::module::import("datetime");
static pybind11::object datetime_class = datetime_module.attr("datetime");
static pybind11::object fromtimestamp = datetime_class.attr("fromtimestamp");
static pybind11::object utcfromtimestamp = datetime_class.attr("utcfromtimestamp");
static pybind11::object timedelta_class = datetime_module.attr("timedelta");
static pybind11::object date_class = datetime_module.attr("date");
static pybind11::module builtins = pybind11::module::import("builtins");

#define MPRINT(format, ...) fprintf(stderr, "[seekdb] " format "\n", ##__VA_ARGS__)

ObSqlString pid_file_name;
bool pid_locked = false;

static int to_absolute_path(const char *cwd, ObSqlString &dir)
{
  int ret = OB_SUCCESS;
  if (!dir.empty() && dir.ptr()[0] != '\0' && dir.ptr()[0] != '/') {
    char abs_path[OB_MAX_FILE_NAME_LENGTH] = {0};
    // realpath will fail if the directory does not exist, so construct absolute path manually
    if (snprintf(abs_path, sizeof(abs_path), "%s/%s", cwd, dir.ptr()) >= static_cast<int>(sizeof(abs_path))) {
        MPRINT("Absolute path is too long.");
        ret = OB_SIZE_OVERFLOW;
    } else if (OB_FAIL(dir.assign(abs_path))) {
      MPRINT("[Maybe Memory Error] Failed to assign absolute path. Please try again.");
    }
  }
  return ret;
}

void ObLiteEmbed::open(const char* db_dir)
{
  open_inner(db_dir, 0);
}

void ObLiteEmbed::open_with_service(const char* db_dir, const int64_t port)
{
  open_inner(db_dir, port);
}

void ObLiteEmbed::open_inner(const char* db_dir, const int64_t port)
{
  int ret = OB_SUCCESS;
  if (GCTX.is_inited()) {
    MPRINT("seekdb has opened");
    ret = OB_INIT_TWICE;
  } else {
    size_t stack_size = 1LL<<20;
    void *stack_addr = ::mmap(nullptr, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (MAP_FAILED == stack_addr) {
      ret = OB_ERR_UNEXPECTED;
      MPRINT("mmap failed");
    } else {
      ret = CALL_WITH_NEW_STACK(do_open_(db_dir, port), stack_addr, stack_size);
      if (-1 == ::munmap(stack_addr, stack_size)) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("open seekdb failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + std::string(ob_errpkt_strerror(ret, false)));
  }
  // TODO promise service ready
  omt::ObTenantNodeBalancer::get_instance().handle();
}

int ObLiteEmbed::do_open_(const char* db_dir, int64_t port)
{
  int ret = OB_SUCCESS;
  ObServerOptions opts;
  opts.embed_mode_ = true;
  opts.port_ = 2881;
  if (port > 0) {
    opts.port_ = port;
    opts.embed_mode_ = false;
  }
  opts.use_ipv6_ = false;
  opts.parameters_.push_back(std::make_pair(common::ObString("memory_limit"), common::ObString("1G")));

  char buffer[PATH_MAX];
  ObSqlString work_abs_dir;
  ObSqlString slog_dir;
  ObSqlString sstable_dir;
  int64_t start_time = ObTimeUtility::current_time();

  ObWarningBuffer::set_warn_log_on(true);
  if (getcwd(buffer, sizeof(buffer)) == nullptr) {
    MPRINT("getcwd failed %d %s", errno, strerror(errno));
  } else if (FALSE_IT(work_abs_dir.assign(buffer))) {
  } else if (OB_FAIL(opts.base_dir_.assign(db_dir))) {
    MPRINT("assign base dir failed %d", ret);
  } else if (OB_FAIL(opts.data_dir_.assign_fmt("%s/store", opts.base_dir_.ptr()))) {
    MPRINT("assign data dir failed %d", ret);
  } else if (OB_FAIL(opts.redo_dir_.assign_fmt("%s/store/redo", opts.data_dir_.ptr()))) {
    MPRINT("assign redo dir failed %d", ret);
  } else if (OB_FAIL(to_absolute_path(work_abs_dir.ptr(), opts.base_dir_))) {
    MPRINT("get base dir absolute path failed %d", ret);
  } else if (OB_FAIL(to_absolute_path(work_abs_dir.ptr(), opts.data_dir_))) {
    MPRINT("get data dir absolute path failed %d", ret);
  } else if (OB_FAIL(to_absolute_path(work_abs_dir.ptr(), opts.redo_dir_))) {
    MPRINT("get redo dir absolute path failed %d", ret);
  } else if (OB_FAIL(pid_file_name.assign_fmt("%s/run/seekdb.pid", opts.base_dir_.ptr()))) {
    MPRINT("get pidfile absolute path failed %d", ret);
  }

  struct statfs fs_info;
  const long TMPFS_MAGIC = 0x01021994;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.base_dir_.ptr()))) {
    MPRINT("create base dir failed %d, directory: %s", ret, opts.base_dir_.ptr());
  } else if (statfs(opts.base_dir_.ptr(), &fs_info) != 0) {
    ret = OB_ERR_UNEXPECTED;
    MPRINT("stat base dir failed %s, directory: %s", strerror(errno), opts.base_dir_.ptr());
  } else if (fs_info.f_type == TMPFS_MAGIC) {
    ret = OB_NOT_SUPPORTED;
    MPRINT("not support tmpfs directory: %s", opts.base_dir_.ptr());
  } else if (-1 == chdir(opts.base_dir_.ptr())) {
    ret = OB_ERR_UNEXPECTED;
    MPRINT("change dir failed %s, directory: %s", strerror(errno), opts.base_dir_.ptr());
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.data_dir_.ptr()))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(opts.redo_dir_.ptr()))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(slog_dir.assign_fmt("%s/slog", opts.data_dir_.ptr())) ||
             OB_FAIL(sstable_dir.assign_fmt("%s/sstable", opts.data_dir_.ptr()))) {
    MPRINT("calculate slog and sstable dir failed %d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(slog_dir.ptr()))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path(sstable_dir.ptr()))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path("./run"))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path("./etc"))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(FileDirectoryUtils::create_full_path("./log"))) {
    MPRINT("create dir failed %d", ret);
  } else if (OB_FAIL(start_daemon(pid_file_name.ptr(), true))) {
    MPRINT("db %s opened by other process", db_dir);
  } else if (FALSE_IT(pid_locked = true)) {
  } else {
    OB_LOGGER.set_log_level("INFO");
    ObSqlString log_file;
    if (OB_FAIL(log_file.assign_fmt("%s/log/seekdb.log", opts.base_dir_.ptr()))) {
      MPRINT("calculate log file failed %d", ret);
    } else {
      OB_LOGGER.set_file_name(log_file.ptr(), true, false);
    }

    int saved_stdout = dup(STDOUT_FILENO); // Save current stdout
    dup2(OB_LOGGER.get_svr_log().fd_, STDOUT_FILENO);

    ObPLogWriterCfg log_cfg;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(OBSERVER.init(opts, log_cfg))) {
      LOG_WARN("observer init failed", KR(ret));
    } else if (OB_FAIL(OBSERVER.start(opts.embed_mode_))) {
      LOG_WARN("observer start failed", KR(ret));
    } else if (-1 == chdir(work_abs_dir.ptr())) {
      ret = OB_ERR_UNEXPECTED;
      MPRINT("change dir failed %s, directory: %s", strerror(errno), work_abs_dir.ptr());
    } else {
      FLOG_INFO("observer start finish wait service ", "cost", ObTimeUtility::current_time() - start_time);
      while (true) {
        if (GCTX.root_service_->is_full_service()) {
          break;
        } else {
          ob_usleep(100 * 1000);
        }
      }
      FLOG_INFO("seekdb start success ", "cost", ObTimeUtility::current_time()-start_time);
    }
    dup2(saved_stdout, STDOUT_FILENO);
  }
  return ret;
}

void ObLiteEmbed::close()
{
  //OBSERVER.set_stop();
  //th_.join();
  if (pid_locked) {
    unlink(pid_file_name.ptr());
  }
  FLOG_INFO("seekdb close");
  _Exit(0);
}

std::string handle_err_msg(int ret)
{
  std::string errmsg;
  if (OB_FAIL(ret)) {
    const common::ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer();
    if (nullptr != wb) {
      if (wb->get_err_code() == ret ||
          (ret >= OB_MIN_RAISE_APPLICATION_ERROR && ret <= OB_MAX_RAISE_APPLICATION_ERROR)) {
        if (wb->get_err_msg() != nullptr && wb->get_err_msg()[0] != '\0') {
          errmsg = std::string(wb->get_err_msg());
        }
      }
    }
    if (errmsg.empty()) {
      errmsg = std::string(ob_errpkt_strerror(ret, false));
    }
  }
  return errmsg;
}

std::shared_ptr<ObLiteEmbedConn> ObLiteEmbed::connect(const char* db_name, const bool autocommit)
{
  int ret = OB_SUCCESS;
  std::shared_ptr<ObLiteEmbedConn> embed_conn = std::make_shared<ObLiteEmbedConn>();
  common::sqlclient::ObISQLConnection *inner_conn = nullptr;
  uint32_t sid = sql::ObSQLSessionInfo::INVALID_SESSID;
  sql::ObSQLSessionInfo *session = NULL;
  const ObUserInfo* user_info = NULL;
  ObSchemaGetterGuard schema_guard;
  ObPrivSet db_priv_set = OB_PRIV_SET_EMPTY;
  int64_t start_time = ObTimeUtility::current_time();
  const ObDatabaseSchema *database_schema = nullptr;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("db not init", KR(ret));
  } else if (OB_FAIL(GCTX.session_mgr_->create_sessid(sid))) {
    LOG_WARN("Failed to create sess id", KR(ret));
  } else if (OB_FAIL(GCTX.session_mgr_->create_session(OB_SYS_TENANT_ID, sid, 0, ObTimeUtility::current_time(), session))) {
    GCTX.session_mgr_->mark_sessid_unused(sid);
    session = nullptr;
    LOG_WARN("Failed to create session", KR(ret), K(sid));
  } else if (FALSE_IT(common::ob_setup_tsi_warning_buffer(&session->get_warnings_buffer()))) {
  } else if (FALSE_IT(embed_conn->get_session() = session)) {
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("failed to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_user_info(OB_SYS_TENANT_ID, OB_SYS_USER_ID, user_info))) {
    LOG_WARN("failed to get user info", KR(ret));
  } else if (OB_ISNULL(user_info)) {
    ret = OB_SCHEMA_ERROR;
    LOG_WARN("schema user info is null", KR(ret));
  } else if (OB_NOT_NULL(db_name) && STRLEN(db_name) > 0) {
    if (OB_FAIL(schema_guard.get_database_schema(OB_SYS_TENANT_ID, ObString(db_name), database_schema))) {
      LOG_WARN("failed to get database", KR(ret), K(db_name));
    } else if (OB_ISNULL(database_schema)) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database is null", KR(ret), K(db_name));
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, STRLEN(db_name), db_name);
    }
  }
  if (OB_SUCC(ret)) {
    OZ (session->load_default_sys_variable(false, true));
    OZ (session->load_default_configs_in_pc());
    OZ (session->init_tenant(OB_SYS_TENANT_NAME, OB_SYS_TENANT_ID));
    OZ (session->load_all_sys_vars(schema_guard));
    if (OB_NOT_NULL(db_name) && STRLEN(db_name) > 0) {
      OZ (session->set_default_database(db_name));
    }
    OX (session->set_user_session());
    OZ (session->set_autocommit(autocommit));
    OZ (session->set_user(user_info->get_user_name_str(), user_info->get_host_name_str(), user_info->get_user_id()));
    OZ (session->set_real_client_ip_and_port("127.0.0.1", 0));
    OX (session->set_priv_user_id(user_info->get_user_id()));
    OX (session->set_user_priv_set(user_info->get_priv_set()));
    OX (session->init_use_rich_format());
    if (OB_NOT_NULL(db_name) && STRLEN(db_name) > 0) {
      OZ (schema_guard.get_db_priv_set(OB_SYS_TENANT_ID, user_info->get_user_id(), db_name, db_priv_set));
      OX (session->set_db_priv_set(db_priv_set));
    }
    OX (session->get_enable_role_array().reuse());
    for (int i = 0; OB_SUCC(ret) && i < user_info->get_role_id_array().count(); ++i) {
      if (user_info->get_disable_option(user_info->get_role_id_option_array().at(i)) == 0) {
        OZ (session->get_enable_role_array().push_back(user_info->get_role_id_array().at(i)));
      }
    }
  }
  if (FAILEDx(OBSERVER.get_inner_sql_conn_pool().acquire(session, inner_conn))) {
    LOG_WARN("acquire conn failed", KR(ret));
  } else if (FALSE_IT(embed_conn->get_conn() = static_cast<observer::ObInnerSQLConnection*>(inner_conn))) {
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("connect failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + handle_err_msg(ret));
  }
  common::ob_setup_tsi_warning_buffer(NULL);
  FLOG_INFO("connect", K(db_name), K(sid), KP(session), KPC(session), KPC(user_info), "cost", ObTimeUtility::current_time()-start_time);
  return embed_conn;
}

void ObLiteEmbedConn::reset_result()
{
  if (OB_NOT_NULL(result_)) {
    result_->close();
    result_->~ReadResult();
    ob_free(result_);
    result_ = nullptr;
  }
}

void ObLiteEmbedConn::reset()
{
  reset_result();
  // release conn
  if (OB_NOT_NULL(conn_)) {
    OBSERVER.get_inner_sql_conn_pool().release(conn_, true);
    conn_ = nullptr;
  }
  if (OB_NOT_NULL(session_)) {
    GCTX.session_mgr_->revert_session(session_);
    GCTX.session_mgr_->mark_sessid_unused(session_->get_sid());
    session_ = nullptr;
  }
}

bool ObLiteEmbedConn::need_autocommit()
{
  bool need_ac = false;
  if (OB_NOT_NULL(session_) && !session_->has_explicit_start_trans()) {
    bool ac = false;
    session_->get_autocommit(ac);
    need_ac = session_->is_in_transaction() && ac;
  }
  return need_ac;
}

int ObLiteEmbedConn::execute(const char *sql, uint64_t &affected_rows, int64_t &result_seq, std::string &errmsg)
{
  int ret = OB_SUCCESS;
  ObString sql_string(sql);
  lib::ObMemAttr mem_attr(OB_SYS_TENANT_ID, "EmbedAlloc");
  result_seq = ATOMIC_AAF(&result_seq_, 1);
  ObCurTraceId::init(GCTX.self_addr());
  int64_t start_time = ObTimeUtility::current_time();
  reset_result();
  if (OB_NOT_NULL(session_)) {
    common::ob_setup_tsi_warning_buffer(&session_->get_warnings_buffer());
  }
  if (OB_ISNULL(conn_) || OB_ISNULL(session_)) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("conn is empty", KR(ret), KP(conn_), KP(session_));
  } else if (OB_ISNULL(result_ = (common::ObCommonSqlProxy::ReadResult*)ob_malloc(sizeof(common::ObCommonSqlProxy::ReadResult), mem_attr))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc mem failed", KR(ret));
  } else if (FALSE_IT(new (result_) common::ObCommonSqlProxy::ReadResult())) {
  } else if (OB_FAIL(conn_->execute_read(OB_SYS_TENANT_ID, sql_string, *result_, true))) {
    LOG_WARN("execute sql failed", KR(ret), K(sql), K(session_->is_in_transaction()));
  } else {
    observer::ObInnerSQLResult& res = static_cast<observer::ObInnerSQLResult&>(*result_->get_result());
    if (res.result_set().get_stmt_type() == sql::stmt::T_SELECT) {
      affected_rows = UINT64_MAX;
    } else {
      affected_rows = res.result_set().get_affected_rows();
    }
    int64_t end_time = ObTimeUtility::current_time();
    FLOG_INFO("execute", K(sql), K(conn_->is_in_trans()), K(session_->is_in_transaction()), K(affected_rows), K(res.result_set().get_stmt_type()),
        "cost", end_time-start_time);
  }
  errmsg = handle_err_msg(ret);
  if (OB_NOT_NULL(session_)) {
    session_->reset_warnings_buf();
  }
  common::ob_setup_tsi_warning_buffer(NULL);
  return ret;
}


ObLiteEmbedCursor ObLiteEmbedConn::cursor()
{
  std::shared_ptr<ObLiteEmbedConn> conn = shared_from_this();
  ObLiteEmbedCursor embed_cursor;
  embed_cursor.embed_conn_ = std::move(conn);
  return embed_cursor;
}

uint64_t ObLiteEmbedCursor::execute(const char *sql)
{
  int ret = OB_SUCCESS;
  uint64_t affected_rows = 0;
  int64_t result_seq = 0;
  std::string errmsg;
  if (!embed_conn_) {
    ret = OB_CONNECT_ERROR;
  } else if (OB_FAIL(embed_conn_->execute(sql, affected_rows, result_seq, errmsg))) {
    LOG_WARN("execute sql failed", KR(ret), K(sql));
  } else {
    result_seq_ = result_seq;
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("execute sql failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + errmsg);
  }
  if (embed_conn_->need_autocommit()) {
    embed_conn_->commit();
  }
  return affected_rows;
}

void ObLiteEmbedCursor::reset()
{
  if (embed_conn_ && result_seq_ > 0 &&
      embed_conn_->get_result_seq() == result_seq_) {
    embed_conn_->reset_result();
  }
  embed_conn_.reset();
  result_seq_ = 0;
}

std::vector<pybind11::tuple> ObLiteEmbedCursor::fetchall()
{
  int ret = OB_SUCCESS;
  std::vector<pybind11::tuple> res;
  sqlclient::ObMySQLResult* mysql_result = nullptr;
  ObInnerSQLResult *inner_result = nullptr;
  if (!embed_conn_) {
    ret = OB_CONNECT_ERROR;
  } else if (OB_ISNULL(embed_conn_->get_conn())) {
    ret = OB_CONNECT_ERROR;
  } else if (OB_ISNULL(embed_conn_->get_res())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("mysql result empty", KR(ret));
  } else if (OB_ISNULL(embed_conn_->get_res()->get_result())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("mysql result empty", KR(ret));
  } else if (result_seq_ == 0 || embed_conn_->get_result_seq() != result_seq_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("result err", KR(ret), K(result_seq_), K(embed_conn_->get_result_seq()));
  } else if (FALSE_IT(mysql_result = embed_conn_->get_res()->get_result())) {
  } else if (FALSE_IT(inner_result = reinterpret_cast<ObInnerSQLResult*>(mysql_result))) {
  } else if (OB_NOT_NULL(inner_result->result_set().get_cmd())) {
    // cmd no result
  } else {
    while (OB_SUCC(ret)) {
      ret = mysql_result->next();
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        break;
      }
      int64_t column_count = mysql_result->get_column_count();
      pybind11::list row_data;
      if (column_count > 0) {
        row_data = pybind11::list();
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < column_count; i++) {
        ObObjMeta obj_meta;
        pybind11::object value;
        if (OB_FAIL(mysql_result->get_type(i, obj_meta))) {
          LOG_WARN("mysql result get obj failed", KR(ret));
        } else if (OB_FAIL(ObLiteEmbedUtil::convert_result_to_pyobj(i, *mysql_result, obj_meta, value))) {
          LOG_WARN("convert obobj to value failed ",KR(ret), K(obj_meta), K(obj_meta.get_type()));
        } else {
          //FLOG_INFO("fetchall", K(i), K(obj_meta), K(obj_meta.get_type()));
          row_data.append(value);
        }
      }
      res.push_back(pybind11::tuple(row_data));
    }
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("fetchall failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + std::string(ob_errpkt_strerror(ret, false)));
  }
  return res;
}

pybind11::object ObLiteEmbedCursor::fetchone()
{
  int ret = OB_SUCCESS;
  pybind11::list row_data;
  sqlclient::ObMySQLResult* mysql_result = nullptr;
  ObInnerSQLResult *inner_result = nullptr;
  if (!embed_conn_) {
    ret = OB_CONNECT_ERROR;
  } else if (OB_ISNULL(embed_conn_->get_conn())) {
    ret = OB_CONNECT_ERROR;
  } else if (OB_ISNULL(embed_conn_->get_res())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("mysql result empty", KR(ret));
  } else if (OB_ISNULL(embed_conn_->get_res()->get_result())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("mysql result empty", KR(ret));
  } else if (result_seq_ == 0 || embed_conn_->get_result_seq() != result_seq_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("result err", KR(ret), K(result_seq_), K(embed_conn_->get_result_seq()));
  } else if (FALSE_IT(mysql_result = embed_conn_->get_res()->get_result())) {
  } else if (FALSE_IT(inner_result = reinterpret_cast<ObInnerSQLResult*>(mysql_result))) {
  } else if (OB_NOT_NULL(inner_result->result_set().get_cmd())) {
    // cmd no result
  } else {
    ret = mysql_result->next();
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
      return pybind11::none();
    } else {
      int64_t column_count = mysql_result->get_column_count();
      for (int64_t i = 0; OB_SUCC(ret) && i < column_count; i++) {
        pybind11::object value;
        ObObjMeta obj_meta;
        if (OB_FAIL(mysql_result->get_type(i, obj_meta))) {
          LOG_WARN("mysql result get obj failed", KR(ret));
        } else if (OB_FAIL(ObLiteEmbedUtil::convert_result_to_pyobj(i, *mysql_result, obj_meta, value))) {
          LOG_WARN("convert obobj to value failed ",KR(ret), K(obj_meta), K(obj_meta.get_type()));
        } else {
          row_data.append(value);
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("fetchone failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + std::string(ob_errpkt_strerror(ret, false)));
  }
  return pybind11::tuple(row_data);
}

void ObLiteEmbedConn::begin()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn_)) {
    ret = OB_CONNECT_ERROR;
  } else if (session_->is_in_transaction()) {
    reset_result();
    conn_->set_is_in_trans(true);
    conn_->rollback();
    LOG_WARN("last trans need rollback", KP(conn_));
  }
  if (FAILEDx(conn_->start_transaction(OB_SYS_TENANT_ID))) {
    LOG_WARN("start trans failed", KR(ret));
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("begin failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + std::string(ob_errpkt_strerror(ret, false)));
  }
}

void ObLiteEmbedConn::commit()
{
  int ret = OB_SUCCESS;
  reset_result();
  if (OB_ISNULL(conn_)) {
    ret = OB_CONNECT_ERROR;
  } else if (!session_->is_in_transaction()) {
  } else if (FALSE_IT(conn_->set_is_in_trans(true))) {
  } else if (OB_FAIL(conn_->commit())) {
    LOG_WARN("commit trans failed", KR(ret));
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("commit failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + std::string(ob_errpkt_strerror(ret, false)));
  }
}

void ObLiteEmbedConn::rollback()
{
  int ret = OB_SUCCESS;
  reset_result();
  if (OB_ISNULL(conn_)) {
    ret = OB_CONNECT_ERROR;
  } else if (!session_->is_in_transaction()) {
  } else if (FALSE_IT(conn_->set_is_in_trans(true))) {
  } else if (OB_FAIL(conn_->rollback())) {
    LOG_WARN("rollback trans failed", KR(ret));
  }
  if (OB_FAIL(ret)) {
    throw std::runtime_error("rollback failed " + std::to_string(ob_errpkt_errno(ret, false)) + " " + std::string(ob_errpkt_strerror(ret, false)));
  }
}

int ObLiteEmbedUtil::convert_result_to_pyobj(const int64_t col_idx, common::sqlclient::ObMySQLResult& result, ObObjMeta& obj_meta, pybind11::object &val)
{
  int ret = OB_SUCCESS;
  lib::ObMemAttr mem_attr(OB_SYS_TENANT_ID, "EmbedAlloc");
  ObArenaAllocator allocator(mem_attr);
  ObInnerSQLResult &inner_result = reinterpret_cast<ObInnerSQLResult&>(result);
  ObObjType type = obj_meta.get_type();
  ObSQLSessionInfo &session = inner_result.result_set().get_session();
  switch (type) {
    case ObNullType: {
      val = pybind11::none();
      break;
    }
    case ObTinyIntType:
    case ObSmallIntType:
    case ObMediumIntType:
    case ObInt32Type:
    case ObIntType: {
      int64_t int_val = 0;
      if (OB_SUCC(result.get_int(col_idx, int_val))) {
        val = pybind11::int_(int_val);
      }
      break;
    }
    case ObUTinyIntType:
    case ObUSmallIntType:
    case ObUMediumIntType:
    case ObUInt32Type:
    case ObUInt64Type: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        val = pybind11::int_(obj.get_uint64());
      }
      break;
    }
    case ObFloatType: {
      float float_val = 0;
      if (OB_SUCC(result.get_float(col_idx, float_val))) {
        char buf[64];
        int len = snprintf(buf, sizeof(buf), "%.6g", float_val);
        if (len > 0 && len < sizeof(buf)) {
          std::string formatted_str(buf, len);
          val = builtins.attr("float")(formatted_str);
        } else {
          ret = OB_NOT_SUPPORTED;
        }
      }
      break;
    }
    case ObUFloatType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        float float_val = 0;
        if (OB_FAIL(obj.get_ufloat(float_val))) {
          LOG_WARN("get_ufloat failed", K(ret), K(obj));
        } else {
          char buf[64];
          int len = snprintf(buf, sizeof(buf), "%.6g", float_val);
          if (len > 0 && len < sizeof(buf)) {
            std::string formatted_str(buf, len);
            val = builtins.attr("float")(formatted_str);
          } else {
            ret = OB_NOT_SUPPORTED;
          }
        }
      }
      break;
    }
    case ObDoubleType: {
      double double_val = 0;
      if (OB_SUCC(result.get_double(col_idx, double_val))) {
        val = pybind11::float_(double_val);
      }
      break;
    }
    case ObUDoubleType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        double double_val = 0;
        if (OB_FAIL(obj.get_udouble(double_val))) {
          LOG_WARN("get_udouble failed", K(ret), K(obj));
        } else {
          val = pybind11::float_(double_val);
        }
      }
      break;
    }
    case ObNumberType:
    case ObUNumberType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        number::ObNumber number = obj.get_number();
        char buf[256];
        int64_t length = 0;
        if (OB_FAIL(number.format(buf, sizeof(buf), length, obj.get_scale()))) {
          LOG_WARN("format number failed", K(ret), K(number));
        } else {
          val = decimal_class(pybind11::str(buf, length));
        }
      }
      break;
    }
    case ObDecimalIntType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        const ObDecimalInt *decint = obj.get_decimal_int();
        int32_t int_bytes = obj.get_int_bytes();
        int16_t scale = obj.get_scale();

        if (OB_ISNULL(decint)) {
          val = pybind11::none();
        } else {
          char buf[256];
          int64_t length = 0;
          if (OB_FAIL(wide::to_string(decint, int_bytes, scale, buf, sizeof(buf), length))) {
            LOG_WARN("to_string failed", K(ret), K(scale), K(int_bytes));
          } else {
            val = decimal_class(pybind11::str(buf, length));
          }
        }
      }
      break;
    }
    case ObTimeType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        int64_t time_us = obj.get_time();
        int64_t days = time_us / (24 * 60 * 60 * 1000000L);
        int64_t remaining_us = time_us % (24 * 60 * 60 * 1000000L);
        int64_t seconds = remaining_us / 1000000L;
        int64_t microseconds = remaining_us % 1000000L;

        val = timedelta_class(
          pybind11::int_(days),
          pybind11::int_(seconds),
          pybind11::int_(microseconds)
        );
      }
      break;
    }
    case ObMySQLDateType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        ObMySQLDate mysql_date = obj.get_mysql_date();
        val = date_class(
          pybind11::int_(mysql_date.year_),
          pybind11::int_(mysql_date.month_),
          pybind11::int_(mysql_date.day_)
        );
      }
      break;
    }
    case ObMySQLDateTimeType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        ObMySQLDateTime mysql_dt = obj.get_mysql_datetime();
        ObSQLSessionInfo &session = inner_result.result_set().get_session();
        const ObTimeZoneInfo *tz_info = session.get_timezone_info();
        if (OB_ISNULL(tz_info)) {
          val = datetime_class(
            pybind11::int_(mysql_dt.year()),
            pybind11::int_(mysql_dt.month()),
            pybind11::int_(mysql_dt.day_),
            pybind11::int_(mysql_dt.hour_),
            pybind11::int_(mysql_dt.minute_),
            pybind11::int_(mysql_dt.second_),
            pybind11::int_(mysql_dt.microseconds_)
          );
        } else {
          int64_t timestamp_us = 0;
          if (OB_FAIL(ObTimeConverter::mdatetime_to_timestamp(mysql_dt, tz_info, timestamp_us))) {
            LOG_WARN("failed to convert mysql datetime to timestamp, use mysql datetime directly", KR(ret));
          } else {
            ObMySQLDateTime local_mdt;
            if (OB_FAIL(ObTimeConverter::timestamp_to_mdatetime(timestamp_us, tz_info, local_mdt))) {
              LOG_WARN("failed to convert timestamp to mysql datetime, use original", KR(ret));
            } else {
              val = datetime_class(
                pybind11::int_(local_mdt.year()),
                pybind11::int_(local_mdt.month()),
                pybind11::int_(local_mdt.day_),
                pybind11::int_(local_mdt.hour_),
                pybind11::int_(local_mdt.minute_),
                pybind11::int_(local_mdt.second_),
                pybind11::int_(local_mdt.microseconds_)
              );
            }
          }
        }
      }
      break;
    }
    case ObTimestampType: {
      int64_t v = 0;
      if (OB_SUCC(result.get_timestamp(col_idx, nullptr, v))) {
        ObSQLSessionInfo &session = inner_result.result_set().get_session();
        const ObTimeZoneInfo *tz_info = session.get_timezone_info();
        if (OB_ISNULL(tz_info)) {
          double seconds = static_cast<double>(v) / 1000000.0;
          val = fromtimestamp(seconds);
        } else {
          int64_t local_datetime_us = 0;
          if (OB_FAIL(ObTimeConverter::timestamp_to_datetime(v, tz_info, local_datetime_us))) {
            LOG_WARN("failed to convert timestamp to datetime, use UTC timestamp", KR(ret));
          } else {
            double local_seconds = static_cast<double>(local_datetime_us) / 1000000.0;
            val = utcfromtimestamp(local_seconds);
          }
        }
      }
      break;
    }
    case ObDateTimeType: {
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        int64_t datetime_val = 0;
        if (OB_FAIL(obj.get_datetime(datetime_val))) {
          LOG_WARN("get_datetime failed", K(ret), K(obj));
        } else {
          const ObTimeZoneInfo *tz_info = session.get_timezone_info();
          ObTime ob_time(DT_TYPE_DATETIME);
          if (OB_FAIL(ObTimeConverter::datetime_to_ob_time(datetime_val, tz_info, ob_time))) {
            LOG_WARN("failed to convert datetime to ob_time", K(ret), K(datetime_val));
          } else {
            val = datetime_class(
              pybind11::int_(ob_time.parts_[DT_YEAR]),
              pybind11::int_(ob_time.parts_[DT_MON]),
              pybind11::int_(ob_time.parts_[DT_MDAY]),
              pybind11::int_(ob_time.parts_[DT_HOUR]),
              pybind11::int_(ob_time.parts_[DT_MIN]),
              pybind11::int_(ob_time.parts_[DT_SEC]),
              pybind11::int_(ob_time.parts_[DT_USEC])
            );
          }
        }
      }
      break;
    }
    case ObYearType: {
      uint8_t v = 0;
      if (OB_SUCC(result.get_year(col_idx, v))) {
        int64_t year_int = 0;
        if (OB_FAIL(ObTimeConverter::year_to_int(v, year_int))) {
          LOG_WARN("year_to_int failed", K(ret), K(v));
        } else {
          val = pybind11::int_(year_int);
        }
      }
      break;
    }
    case ObEnumType:
    case ObSetType:
    case ObVarcharType:
    case ObCharType: {
      ObString obj_str;
      if (OB_FAIL(result.get_varchar(col_idx, obj_str))) {
        LOG_WARN("get varchar failed", K(ret), K(col_idx));
      } else if (obj_meta.get_collation_type() == ObCollationType::CS_TYPE_BINARY) {
        val = pybind11::bytes(obj_str.ptr(), obj_str.length());
      } else {
        ObString out_str;
        if (OB_FAIL(convert_string_charset(session, allocator, obj_str,
                                           obj_meta.get_collation_type(), out_str))) {
          LOG_WARN("convert string charset failed", K(ret), K(obj_str));
        } else {
          val = pybind11::str(out_str.ptr(), out_str.length());
        }
      }
      break;
    }
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType: {
      MTL_SWITCH(OB_SYS_TENANT_ID) {
        ObObj obj;
        ObString real_data;
        if (OB_FAIL(result.get_obj(col_idx, obj))) {
          LOG_WARN("get obj failed", K(ret), K(col_idx));
        } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&allocator, obj, real_data))) {
          LOG_WARN("failed to read real string data", K(ret), K(obj));
        } else {
          ObString out_str;
          if (OB_FAIL(convert_string_charset(session, allocator, real_data,
                                             obj_meta.get_collation_type(), out_str))) {
            LOG_WARN("convert string charset failed", K(ret) ,K(real_data));
          } else {
            val = pybind11::str(out_str.ptr(), out_str.length());
          }
        }
      }
      break;
    }
    case ObJsonType: {
      MTL_SWITCH(OB_SYS_TENANT_ID) {
        ObObj obj;
        ObString obj_str;
        if (OB_FAIL(result.get_obj(col_idx, obj))) {
          LOG_WARN("get obj failed", K(ret), K(col_idx));
        } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&allocator, obj, obj_str))) {
          LOG_WARN("failed to read real string data", K(ret), K(obj));
        } else if (obj_str.length() == 0) {
          val = pybind11::none();
        } else {
          ObJsonBin j_bin(obj_str.ptr(), obj_str.length(), &allocator);
          ObIJsonBase *j_base = &j_bin;
          ObJsonBuffer jbuf(&allocator);
          static_cast<ObJsonBin*>(j_base)->set_seek_flag(true);
          if (OB_FAIL(j_bin.reset_iter())) {
            OB_LOG(WARN, "fail to reset json bin iter", K(ret), K(obj_str));
          } else if (OB_FAIL(j_base->print(jbuf, true, obj_str.length()))) {
            OB_LOG(WARN, "json binary to string failed in mysql mode", K(ret), K(obj_str), K(*j_base));
          } else {
            val = pybind11::str(jbuf.ptr(), jbuf.length());
          }
        }
      }
      break;
    }
    case ObGeometryType:
    case ObRoaringBitmapType: {
      MTL_SWITCH(OB_SYS_TENANT_ID) {
        ObObj obj;
        ObString obj_str;
        if (OB_FAIL(result.get_obj(col_idx, obj))) {
          LOG_WARN("get obj failed", K(ret), K(col_idx));
        } else if (OB_FAIL(sql::ObTextStringHelper::read_real_string_data(&allocator, obj, obj_str))) {
          LOG_WARN("failed to read real string data", K(ret), K(obj));
        } else if (obj_str.length() == 0) {
          val = pybind11::bytes("");
        } else {
          val = pybind11::bytes(obj_str.ptr(), obj_str.length());
        }
      }
      break;
    }
    case ObCollectionSQLType: {
      ObObj obj;
      ObArenaAllocator allocator(mem_attr);
      ObString res_str;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else if (OB_FAIL(convert_collection_to_string(obj, obj_meta, inner_result, allocator, res_str))) {
        LOG_WARN("convert collection failed", KR(ret), K(obj), K(obj_meta));
        if (ret == OB_NOT_SUPPORTED) {
          val = pybind11::bytes(obj.get_string_ptr(), obj.get_string_len());
          ret = OB_SUCCESS;
        }
      } else {
        val = pybind11::str(res_str.ptr(), res_str.length());
      }
      break;
    }
    case ObBitType: {
      uint64_t int_val = 0;
      ObObj obj;
      if (OB_FAIL(result.get_obj(col_idx, obj))) {
        LOG_WARN("get obj failed", K(ret), K(col_idx));
      } else {
        int_val = htobe64(obj.get_bit());
        val = pybind11::bytes((char*)&int_val, sizeof(int_val));
      }
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      break;
    }
  }
  return ret;
}

int ObLiteEmbedUtil::convert_string_charset(sql::ObSQLSessionInfo &session,
                                             ObIAllocator &allocator,
                                             const ObString &in_str,
                                             ObCollationType col_collation,
                                             ObString &out_str)
{
  int ret = OB_SUCCESS;
  out_str.reset();

  ObCharsetType result_charset = CHARSET_INVALID;
  if (OB_FAIL(session.get_character_set_results(result_charset))) {
    LOG_WARN("get character_set_results failed", K(ret));
  } else if (result_charset == CHARSET_INVALID) {
    out_str = in_str;
  } else if (col_collation == CS_TYPE_BINARY) {
    out_str = in_str;
  } else {
    ObCollationType result_collation = ObCharset::get_default_collation(result_charset);
    if (col_collation == result_collation || col_collation == CS_TYPE_INVALID) {
      out_str = in_str;
    } else {
      if (OB_FAIL(ObCharset::charset_convert(allocator,
                                              in_str,
                                              col_collation,
                                              result_collation,
                                              out_str))) {
        LOG_WARN("charset convert failed, use original string", K(ret),
                 K(col_collation), K(result_collation));
      }
    }
  }
  return ret;
}

int ObLiteEmbedUtil::convert_collection_to_string(ObObj &obj, ObObjMeta &obj_meta, observer::ObInnerSQLResult &inner_result,
    ObIAllocator &allocator, ObString &res_str)
{
  int ret = OB_SUCCESS;
  const uint16_t subschema_id = obj.get_meta().get_subschema_id();
  ObSubSchemaValue sub_meta;
  if (OB_FAIL(inner_result.result_set().get_exec_context().get_sqludt_meta_by_subschema_id(subschema_id, sub_meta))) {
    LOG_WARN("failed to get udt meta", K(ret), K(subschema_id));
  } else if (sub_meta.type_ == ObSubSchemaType::OB_SUBSCHEMA_COLLECTION_TYPE) {
    ObSqlCollectionInfo *coll_meta = reinterpret_cast<ObSqlCollectionInfo *>(sub_meta.value_);
    if (OB_FAIL(sql::ObSqlUdtUtils::convert_collection_to_string(obj, *coll_meta, &allocator, res_str))) {
      FLOG_WARN("failed to convert udt to string", K(ret), K(subschema_id));
    }
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret;
}

} // end embed
} // end oceanbase
