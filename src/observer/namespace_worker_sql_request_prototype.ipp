// Prototype capability policy stays separate from native SQL execution.
#include "observer/namespace_worker_result_prototype.h"
#include "observer/mysql/ob_sync_plan_driver.h"
#include "observer/mysql/ob_sync_cmd_driver.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "query/protocol/ob_mysql_packet_sender.h"
#include "rpc/obmysql/packet/ompk_row.h"
#include "sql/ob_query_retry_ctrl.h"
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
int check_worker_sql(const ObString &text, sql::ObSQLSessionInfo &info, ObIAllocator &allocator) {
  if (worker_namespace == 1) { return OB_SUCCESS; }
  using namespace sql;
  auto *session = &info;
  int ret = OB_SUCCESS;
  // Admission policy only; native drivers own statement and transaction semantics.
  ObParser parser(allocator, session->get_sql_mode(), session->get_charsets4parser());
  ObSEArray<ObString, 2> statements;
  ObMPParseStat parse_stat;
  ret = parser.split_multiple_stmt(text, statements, parse_stat);
  if (!ret && (parse_stat.parse_fail_ || statements.count() != 1)) { ret = OB_NOT_SUPPORTED; }
  ParseResult parsed;
  if (!ret) { ret = parser.parse(text, parsed); }
  if (!ret) {
    const ParseNode *node = parsed.result_tree_;
    if (node && node->type_ == T_STMT_LIST && node->num_child_ == 1) { node = node->children_[0]; }
    if (!node) { ret = OB_INVALID_ARGUMENT; }
    if (!ret && node->type_ == T_INSERT
        && (node->num_child_ != 4
            || !node->children_[1]
            || node->children_[1]->type_ == T_REPLACE
            || node->children_[3])) {
      ret = OB_NOT_SUPPORTED;
    }
    // REPLACE and IGNORE need additional write/savepoint operations; keep them outside this slice.
    if (!ret && node->type_ == T_UPDATE && (node->num_child_ != 11 || node->children_[8])) { ret = OB_NOT_SUPPORTED; }
    if (!ret && node->type_ == T_DELETE && (node->num_child_ != 10 || node->children_[9])) { ret = OB_NOT_SUPPORTED; }
    std::vector<const ParseNode *> pending;
    if (!ret) { pending.push_back(node); }
    while (!ret && !pending.empty()) {
      const ParseNode *part = pending.back(); pending.pop_back();
      if (part->type_ == T_INTO_OUTFILE || part->type_ == T_INTO_DUMPFILE || part->type_ == T_INTO_VARIABLES) {
        ret = OB_NOT_SUPPORTED;
      }
      for (int i = 0; !ret && i < part->num_child_; ++i) {
        if (part->children_[i]) { pending.push_back(part->children_[i]); }
      }
    }
  }
  return ret;
}
int check_worker_plan(ObMySQLResultSet &result) {
  if (worker_namespace == 1) { return OB_SUCCESS; }
  using namespace sql;
  int ret = OB_SUCCESS;
  if (result.get_stmt_type() == stmt::T_VARIABLE_SET) {
    auto *command = static_cast<ObVariableSetStmt *>(result.get_cmd());
    if (!command || command->has_global_variable()) { ret = OB_NOT_SUPPORTED; }
    for (int64_t i = 0; !ret && i < command->get_variables_size(); ++i) {
      ObVariableSetStmt::VariableSetNode node;
      ret = command->get_variable_node(i, node);
      if (!ret && !node.set_names_stmt_) {
        if (node.set_scope_ != ObSetVar::SET_SCOPE_SESSION
            || (node.value_expr_ && node.value_expr_->has_flag(CNT_SUB_QUERY))) { ret = OB_NOT_SUPPORTED; }
        // Global/storage configuration still belongs to shared server administration.
        if (node.is_system_variable_ && node.variable_name_.case_compare("sql_mode") != 0
            && node.variable_name_.case_compare("ob_query_timeout") != 0
            && node.variable_name_.case_compare("autocommit") != 0
            && node.variable_name_.case_compare("transaction_isolation") != 0
            && node.variable_name_.case_compare("tx_isolation") != 0
            && node.variable_name_.case_compare("character_set_client") != 0
            && node.variable_name_.case_compare("character_set_connection") != 0
            && node.variable_name_.case_compare("character_set_results") != 0
            && node.variable_name_.case_compare("collation_connection") != 0) { ret = OB_NOT_SUPPORTED; }
      }
    }
  } else if (!ret && result.get_stmt_type() == stmt::T_USE_DATABASE) {
    auto *command = static_cast<ObUseDatabaseStmt *>(result.get_cmd());
    if (!command || storage::NamespaceForkKernelPrototype::is_encoded_id(
        static_cast<uint64_t>(command->get_db_id()))) {
      ret = OB_NOT_SUPPORTED;
    }
  }
  return ret;
}

// This adapter transports driver output, never opens/closes a SQL result or
// converts SQL values. Each row is bounded by the existing IPC frame limit.
class WorkerPacketSender final : public ObIMPPacketSender {
public:
  Frame terminal;
  bool finished = false;
  int error = OB_SUCCESS;
  void disconnect() override { if (worker_request) { worker_request->cancel(OB_CONNECT_ERROR); } }
  void force_disconnect() override { disconnect(); }
  ObSMConnection *get_conn() const override { return nullptr; }
  int wait_packet(obmysql::ObICSMemPool &, int64_t, obmysql::ObMySQLPacket *&) override { return OB_NOT_SUPPORTED; }
  int release_packet(obmysql::ObMySQLPacket *) override { return OB_NOT_SUPPORTED; }
  int flush_buffer(bool) override { return THIS_WORKER.check_status(); }
  int send_error_packet(int code, const char *, void * = nullptr) override {
    error = code; return OB_SUCCESS; // D returns the error after native cleanup.
  }
  int response_resultset_metadata(const ObIArray<obmysql::ObMySQLField> &fields,
      bool include_header, uint8_t eof_count, uint16_t warnings, uint16_t status) override {
    if (finished || fields.empty() || fields.count() > 64) { return OB_NOT_SUPPORTED; }
    Frame frame('H'); frame.number(fields.count()); frame.number(include_header);
    frame.number(eof_count); frame.number(warnings); frame.number(status);
    for (int64_t i = 0; i < fields.count(); ++i) { append_field(frame, fields.at(i)); }
    return worker_send(frame);
  }
  int response_packet(obmysql::ObMySQLPacket &packet) override {
    using namespace obmysql;
    if (finished) { return OB_ERR_UNEXPECTED; }
    if (packet.get_mysql_packet_type() == ObMySQLPacketType::PKT_EOF) {
      auto &eof = static_cast<OMPKEOF &>(packet);
      terminal = Frame('e'); terminal.number(eof.get_warning_count()); terminal.number(eof.get_server_status().flags_);
      finished = true; return terminal.ret;
    }
    if (packet.get_mysql_packet_type() != ObMySQLPacketType::PKT_ROW) { return OB_NOT_SUPPORTED; }
    const auto &row = static_cast<OMPKRow &>(packet).get_row();
    const int64_t count = row.get_cells_count();
    if (count < 0 || count > 64) { return OB_NOT_SUPPORTED; }
    Frame frame('R'); frame.number(row.get_protocol_type()); frame.number(row.is_packed()); frame.number(count);
    int ret = OB_SUCCESS;
    if (row.is_packed()) {
      const char *data = nullptr; int64_t size = 0;
      ret = row.get_packed_row_blob(data, size);
      if (!ret && (size < 0 || size > MAX_SQL_MESSAGE || (size && !data))) { ret = OB_INVALID_ARGUMENT; }
      if (!ret) { frame.string(ObString(static_cast<int32_t>(size), data)); }
    } else {
      ObArenaAllocator scratch(ObMemAttr("NsMySQLCell"));
      for (int64_t i = 0; !ret && i < count; ++i) {
        ObMySQLCellValue value;
        ret = row.build_cell_value(i, scratch, value);
        if (!ret) { append_cell(frame, value); ret = frame.ret; }
      }
    }
    return ret ? ret : worker_send(frame);
  }
  int send_ok_packet(sql::ObSQLSessionInfo &, ObOKPParam &param, obmysql::ObMySQLPacket *packet = nullptr) override {
    if (finished || packet) { return OB_NOT_SUPPORTED; }
    terminal = Frame('o'); terminal.number(param.affected_rows_); terminal.number(param.lii_);
    terminal.number(param.warnings_count_);
    terminal.number(param.has_more_result_); terminal.number(param.cursor_exist_);
    terminal.number(param.send_last_row_); terminal.number(param.has_pl_out_);
    terminal.number(param.take_trace_id_to_client_);
    terminal.string(param.message_ ? ObString::make_string(param.message_) : ObString());
    finished = true; return terminal.ret;
  }
  int send_eof_packet(const sql::ObSQLSessionInfo &, const ObMySQLResultSet &, ObOKPParam * = nullptr) override {
    return OB_NOT_SUPPORTED; // The synchronous drivers submit their native EOF packet.
  }
  int complete() {
    return error ? error : !finished ? OB_ERR_UNEXPECTED : worker_send(terminal);
  }
};
} } }
