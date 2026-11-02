#include "query/protocol/ob_mysql_protocol_util.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "observer/mysql/obsm_row.h"
#include "sql/executor/ob_execute_result.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "common/datum/datum_parameter_codec.h"
#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "common/ob_field.h"
#include "query/engine/basic/ob_row_to_expr_projector.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/expr/ob_expr_extra_info_factory.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/das/ob_das_scan_op.h"
#include "sql/ob_sql_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "storage/blocksstable/ob_datum_row.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cstring>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::obmysql;

namespace {
std::vector<std::string> columns(const std::string &line)
{
  std::vector<std::string> result;
  size_t begin = 0;
  for (;;) {
    const size_t end = line.find('\t', begin);
    result.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos) { return result; }
    begin = end + 1;
  }
}
std::string hex(const char *bytes, int64_t count)
{
  static const char digits[] = "0123456789abcdef";
  std::string result;
  for (int64_t i = 0; i < count; ++i) {
    const unsigned char ch = bytes[i];
    result += digits[ch >> 4]; result += digits[ch & 15];
  }
  return result.empty() ? "-" : result;
}
std::string unhex(const std::string &text)
{
  if (text == "-") { return {}; }
  const char *digits = "0123456789abcdef";
  std::string result;
  for (size_t i = 0; i + 1 < text.size(); i += 2) {
    const char *high = std::strchr(digits, text[i]);
    const char *low = std::strchr(digits, text[i + 1]);
    if (high == nullptr || low == nullptr) { return {}; }
    result += char((high - digits) * 16 + low - digits);
  }
  return result;
}
class RejectOwner final : public ObIAllocator {
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
};
int seed(const std::vector<std::string> &row, ObIAllocator &owner, DatumValue &value)
{
  const std::string &name = row[1];
  if (name.rfind("integer_", 0) == 0) {
    const size_t separator = name.find('_', 8);
    const int type = std::stoi(name.substr(8, separator - 8));
    const int ret = value.set_int(std::stoll(name.substr(separator + 1)), owner);
    if (ret == OB_SUCCESS) { value.meta_.set_type(static_cast<SqlValueType>(type)); }
    return ret;
  }
  if (name.rfind("unsupported_", 0) == 0) {
    const int ret = value.set_int(1, owner);
    if (ret == OB_SUCCESS) { value.meta_.set_type(static_cast<SqlValueType>(std::stoi(name.substr(12)))); }
    return ret;
  }
  if (name == "invalid") { value.meta_.set_type(ObMaxType); return OB_SUCCESS; }
  const std::string bytes = unhex(row[5]);
  if (bytes.empty()) { return OB_INVALID_DATA; }
  int64_t pos = 0;
  const int ret = DatumParameterCodec::decode_native_value(bytes.data(), bytes.size(), pos,
                                                    owner, value.datum_, value.meta_);
  return ret == OB_SUCCESS && pos != bytes.size() ? OB_INVALID_DATA : ret;
}
std::string neutral(int ret, const ObMySQLCellValue &out)
{
  std::ostringstream result;
  result << ret << '\t' << int(out.kind_) << '\t' << out.value_
         << '\t' << out.days_ << '\t' << out.microseconds_ << '\t' << out.bit_len_
         << '\t' << out.year_ << '\t' << int(out.month_) << '\t' << int(out.day_)
         << '\t' << int(out.hour_) << '\t' << int(out.minute_) << '\t' << int(out.second_)
         << '\t' << int(out.is_negative_) << '\t' << out.get_bytes_len()
         << '\t' << hex(out.get_bytes(), out.get_bytes_len());
  return result.str();
}

bool output_pack_scalar_datum_supported(const SqlTypeMeta &meta,
                                        const SqlValueDatumMapType map)
{
  bool supported = false;
  SqlValueDatumMapType expected_map = OBJ_DATUM_MAPPING_MAX;
  switch (meta.get_type_class()) {
  case ObNullTC:
    expected_map = OBJ_DATUM_NULL;
    supported = true;
    break;
  case ObIntTC:
  case ObUIntTC:
  case ObDoubleTC:
  case ObDateTimeTC:
  case ObTimeTC:
  case ObBitTC:
  case ObMySQLDateTimeTC:
    expected_map = OBJ_DATUM_8BYTE_DATA;
    supported = true;
    break;
  case ObFloatTC:
  case ObDateTC:
  case ObMySQLDateTC:
    expected_map = OBJ_DATUM_4BYTE_DATA;
    supported = true;
    break;
  case ObYearTC:
    expected_map = OBJ_DATUM_1BYTE_DATA;
    supported = true;
    break;
  case ObNumberTC:
    expected_map = OBJ_DATUM_NUMBER;
    supported = true;
    break;
  case ObDecimalIntTC:
    expected_map = OBJ_DATUM_DECIMALINT;
    supported = true;
    break;
  default:
    break;
  }
  return supported && map == expected_map
         && ObDatum::get_sql_value_datum_map_type(meta.get_type()) == expected_map;
}

uint64_t cell_checksum(const ObMySQLCellValue &cell)
{
  uint64_t checksum = static_cast<uint64_t>(cell.kind_) + cell.value_
      + cell.days_ + cell.microseconds_ + cell.bit_len_ + cell.year_
      + cell.month_ + cell.day_ + cell.hour_ + cell.minute_ + cell.second_
      + cell.is_negative_;
  const char *bytes = cell.get_bytes();
  for (int64_t i = 0; i < cell.get_bytes_len(); ++i) {
    checksum = checksum * 33 + static_cast<unsigned char>(bytes[i]);
  }
  return checksum;
}

struct CellFormatterBenchCase
{
  DatumValue value_;
  SqlValueDatumMapType map_;
  ObField field_;
  MYSQL_PROTOCOL_TYPE protocol_;
};

struct BenchSample
{
  double wall_ns_per_cell_;
  double cpu_ns_per_cell_;
};

template <typename Fn>
BenchSample measure_cells(Fn &&fn, int64_t cell_count, uint64_t &checksum)
{
  const auto wall_start = std::chrono::steady_clock::now();
  const std::clock_t cpu_start = std::clock();
  fn(checksum);
  const std::clock_t cpu_end = std::clock();
  const auto wall_end = std::chrono::steady_clock::now();
  const double wall_ns = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();
  const double cpu_ns = static_cast<double>(cpu_end - cpu_start) * 1.0e9 / CLOCKS_PER_SEC;
  return {wall_ns / cell_count, cpu_ns / cell_count};
}

double median(std::vector<double> values)
{
  std::sort(values.begin(), values.end());
  const size_t middle = values.size() / 2;
  return values.size() % 2 == 0
      ? (values[middle - 1] + values[middle]) / 2.0
      : values[middle];
}
}


namespace {
// The raw tree, schema/read-info, operator spec and finalizer are the same
// public construction boundaries used by ordinary CG. No native frame offset
// or native value/state address is supplied by this fixture.
class NativePointProtocolInputs
{
public:
  enum ProjectionShape {
    ALL_THREE_COLUMNS,
    DUPLICATE_TWO_STORAGE,
    MISSING_OUTPUT_STORAGE,
    MISSING_FILTER_STORAGE,
    FOREIGN_STORAGE_ACCESS,
    MISALIGNED_STORAGE_IDS
  };
  NativePointProtocolInputs()
      : owner_("PointProtocol"), session_(), exec_(owner_), eval_(exec_),
        factory_(owner_), range_node_(owner_), projector_(owner_), scan_(nullptr), pack_(nullptr), shape_(ALL_THREE_COLUMNS)
  {}
  ~NativePointProtocolInputs()
  { sql::ObSQLSessionInfo::ExecCtxSessionRegister unregister(session_, nullptr); }

  int initialize(MYSQL_PROTOCOL_TYPE protocol, bool with_pack, bool cursor_mode = false,
                 ProjectionShape shape = ALL_THREE_COLUMNS)
  {
    shape_ = shape;
    if (with_pack && shape_ != ALL_THREE_COLUMNS) { return OB_INVALID_ARGUMENT; }
    using namespace sql;
    using namespace share::schema;
    ObExprOperatorFactory::register_expr_operators();
    ObExprExtraInfoFactory::register_expr_extra_infos();
    int ret = session_.test_init(1, 1, nullptr);
    if (ret == OB_SUCCESS) { ret = session_.load_default_sys_variable(false, false); }
    if (ret == OB_SUCCESS) {
      exec_.set_my_session(&session_);
      sql_ctx_.stmt_type_ = stmt::T_SELECT;
      sql_ctx_.exec_type_ = MpQuery;
      sql_ctx_.is_prepare_protocol_ = true;
      sql_ctx_.is_cursor_ = cursor_mode;
      sql_ctx_.schema_guard_ = &schema_guard_;
      exec_.set_sql_ctx(&sql_ctx_);
      ret = exec_.create_physical_plan_ctx();
    }
    if (ret == OB_SUCCESS) { ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &exec_); }
    if (ret == OB_SUCCESS) {
      auto &parameters = *exec_.get_physical_plan_ctx();
      parameters.set_original_param_cnt(3);
      for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
        DatumParameter parameter;
        SqlTypeMeta meta; meta.set_int();
        parameter.set_type_meta(meta);
        parameter.set_accuracy(ObAccuracy(20, 0));
        parameter.datum_.set_int(value(i));
        ret = parameters.append_parameter_input(parameter);
      }
      if (ret == OB_SUCCESS) { ret = parameters.init_datum_param_store(); }
    }
    ObOpSpec *spec = nullptr;
    if (ret == OB_SUCCESS) { ret = physical_.alloc_op_spec(PHY_TABLE_SCAN, 0, spec, 0); }
    if (ret == OB_SUCCESS) {
      scan_ = static_cast<ObTableScanSpec *>(spec);
      ObTableSchema schema;
      schema.set_table_id(1001); schema.set_database_id(1001);
      schema.set_schema_version(1); schema.set_table_type(USER_TABLE);
      schema.set_index_type(INDEX_TYPE_IS_NOT); schema.set_rowkey_column_num(3);
      schema.set_max_used_column_id(shape_ == FOREIGN_STORAGE_ACCESS ? 19 : 18); schema.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      ret = schema.set_table_name(ObString::make_string("point_protocol"));
      auto &das = scan_->tsc_ctdef_.scan_ctdef_;
      if (ret == OB_SUCCESS) { ret = das.access_column_ids_.init(storage_count()); }
      const int64_t schema_columns = shape_ == FOREIGN_STORAGE_ACCESS ? 4 : 3;
      for (int64_t i = 0; ret == OB_SUCCESS && i < schema_columns; ++i) {
        ObColumnSchemaV2 column;
        column.set_table_id(1001); column.set_column_id(16 + i);
        column.set_data_type(ObIntType); column.set_data_length(8);
        column.set_accuracy(ObAccuracy(20, 0)); column.set_nullable(false);
        column.set_rowkey_position(i < 3 ? i + 1 : 0); column.set_collation_type(CS_TYPE_BINARY);
        const char *names[] = {"a", "b", "c", "foreign_column"};
        ret = column.set_column_name(ObString::make_string(names[i]));
        if (ret == OB_SUCCESS) { ret = schema.add_column(column); }
        if (ret == OB_SUCCESS && is_storage_column(i)) { ret = das.access_column_ids_.push_back(16 + i); }
      }
      if (ret == OB_SUCCESS) {
        ret = das.table_param_.convert(schema, das.access_column_ids_, das.pd_expr_spec_.pd_storage_flag_);
      }
    }
    // Corrupt only the negative control's access-ID correspondence after real
    // table/read-info construction. Do not change its schema or Expr pointers.
    if (ret == OB_SUCCESS && shape_ == MISALIGNED_STORAGE_IDS) {
      std::swap(scan_->tsc_ctdef_.scan_ctdef_.access_column_ids_.at(0),
                scan_->tsc_ctdef_.scan_ctdef_.access_column_ids_.at(1));
    }
    if (ret == OB_SUCCESS) { ret = generate(protocol, with_pack); }
    if (ret == OB_SUCCESS) { ret = allocate_execution(); }
    return ret;
  }

  int allocate_execution()
  {
    int ret = physical_.get_expr_frame_info().pre_alloc_exec_memory(exec_);
    if (ret == OB_SUCCESS) {
      eval_.frames_ = exec_.get_frames();
      eval_.set_max_batch_size(4);
    }
    return ret;
  }

  int use_generic_parameter_type()
  {
    SqlTypeMeta meta; meta.set_type(ObInt32Type);
    SqlType type;
    int ret = SqlType::from_legacy(meta, ObAccuracy(10, 0), type);
    Datum input; input.set_int(0);
    auto &parameters = *exec_.get_physical_plan_ctx();
    DatumParameterMetadata attributes = *parameters.get_parameter_metadata(1);
    attributes.type_meta_ = meta; attributes.param_meta_ = meta;
    attributes.accuracy_ = ObAccuracy(10, 0);
    if (ret == OB_SUCCESS) { ret = parameters.set_runtime_parameter(1, input, type, attributes); }
    if (ret == OB_SUCCESS) { ret = allocate_execution(); }
    return ret;
  }

  int project(const blocksstable::ObStorageDatum (&input)[3], int64_t row, int64_t &nop_count)
  {
    auto &pd = scan_->tsc_ctdef_.scan_ctdef_.pd_expr_spec_;
    sql::ObPushdownOperator push(eval_, pd);
    ObSEArray<int32_t, 3> indexes;
    int ret = OB_SUCCESS;
    for (int32_t i = 0; ret == OB_SUCCESS && i < 3; ++i) { ret = indexes.push_back(i); }
    if (ret == OB_SUCCESS) { ret = projector_.init(pd.access_exprs_, push, indexes); }
    sql::ObEvalCtx::BatchInfoScopeGuard scope(eval_);
    scope.set_batch_idx(row); scope.set_batch_size(4);
    if (ret == OB_SUCCESS) { ret = push.clear_evaluated_flag(); }
    int16_t nops[3] = {};
    nop_count = 0;
    if (ret == OB_SUCCESS) { ret = projector_.project(pd.access_exprs_, input, nops, nop_count); }
    if (ret == OB_SUCCESS && nop_count == 0) { ret = push.deep_copy(&pd.access_exprs_, row); }
    return ret;
  }

  int project_two_storage_columns(const blocksstable::ObStorageDatum (&input)[2],
                                  int64_t row, int64_t &nop_count)
  {
    auto &pd = scan_->tsc_ctdef_.scan_ctdef_.pd_expr_spec_;
    if (pd.access_exprs_.count() != 2) { return OB_INVALID_ARGUMENT; }
    sql::ObPushdownOperator push(eval_, pd);
    ObSEArray<int32_t, 2> indexes;
    int ret = indexes.push_back(0);
    if (ret == OB_SUCCESS) { ret = indexes.push_back(1); }
    if (ret == OB_SUCCESS) { ret = projector_.init(pd.access_exprs_, push, indexes); }
    sql::ObEvalCtx::BatchInfoScopeGuard scope(eval_);
    scope.set_batch_idx(row); scope.set_batch_size(4);
    if (ret == OB_SUCCESS) { ret = push.clear_evaluated_flag(); }
    int16_t nops[2] = {};
    nop_count = 0;
    if (ret == OB_SUCCESS) { ret = projector_.project(pd.access_exprs_, input, nops, nop_count); }
    if (ret == OB_SUCCESS && nop_count == 0) { ret = push.deep_copy(&pd.access_exprs_, row); }
    return ret;
  }

  int filter(int64_t row, bool &filtered)
  {
    sql::ObEvalCtx::BatchInfoScopeGuard scope(eval_);
    scope.set_batch_idx(row); scope.set_batch_size(4);
    return sql::ObOperator::filter_row(eval_, scan_->filters_, filtered);
  }

  int packed(int64_t row, std::string &bytes)
  {
    if (pack_ == nullptr) { return OB_NOT_INIT; }
    sql::ObEvalCtx::BatchInfoScopeGuard scope(eval_);
    scope.set_batch_idx(row); scope.set_batch_size(4);
    Datum packed;
    const int ret = pack_->eval_value(eval_, packed);
    if (ret == OB_SUCCESS) { bytes.assign(packed.get_string().ptr(), packed.get_string().length()); }
    return ret;
  }

  const sql::ExprFixedArray &columns() const { return scan_->tsc_ctdef_.scan_ctdef_.pd_expr_spec_.access_exprs_; }
  static int64_t value(int64_t index) { return index == 0 ? INT64_MIN : index == 1 ? 0 : INT64_MAX; }
  ObArenaAllocator owner_;
  sql::ObSQLSessionInfo session_;
  sql::ObExecContext exec_;
  sql::ObEvalCtx eval_;
  share::schema::ObSchemaGetterGuard schema_guard_;
  sql::ObSqlCtx sql_ctx_;
  sql::ObPhysicalPlan physical_;
  sql::ObRawExprFactory factory_;
  sql::ObRangeNode range_node_;
  storage::ObRow2ExprsProjector projector_;
  int64_t bounds_[3] = {};
  sql::ObTableScanSpec *scan_;
  sql::ObExpr *pack_;
  ObSEArray<ObField, 3> fields_;
  sql::ObExpr *key_columns_[3] = {};

private:
  int64_t storage_count() const
  { return shape_ == ALL_THREE_COLUMNS || shape_ == FOREIGN_STORAGE_ACCESS ? 3 : 2; }
  bool is_storage_column(int64_t index) const
  {
    return shape_ == ALL_THREE_COLUMNS ? index < 3
        : shape_ == MISSING_OUTPUT_STORAGE ? index < 2 : index != 1;
  }
  ProjectionShape shape_;
  int generate(MYSQL_PROTOCOL_TYPE protocol, bool with_pack)
  {
    using namespace sql;
    int ret = OB_SUCCESS;
    ObColumnRefRawExpr *columns[3] = {};
    ObColumnRefRawExpr *foreign_column = nullptr;
    ObOpRawExpr *equals[3] = {};
    ObRawExprResType bigint;
    bigint.set_int(); bigint.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType]);
    bigint.set_collation_type(CS_TYPE_BINARY); bigint.set_collation_level(CS_LEVEL_NUMERIC);
    ObRawExprUniqueSet roots(false);
    ObSEArray<ColumnItem, 3> range_columns;
    ObSEArray<ObRawExpr *, 3> pack_columns;
    for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
      ObConstRawExpr *parameter = nullptr;
      if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_REF_COLUMN, columns[i]); }
      if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_QUESTIONMARK, parameter); }
      if (ret == OB_SUCCESS) {
        columns[i]->set_ref_id(1001, 16 + i); columns[i]->set_result_type(bigint);
        ret = columns[i]->add_flag(IS_COLUMN);
      }
      if (ret == OB_SUCCESS) { ret = columns[i]->add_flag(CNT_COLUMN); }
      if (ret == OB_SUCCESS) { ret = parameter->set_questionmark_index(i); }
      if (ret == OB_SUCCESS) {
        parameter->set_result_type(bigint); ret = parameter->add_flag(IS_CONST);
      }
      if (ret == OB_SUCCESS) { ret = parameter->add_flag(CNT_CONST); }
      if (ret == OB_SUCCESS) { ret = factory_.create_raw_expr(T_OP_EQ, equals[i]); }
      if (ret == OB_SUCCESS) { ret = equals[i]->set_param_exprs(columns[i], parameter); }
      if (ret == OB_SUCCESS) { ret = equals[i]->formalize(&session_); }
      if (ret == OB_SUCCESS) { ret = roots.append(equals[i]); }
      if (ret == OB_SUCCESS) {
        ColumnItem item; item.expr_ = columns[i]; item.set_ref_id(1001, 16 + i);
        ret = range_columns.push_back(item);
      }
      if (ret == OB_SUCCESS) { ret = pack_columns.push_back(columns[i]); }
      if (ret == OB_SUCCESS) {
        ObField field; field.type_meta_.set_int(); field.accuracy_ = ObAccuracy(20, 0);
        ret = fields_.push_back(field);
      }
    }
    if (ret == OB_SUCCESS && shape_ == FOREIGN_STORAGE_ACCESS) {
      ret = factory_.create_raw_expr(T_REF_COLUMN, foreign_column);
      if (ret == OB_SUCCESS) {
        foreign_column->set_ref_id(1001, 19); foreign_column->set_result_type(bigint);
        ret = foreign_column->add_flag(IS_COLUMN);
      }
      if (ret == OB_SUCCESS) { ret = foreign_column->add_flag(CNT_COLUMN); }
      if (ret == OB_SUCCESS) { ret = foreign_column->formalize(&session_); }
      if (ret == OB_SUCCESS) { ret = roots.append(foreign_column); }
    }
    ObRawExpr *raw_pack = nullptr;
    if (ret == OB_SUCCESS && with_pack) {
      ret = ObRawExprUtils::build_pack_expr(factory_, protocol == BINARY, &session_, &fields_, pack_columns, raw_pack);
      if (ret == OB_SUCCESS) { ret = roots.append(raw_pack); }
    }
    physical_.set_plan_type(OB_PHY_PLAN_LOCAL); physical_.set_stmt_type(stmt::T_SELECT);
    physical_.set_px_dop(1); physical_.set_das_dop(1); physical_.set_batch_size(4);
    ObStaticEngineExprCG cg(physical_.get_allocator(), &session_, &schema_guard_, 3, 3);
    cg.set_batch_size(4);
    if (ret == OB_SUCCESS) { ret = cg.generate(roots, physical_.get_expr_frame_info()); }
    auto &das = scan_->tsc_ctdef_.scan_ctdef_;
    auto &pd = das.pd_expr_spec_;
    if (ret == OB_SUCCESS && shape_ != ALL_THREE_COLUMNS) {
      // Match fxa3's real no-pack point: keep the complete three-key CG/range
      // graph, but only the two requested storage values can execute.
      ret = pd.access_exprs_.init(storage_count());
      if (ret == OB_SUCCESS) { ret = das.result_output_.init(2); }
      if (ret == OB_SUCCESS) { ret = scan_->output_.init(3); }
      for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
        key_columns_[i] = static_cast<ObExpr *>(cg.get_left_value_rt_expr(*columns[i]));
        if (is_storage_column(i)) { ret = pd.access_exprs_.push_back(key_columns_[i]); }
      }
      if (ret == OB_SUCCESS && shape_ == FOREIGN_STORAGE_ACCESS) {
        ret = pd.access_exprs_.push_back(static_cast<ObExpr *>(cg.get_left_value_rt_expr(*foreign_column)));
      }
      if (ret == OB_SUCCESS) {
        ret = das.result_output_.push_back(shape_ == MISSING_OUTPUT_STORAGE ? key_columns_[1] : key_columns_[2]);
      }
      if (ret == OB_SUCCESS) { ret = das.result_output_.push_back(key_columns_[0]); }
      const int64_t projection[] = {2, 0, 2};
      for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
        ret = scan_->output_.push_back(key_columns_[projection[i]]);
      }
      if (ret == OB_SUCCESS && shape_ == MISSING_FILTER_STORAGE) {
        ret = scan_->filters_.init(1);
        if (ret == OB_SUCCESS) {
          ret = scan_->filters_.push_back(static_cast<ObExpr *>(cg.get_left_value_rt_expr(*equals[1])));
        }
      }
    } else {
    if (ret == OB_SUCCESS) { ret = pd.access_exprs_.init(3); }
    if (ret == OB_SUCCESS) { ret = pd.calc_exprs_.init(6); }
    if (ret == OB_SUCCESS) { ret = das.result_output_.init(3); }
    if (ret == OB_SUCCESS) { ret = scan_->output_.init(with_pack ? 1 : 3); }
    if (ret == OB_SUCCESS) { ret = scan_->filters_.init(3); }
    for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
      auto *column = static_cast<ObExpr *>(cg.get_left_value_rt_expr(*columns[i]));
      auto *equal = static_cast<ObExpr *>(cg.get_left_value_rt_expr(*equals[i]));
      ret = pd.access_exprs_.push_back(column);
      if (ret == OB_SUCCESS) { ret = pd.calc_exprs_.push_back(column); }
      if (ret == OB_SUCCESS) { ret = pd.calc_exprs_.push_back(equal); }
      if (ret == OB_SUCCESS) { ret = das.result_output_.push_back(column); }
      if (ret == OB_SUCCESS && !with_pack) { ret = scan_->output_.push_back(column); }
      if (ret == OB_SUCCESS) { ret = scan_->filters_.push_back(equal); }
    }
    if (ret == OB_SUCCESS && with_pack) {
      pack_ = static_cast<ObExpr *>(cg.get_left_value_rt_expr(*raw_pack));
      ret = scan_->output_.push_back(pack_);
    }
    }
    pd.max_batch_size_ = 4;
    scan_->ref_table_id_ = das.ref_table_id_ = 1001;
    scan_->part_level_ = share::schema::PARTITION_LEVEL_ZERO; das.is_get_ = true;
    auto &range = scan_->tsc_ctdef_.pre_range_graph_;
    if (ret == OB_SUCCESS) { ret = range.fill_column_metas(range_columns); }
    if (ret == OB_SUCCESS) { ret = range.get_range_map().expr_final_infos_.prepare_allocate(3); }
    for (int64_t i = 0; ret == OB_SUCCESS && i < 3; ++i) {
      auto &bound = range.get_range_map().expr_final_infos_.at(i);
      bound.is_param_ = true; bound.param_idx_ = i; bounds_[i] = i;
    }
    range_node_.column_cnt_ = 3; range_node_.start_keys_ = range_node_.end_keys_ = bounds_;
    range_node_.min_offset_ = 0; range_node_.max_offset_ = 2;
    range_node_.include_start_ = range_node_.include_end_ = true;
    range.set_range_head(&range_node_); range.set_node_count(1);
    range.set_is_precise_get(true); range.set_is_standard_range(true);
    range.set_is_equal_range(true); range.set_is_get(true);
    physical_.set_root_op_spec(scan_);
    exec_.reference_my_plan(&physical_);
    if (ret == OB_SUCCESS) { ret = ObStaticEngineExprCG::finalize_native_i64_point(physical_, exec_); }
    return ret;
  }
};

void expect_point_protocol_row(NativePointProtocolInputs &input, int64_t index,
                               MYSQL_PROTOCOL_TYPE protocol, bool first_null)
{
  sql::ResultRowView view;
  view.set_datums(input.columns(), input.eval_, index, true, nullptr);
  ObSEArray<int64_t, 3> indexes;
  ObSEArray<DatumValue, 1> fallback;
  for (int64_t i = 0; i < 3; ++i) { ASSERT_EQ(OB_SUCCESS, indexes.push_back(-3)); }
  ObDataTypeCastParams cast_params;
  DatumMysqlRow row(protocol, view, indexes, fallback, cast_params, input.session_, input.fields_,
                    CHARSET_UTF8MB4, nullptr);
  for (int64_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(DatumMysqlRow::can_encode_inline(view, i, input.fields_.at(i)));
    Datum expected_value;
    if (!(first_null && i == 0)) { expected_value.set_int(NativePointProtocolInputs::value(i)); }
    ObMySQLCellValue expected, actual;
    ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_inline_bigint_cell_value(expected_value,
        input.columns().at(i)->get_sql_type(), protocol, input.owner_, expected, &input.fields_.at(i)));
    ASSERT_EQ(OB_SUCCESS, row.build_cell_value(i, input.owner_, actual));
    EXPECT_EQ(neutral(OB_SUCCESS, expected), neutral(OB_SUCCESS, actual));
  }
}

void expect_duplicate_protocol_row(NativePointProtocolInputs &input, int64_t row_index,
                                   MYSQL_PROTOCOL_TYPE protocol, int64_t first,
                                   int64_t third, int64_t null_mask)
{
  sql::ResultRowView view;
  view.set_datums(input.scan_->output_, input.eval_, row_index, true, nullptr);
  ASSERT_EQ(3, view.count());
  ObSEArray<int64_t, 3> indexes;
  ObSEArray<DatumValue, 1> fallback;
  for (int64_t i = 0; i < 3; ++i) { ASSERT_EQ(OB_SUCCESS, indexes.push_back(-3)); }
  ObDataTypeCastParams cast;
  DatumMysqlRow row(protocol, view, indexes, fallback, cast, input.session_, input.fields_,
                    CHARSET_UTF8MB4, nullptr);
  ASSERT_EQ(3, row.get_cells_count());
  const int64_t projected_values[] = {third, first, third};
  const int64_t projected_keys[] = {2, 0, 2};
  for (int64_t i = 0; i < 3; ++i) {
    ASSERT_TRUE(DatumMysqlRow::can_encode_inline(view, i, input.fields_.at(i)));
    Datum captured;
    const SqlType *type = nullptr;
    ASSERT_EQ(OB_SUCCESS, view.get_inline_value(i, captured, type));
    ASSERT_NE(nullptr, type);
    EXPECT_EQ(ObIntType, type->kind());
    ObMySQLCellValue actual;
    ASSERT_EQ(OB_SUCCESS, row.build_cell_value(i, input.owner_, actual));
    if ((null_mask & (1L << projected_keys[i])) != 0) {
      EXPECT_TRUE(captured.is_null());
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, actual.kind_);
    } else {
      ASSERT_TRUE(captured.is_present());
      EXPECT_EQ(projected_values[i], captured.get_int());
      if (protocol == TEXT) {
        EXPECT_EQ(ObMySQLCellValueKind::LENENC_BYTES, actual.kind_);
        EXPECT_EQ(std::to_string(projected_values[i]), std::string(actual.get_bytes(), actual.get_bytes_len()));
      } else {
        EXPECT_EQ(ObMySQLCellValueKind::I64, actual.kind_);
        EXPECT_EQ(static_cast<uint64_t>(projected_values[i]), actual.value_);
      }
    }
  }
}

void expect_unproduced_point_column_unpublished(NativePointProtocolInputs &input, int64_t row)
{
  const sql::ObExpr &unproduced = *input.key_columns_[1];
  Datum value; value.set_int(919);
  ASSERT_EQ(OB_SUCCESS, unproduced.read_value(input.eval_, row, value));
  EXPECT_TRUE(value.is_null());
  EXPECT_FALSE(unproduced.get_eval_info(input.eval_).evaluated_);
  EXPECT_FALSE(unproduced.get_eval_info(input.eval_).projected_);
  EXPECT_FALSE(unproduced.get_evaluated_flags(input.eval_).at(row));
  bool native = false;
  ASSERT_EQ(OB_SUCCESS, unproduced.get_native_i64_mode(input.eval_, native));
  if (native) {
    sql::NativeI64Lane lane;
    ASSERT_EQ(OB_SUCCESS, unproduced.get_native_i64_lane(input.eval_, lane));
    EXPECT_EQ(static_cast<uint8_t>(Datum::State::Null), lane.states[row]);
    // A NULL slot has no readable payload; never inspect lane.values[row].
  }
}
} // namespace

TEST(NativeMysqlProtocol, actual_point_cg_storage_projector_filter_and_result_protocol_choose_one_execution_layout)
{
  for (const auto protocol : {TEXT, BINARY}) {
    NativePointProtocolInputs input;
    ASSERT_EQ(OB_SUCCESS, input.initialize(protocol, false));
    ASSERT_NE(nullptr, input.physical_.get_expr_frame_info().native_i64_point_layout_);
    uint32_t frame[3], offset[3];
    for (int64_t i = 0; i < 3; ++i) {
      bool native = false;
      ASSERT_EQ(OB_SUCCESS, input.columns().at(i)->get_native_i64_mode(input.eval_, native));
      ASSERT_TRUE(native);
      frame[i] = input.columns().at(i)->frame_idx_; offset[i] = input.columns().at(i)->datum_off_;
      EXPECT_EQ(nullptr, input.exec_.get_physical_plan_ctx()->locate_materialized_parameter(i));
    }
    blocksstable::ObStorageDatum values[3];
    for (int64_t i = 0; i < 3; ++i) { values[i].set_int(NativePointProtocolInputs::value(i)); }
    int64_t nops = -1;
    ASSERT_EQ(OB_SUCCESS, input.project(values, 2, nops)); ASSERT_EQ(0, nops);
    bool filtered = true;
    ASSERT_EQ(OB_SUCCESS, input.filter(2, filtered)); EXPECT_FALSE(filtered);
    ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 2, protocol, false));
    for (int64_t i = 0; i < 3; ++i) {
      EXPECT_EQ(nullptr, input.exec_.get_physical_plan_ctx()->locate_materialized_parameter(i));
    }
    values[0].set_null();
    ASSERT_EQ(OB_SUCCESS, input.project(values, 1, nops)); ASSERT_EQ(0, nops);
    ASSERT_EQ(OB_SUCCESS, input.filter(1, filtered)); EXPECT_TRUE(filtered);
    ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 1, protocol, true));
    values[0].set_nop();
    ASSERT_EQ(OB_SUCCESS, input.project(values, 0, nops)); EXPECT_EQ(1, nops);
    if (input.columns().at(0)->is_batch_result()) {
      EXPECT_FALSE(input.columns().at(0)->get_evaluated_flags(input.eval_).at(0));
    } else {
      EXPECT_FALSE(input.columns().at(0)->get_eval_info(input.eval_).evaluated_);
    }
    // A real full-Type rebind selects the generic alternative for the whole
    // execution. Re-prepare the actual projector targets after new frames.
    ASSERT_EQ(OB_SUCCESS, input.use_generic_parameter_type());
    for (int64_t i = 0; i < 3; ++i) {
      bool native = true;
      ASSERT_EQ(OB_SUCCESS, input.columns().at(i)->get_native_i64_mode(input.eval_, native));
      EXPECT_FALSE(native);
      EXPECT_EQ(frame[i], input.columns().at(i)->frame_idx_);
      EXPECT_EQ(offset[i], input.columns().at(i)->datum_off_);
      values[i].set_int(NativePointProtocolInputs::value(i));
    }
    ASSERT_EQ(OB_SUCCESS, input.project(values, 2, nops)); ASSERT_EQ(0, nops);
    ASSERT_EQ(OB_SUCCESS, input.filter(2, filtered)); EXPECT_FALSE(filtered);
    ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 2, protocol, false));
  }
}

TEST(NativeMysqlProtocol, real_cg_targets_and_protocol_use_physical_slots_with_scalar_parent_context)
{
  for (const auto protocol : {TEXT, BINARY}) {
    for (const int mode : {0, 1, 2}) { // ordinary, native, descriptor-present generic fallback
      NativePointProtocolInputs input;
      ASSERT_EQ(OB_SUCCESS, input.initialize(protocol, false, mode == 0));
      if (mode == 2) { ASSERT_EQ(OB_SUCCESS, input.use_generic_parameter_type()); }
      EXPECT_EQ(mode == 0, input.physical_.get_expr_frame_info().native_i64_point_layout_ == nullptr);
      for (const int64_t consumer_batch : {0, 1}) {
        input.eval_.set_max_batch_size(consumer_batch);
        blocksstable::ObStorageDatum storage_values[3];
        for (int64_t i = 0; i < 3; ++i) { storage_values[i].set_int(NativePointProtocolInputs::value(i)); }
        int64_t nops = -1;
        ASSERT_EQ(OB_SUCCESS, input.project(storage_values, 0, nops)); ASSERT_EQ(0, nops);
        ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 0, protocol, false));
        sql::ObExprFrameWriteTarget targets[3];
        EXPECT_EQ(32, sizeof(targets[0]));
        for (int64_t i = 0; i < 3; ++i) {
          const sql::ObExpr &expr = *input.columns().at(i);
          ASSERT_TRUE(expr.is_batch_result());
          ASSERT_EQ(OB_SUCCESS, targets[i].prepare(expr, input.eval_));
          EXPECT_EQ(mode == 1, targets[i].is_native());
          ASSERT_EQ(OB_SUCCESS, targets[i].set_i64(3, NativePointProtocolInputs::value(i)));
          EXPECT_EQ(OB_INVALID_ARGUMENT, targets[i].set_i64(-1, 27));
          EXPECT_EQ(OB_INVALID_ARGUMENT, targets[i].set_i64(4, 27));
          if (mode == 0) {
            ASSERT_EQ(4 * sizeof(Datum), expr.pvt_skip_off_ - expr.datum_off_);
            sql::ObExpr malformed = expr;
            malformed.pvt_skip_off_ = malformed.datum_off_ + 1;
            EXPECT_EQ(OB_INVALID_ARGUMENT, targets[i].prepare(malformed, input.eval_));
          } else {
            auto *header = reinterpret_cast<sql::NativeI64FrameHeader *>(input.eval_.frames_[expr.frame_idx_]);
            const uint32_t magic = header->magic;
            header->magic ^= 1;
            EXPECT_EQ(OB_ERR_UNEXPECTED, targets[i].prepare(expr, input.eval_));
            header->magic = magic;
          }
          // A failed re-prepare must preserve the previously prepared target.
          ASSERT_EQ(OB_SUCCESS, targets[i].set_i64(3, NativePointProtocolInputs::value(i)));
        }
        ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 3, protocol, false));
        ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 0, protocol, false));
        for (const int64_t invalid_row : {-1, 4}) {
          sql::ResultRowView view;
          view.set_datums(input.columns(), input.eval_, invalid_row, true, nullptr);
          Datum out; out.set_int(991);
          const SqlType *type = &input.columns().at(1)->get_sql_type();
          const SqlType *previous_type = type;
          EXPECT_EQ(OB_INVALID_ARGUMENT, view.get_inline_value(0, out, type));
          EXPECT_EQ(991, out.get_int()); EXPECT_EQ(previous_type, type);
        }
      }
    }
  }
}

TEST(NativeMysqlProtocol, actual_cg_das_reset_preserves_native_and_generic_values_without_datum_projection)
{
  for (const int mode : {1, 0, 2}) { // native first: old production aborts at the actual DAS entry
    NativePointProtocolInputs input;
    ASSERT_EQ(OB_SUCCESS, input.initialize(BINARY, false, mode == 0));
    if (mode == 2) { ASSERT_EQ(OB_SUCCESS, input.use_generic_parameter_type()); }
    blocksstable::ObStorageDatum storage_values[3];
    for (int64_t i = 0; i < 3; ++i) { storage_values[i].set_int(NativePointProtocolInputs::value(i)); }
    int64_t nops = -1;
    ASSERT_EQ(OB_SUCCESS, input.project(storage_values, 2, nops)); ASSERT_EQ(0, nops);
    storage_values[0].set_null();
    ASSERT_EQ(OB_SUCCESS, input.project(storage_values, 1, nops)); ASSERT_EQ(0, nops);

    sql::ObEvalInfo expected_infos[3];
    std::vector<char> evaluated[3], skip[3];
    sql::NativeI64Lane lanes[3] = {};
    Datum before[3][2];
    for (int64_t i = 0; i < 3; ++i) {
      const sql::ObExpr &expr = *input.columns().at(i);
      bool native = false;
      ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_mode(input.eval_, native)); EXPECT_EQ(mode == 1, native);
      if (native) { ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_lane(input.eval_, lanes[i])); }
      sql::ObEvalInfo &info = expr.get_eval_info(input.eval_);
      info.evaluated_ = true; info.projected_ = true; info.notnull_ = false;
      info.point_to_frame_ = false; info.cnt_ = 3;
      expected_infos[i] = info; expected_infos[i].point_to_frame_ = true;
      for (int64_t row = 1; row <= 2; ++row) {
        ASSERT_EQ(OB_SUCCESS, expr.read_value(input.eval_, row, before[i][row - 1]));
      }
      const int64_t bitmap_bytes = sql::ObBitVector::memory_size(4);
      const char *flags = reinterpret_cast<const char *>(&expr.get_evaluated_flags(input.eval_));
      const char *private_skip = reinterpret_cast<const char *>(&expr.get_pvt_skip(input.eval_));
      evaluated[i].assign(flags, flags + bitmap_bytes);
      skip[i].assign(private_skip, private_skip + bitmap_bytes);
      EXPECT_EQ(nullptr, input.exec_.get_physical_plan_ctx()->locate_materialized_parameter(i));
    }
    sql::ObDASScanOp task(input.owner_);
    // This is the same public reset entry reached by open_op() in the failed PS.
    task.reset_access_datums_ptr(&input.scan_->tsc_ctdef_.scan_ctdef_, input.eval_, 4);
    // Exercise all three reset lists using the same real CG-produced expressions.
    // This tests list dispatch, not storage-aggregate or transaction SQL semantics.
    sql::ObDASScanCtDef reset_lists(input.owner_);
    ASSERT_EQ(OB_SUCCESS, reset_lists.pd_expr_spec_.access_exprs_.init(1));
    ASSERT_EQ(OB_SUCCESS, reset_lists.pd_expr_spec_.access_exprs_.push_back(input.columns().at(0)));
    ASSERT_EQ(OB_SUCCESS, reset_lists.pd_expr_spec_.pd_storage_aggregate_output_.init(1));
    ASSERT_EQ(OB_SUCCESS, reset_lists.pd_expr_spec_.pd_storage_aggregate_output_.push_back(input.columns().at(1)));
    reset_lists.trans_info_expr_ = input.columns().at(2);
    for (const int64_t capacity : {0, 1, 4}) {
      for (int64_t i = 0; i < 3; ++i) { input.columns().at(i)->get_eval_info(input.eval_).point_to_frame_ = false; }
      task.reset_access_datums_ptr(&reset_lists, input.eval_, capacity);
      for (int64_t i = 0; i < 3; ++i) {
        const sql::ObExpr &expr = *input.columns().at(i);
        EXPECT_EQ(expected_infos[i].flag_, expr.get_eval_info(input.eval_).flag_);
        EXPECT_EQ(expected_infos[i].cnt_, expr.get_eval_info(input.eval_).cnt_);
        EXPECT_EQ(0, MEMCMP(evaluated[i].data(), &expr.get_evaluated_flags(input.eval_), evaluated[i].size()));
        EXPECT_EQ(0, MEMCMP(skip[i].data(), &expr.get_pvt_skip(input.eval_), skip[i].size()));
        for (int64_t row = 1; row <= 2; ++row) {
          Datum after;
          ASSERT_EQ(OB_SUCCESS, expr.read_value(input.eval_, row, after));
          EXPECT_EQ(0, MEMCMP(&before[i][row - 1], &after, sizeof(Datum)));
        }
        if (mode == 1) {
          sql::NativeI64Lane after;
          ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_lane(input.eval_, after));
          EXPECT_EQ(lanes[i].values, after.values); EXPECT_EQ(lanes[i].states, after.states);
          EXPECT_EQ(lanes[i].capacity, after.capacity);
        }
        EXPECT_EQ(nullptr, input.exec_.get_physical_plan_ctx()->locate_materialized_parameter(i));
      }
      ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 2, BINARY, false));
      ASSERT_NO_FATAL_FAILURE(expect_point_protocol_row(input, 1, BINARY, true));
    }
  }
}

TEST(NativeMysqlProtocol, actual_point_output_pack_uses_format_constant_and_matches_generic_fallback_bytes)
{
  for (const auto protocol : {TEXT, BINARY}) {
    NativePointProtocolInputs input;
    ASSERT_EQ(OB_SUCCESS, input.initialize(protocol, true));
    ASSERT_NE(nullptr, input.physical_.get_expr_frame_info().native_i64_point_layout_);
    ASSERT_NE(nullptr, input.pack_);
    ASSERT_EQ(4, input.pack_->arg_cnt_);
    Datum format;
    ASSERT_EQ(OB_SUCCESS, input.pack_->args_[0]->read_value(input.eval_, 0, format));
    ASSERT_TRUE(format.is_present());
    EXPECT_EQ(protocol == BINARY ? 0 : 1, format.get_int());
    blocksstable::ObStorageDatum values[3];
    for (int64_t i = 0; i < 3; ++i) { values[i].set_int(NativePointProtocolInputs::value(i)); }
    int64_t nops = -1;
    ASSERT_EQ(OB_SUCCESS, input.project(values, 2, nops)); ASSERT_EQ(0, nops);
    std::string native_bytes, generic_bytes;
    ASSERT_EQ(OB_SUCCESS, input.packed(2, native_bytes)); ASSERT_FALSE(native_bytes.empty());
    ASSERT_EQ(OB_SUCCESS, input.use_generic_parameter_type());
    ASSERT_EQ(OB_SUCCESS, input.project(values, 2, nops)); ASSERT_EQ(0, nops);
    ASSERT_EQ(OB_SUCCESS, input.packed(2, generic_bytes));
    EXPECT_EQ(native_bytes, generic_bytes);
  }
}

TEST(NativeMysqlProtocol, actual_duplicate_point_two_storage_columns_reach_both_protocols_without_publishing_missing_lane)
{
  for (const auto protocol : {TEXT, BINARY}) {
    NativePointProtocolInputs input;
    ASSERT_EQ(OB_SUCCESS, input.initialize(protocol, false, false,
        NativePointProtocolInputs::DUPLICATE_TWO_STORAGE));
    auto &das = input.scan_->tsc_ctdef_.scan_ctdef_;
    ASSERT_EQ(2, das.pd_expr_spec_.access_exprs_.count());
    ASSERT_EQ(2, das.access_column_ids_.count());
    EXPECT_EQ(16, das.access_column_ids_.at(0)); EXPECT_EQ(18, das.access_column_ids_.at(1));
    ASSERT_EQ(3, input.scan_->get_rowkey_cnt());
    ASSERT_GE(input.scan_->get_columns_desc().count(), 3);
    for (int64_t key = 0; key < 3; ++key) {
      EXPECT_EQ(16 + key, input.scan_->get_columns_desc().at(key).col_id_);
    }
    EXPECT_EQ(input.key_columns_[0], das.pd_expr_spec_.access_exprs_.at(0));
    EXPECT_EQ(input.key_columns_[2], das.pd_expr_spec_.access_exprs_.at(1));
    ASSERT_EQ(2, das.result_output_.count());
    EXPECT_EQ(input.key_columns_[2], das.result_output_.at(0));
    EXPECT_EQ(input.key_columns_[0], das.result_output_.at(1));
    ASSERT_EQ(9, input.physical_.get_expr_frame_info().rt_exprs_.count());
    int64_t columns = 0, equals = 0, parameters = 0;
    for (int64_t i = 0; i < input.physical_.get_expr_frame_info().rt_exprs_.count(); ++i) {
      const sql::ObExpr &expr = input.physical_.get_expr_frame_info().rt_exprs_.at(i);
      columns += expr.type_ == T_REF_COLUMN;
      equals += expr.type_ == T_OP_EQ;
      parameters += expr.type_ == T_QUESTIONMARK;
    }
    EXPECT_EQ(3, columns); EXPECT_EQ(3, equals); EXPECT_EQ(3, parameters);
    ASSERT_EQ(3, input.scan_->output_.count());
    EXPECT_EQ(input.key_columns_[2], input.scan_->output_.at(0));
    EXPECT_EQ(input.key_columns_[0], input.scan_->output_.at(1));
    EXPECT_EQ(input.key_columns_[2], input.scan_->output_.at(2));
    EXPECT_TRUE(input.scan_->filters_.empty()); EXPECT_TRUE(input.scan_->calc_exprs_.empty());
    EXPECT_TRUE(das.pd_expr_spec_.calc_exprs_.empty()); EXPECT_TRUE(das.pd_expr_spec_.pushdown_filters_.empty());
    EXPECT_TRUE(das.rowkey_exprs_.empty()); EXPECT_EQ(nullptr, input.pack_);
    // Old ae compiles this real graph but refuses it at pd.access.count==2.
    ASSERT_NE(nullptr, input.physical_.get_expr_frame_info().native_i64_point_layout_);
    for (const bool generic : {false, true}) {
      if (generic) { ASSERT_EQ(OB_SUCCESS, input.use_generic_parameter_type()); }
      for (int64_t column = 0; column < 3; ++column) {
        bool native = generic;
        ASSERT_EQ(OB_SUCCESS, input.key_columns_[column]->get_native_i64_mode(input.eval_, native));
        EXPECT_EQ(!generic, native);
      }
      const int64_t first[] = {INT64_MIN, 0, INT64_MIN, INT64_MIN};
      const int64_t third[] = {INT64_MAX, -1, INT64_MAX, INT64_MAX};
      const int64_t nulls[] = {0, 0, 1, 4};
      for (int64_t row = 0; row < 4; ++row) {
        blocksstable::ObStorageDatum values[2];
        if ((nulls[row] & 1) != 0) { values[0].set_null(); } else { values[0].set_int(first[row]); }
        if ((nulls[row] & 4) != 0) { values[1].set_null(); } else { values[1].set_int(third[row]); }
        int64_t nops = -1;
        ASSERT_EQ(OB_SUCCESS, input.project_two_storage_columns(values, row, nops)); ASSERT_EQ(0, nops);
        ASSERT_NO_FATAL_FAILURE(expect_duplicate_protocol_row(input, row, protocol, first[row], third[row], nulls[row]));
        ASSERT_NO_FATAL_FAILURE(expect_unproduced_point_column_unpublished(input, row));
        for (int64_t parameter = 0; parameter < 3; ++parameter) {
          EXPECT_EQ(nullptr, input.exec_.get_physical_plan_ctx()->locate_materialized_parameter(parameter));
        }
      }
    }
  }
}

TEST(NativeMysqlProtocol, actual_duplicate_point_declines_unproduced_consumers_foreign_access_and_misaligned_ids)
{
  using Inputs = NativePointProtocolInputs;
  for (const auto shape : {Inputs::MISSING_OUTPUT_STORAGE, Inputs::MISSING_FILTER_STORAGE,
                           Inputs::FOREIGN_STORAGE_ACCESS, Inputs::MISALIGNED_STORAGE_IDS}) {
    Inputs input;
    ASSERT_EQ(OB_SUCCESS, input.initialize(BINARY, false, false, shape));
    EXPECT_EQ(nullptr, input.physical_.get_expr_frame_info().native_i64_point_layout_);
    EXPECT_EQ(3, input.scan_->output_.count());
    EXPECT_EQ(3, input.scan_->get_rowkey_cnt());
    for (int64_t column = 0; column < 3; ++column) {
      EXPECT_EQ(nullptr, input.key_columns_[column]->native_i64_layout_);
      bool native = true;
      ASSERT_EQ(OB_SUCCESS, input.key_columns_[column]->get_native_i64_mode(input.eval_, native));
      EXPECT_FALSE(native);
    }
    const auto &ids = input.scan_->tsc_ctdef_.scan_ctdef_.access_column_ids_;
    if (shape == Inputs::MISALIGNED_STORAGE_IDS) {
      ASSERT_EQ(2, ids.count()); EXPECT_EQ(18, ids.at(0)); EXPECT_EQ(16, ids.at(1));
    }
    if (shape == Inputs::MISSING_FILTER_STORAGE) {
      ASSERT_EQ(1, input.scan_->filters_.count());
      EXPECT_EQ(input.key_columns_[1], input.scan_->filters_.at(0)->args_[0]);
    }
    if (shape == Inputs::FOREIGN_STORAGE_ACCESS) {
      ASSERT_EQ(3, ids.count()); EXPECT_EQ(19, ids.at(2));
    }
  }
}

TEST(NativeMysqlProtocol, inline_bigint_extremes_null_and_zerofill_match_existing_wire_cells)
{
  ObArenaAllocator owner;
  SqlTypeMeta meta;
  meta.set_int();
  ObAccuracy accuracy;
  accuracy.set_precision(20);
  accuracy.set_scale(0);
  SqlType sql_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, sql_type));
  const int64_t values[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
  for (const auto protocol : {TEXT, BINARY}) {
    for (const bool zerofill : {false, true}) {
      ObField field;
      field.type_meta_ = meta;
      field.flags_ = zerofill ? ZEROFILL_FLAG : 0;
      field.length_ = 24;
      for (const int64_t value : values) {
        Datum native;
        native.set_int(value);
        DatumValue legacy;
        ASSERT_EQ(OB_SUCCESS, legacy.set_int(value, owner));
        ObMySQLCellValue old_out, new_out;
        ObDataTypeCastParams cast_params;
        ObDatum physical;
        alignas(8) char physical_scratch[64];
        ASSERT_EQ(OB_SUCCESS, legacy.export_legacy(physical, physical_scratch, sizeof(physical_scratch)));
        ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_datum_cell_value(
            physical, meta, protocol, owner, old_out, &field, cast_params));
        ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_inline_bigint_cell_value(
            native, sql_type, protocol, owner, new_out, &field));
        EXPECT_EQ(neutral(OB_SUCCESS, old_out), neutral(OB_SUCCESS, new_out));
      }
      Datum null;
      ObMySQLCellValue out;
      ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_inline_bigint_cell_value(
          null, sql_type, protocol, owner, out, &field));
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, out.kind_);
    }
  }
}

TEST(NativeMysqlProtocol, inline_bigint_rejects_non_values_and_incompatible_fields)
{
  RejectOwner owner;
  SqlTypeMeta meta;
  meta.set_int();
  ObAccuracy accuracy;
  accuracy.set_precision(20);
  accuracy.set_scale(0);
  SqlType sql_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, sql_type));
  ObMySQLCellValue out;
  Datum input;
  input.set_nop();
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObSMUtils::build_inline_bigint_cell_value(
      input, sql_type, TEXT, owner, out, nullptr));
  input.set_min();
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObSMUtils::build_inline_bigint_cell_value(
      input, sql_type, BINARY, owner, out, nullptr));
  int64_t bytes = 42;
  ASSERT_EQ(OB_SUCCESS, input.set_reference(&bytes, sizeof(bytes)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObSMUtils::build_inline_bigint_cell_value(
      input, sql_type, TEXT, owner, out, nullptr));
  input.set_int(42);
  ObField field;
  field.type_meta_.set_uint64();
  EXPECT_EQ(OB_NOT_SUPPORTED, ObSMUtils::build_inline_bigint_cell_value(
      input, sql_type, BINARY, owner, out, &field));
  // Ordinary native integers need no scratch allocation for either protocol.
  ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_inline_bigint_cell_value(
      input, sql_type, TEXT, owner, out, nullptr));
  EXPECT_EQ("42", std::string(out.get_bytes(), out.get_bytes_len()));
}

TEST(NativeMysqlProtocol, result_row_adapter_encodes_authoritative_native_expression_slots)
{
  ObArenaAllocator owner;
  sql::ObExecContext exec(owner);
  alignas(8) char frame[1024]{};
  char *frames[] = {frame};
  exec.set_frames(frames);
  sql::ObEvalCtx eval(exec);
  sql::ObExpr expr[3];
  sql::ExprFixedArray exprs(owner);
  ASSERT_EQ(OB_SUCCESS, exprs.init(3));
  for (int i = 0; i < 3; ++i) {
    expr[i].obj_meta_.set_int(); expr[i].obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
    expr[i].frame_idx_ = 0; expr[i].datum_off_ = i * 64;
    ASSERT_EQ(OB_SUCCESS, expr[i].prepare_scalar_type(expr[i].obj_meta_, ObAccuracy(20,0), owner));
    new (frame + expr[i].datum_off_) Datum();
    ASSERT_EQ(OB_SUCCESS, exprs.push_back(&expr[i]));
  }
  expr[0].locate_expr_datum(eval).set_int(INT64_MIN);
  expr[2].locate_expr_datum(eval).set_int(INT64_MAX);
  sql::ResultRowView view;
  // The ordinary native row needs no compatibility scratch for the direct
  // integer encoder, while each column still has its real full Type owner.
  view.set_datums(exprs, eval, 0, false, nullptr);
  ASSERT_FALSE(view.has_legacy_datums());
  ObSEArray<ObField,4> fields;
  ObSEArray<int64_t,4> indexes;
  ObSEArray<DatumValue,4> fallback;
  for (int i = 0; i < 3; ++i) {
    ObField field; field.type_meta_.set_int();
    ASSERT_EQ(OB_SUCCESS, fields.push_back(field));
    ASSERT_TRUE(DatumMysqlRow::can_encode_inline(view, i, fields.at(i)));
    ASSERT_EQ(OB_SUCCESS, indexes.push_back(-3));
  }
  sql::ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1,1,nullptr));
  ObDataTypeCastParams cast_params;
  for (const auto protocol : {TEXT, BINARY}) {
    DatumMysqlRow row(protocol, view, indexes, fallback, cast_params, session, fields,
                       CHARSET_UTF8MB4, nullptr);
    for (int i = 0; i < 3; ++i) {
      ObMySQLCellValue actual, expected;
      ASSERT_EQ(OB_SUCCESS, row.build_cell_value(i, owner, actual));
      ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_inline_bigint_cell_value(
          expr[i].locate_expr_datum(eval), expr[i].get_sql_type(), protocol, owner, expected, &fields.at(i)));
      EXPECT_EQ(neutral(OB_SUCCESS, expected), neutral(OB_SUCCESS, actual));
    }
    view.set_legacy(nullptr);
    ObMySQLCellValue invalid;
    EXPECT_EQ(OB_INVALID_ARGUMENT, row.build_cell_value(0, owner, invalid));
    view.set_datums(exprs, eval, 0, false, nullptr);
  }
  fields.at(0).type_meta_.set_uint64();
  EXPECT_FALSE(DatumMysqlRow::can_encode_inline(view, 0, fields.at(0)));
}

TEST(NativeMysqlProtocol, incompatible_native_adapter_indexes_fail_without_legacy_scratch)
{
  ObArenaAllocator owner;
  sql::ObExecContext exec(owner);
  alignas(8) char frame[64]{}; char *frames[] = {frame}; exec.set_frames(frames);
  sql::ObEvalCtx eval(exec);
  sql::ObExpr expr;
  expr.obj_meta_.set_int(); expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
  expr.frame_idx_ = 0; expr.datum_off_ = 0;
  ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(20,0), owner));
  new (frame) Datum(); expr.locate_expr_datum(eval).set_int(42);
  sql::ExprFixedArray exprs(owner);
  ASSERT_EQ(OB_SUCCESS, exprs.init(1)); ASSERT_EQ(OB_SUCCESS, exprs.push_back(&expr));
  sql::ResultRowView view; view.set_datums(exprs, eval, 0, false, nullptr);
  ObSEArray<ObField, 1> fields;
  ObField field; field.type_meta_.set_int(); ASSERT_EQ(OB_SUCCESS, fields.push_back(field));
  ObSEArray<int64_t, 1> indexes; ASSERT_EQ(OB_SUCCESS, indexes.push_back(-1));
  ObSEArray<DatumValue, 1> fallback;
  sql::ObSQLSessionInfo session; ASSERT_EQ(OB_SUCCESS, session.test_init(1,1,nullptr));
  ObDataTypeCastParams params;
  for (int64_t index : {int64_t(-1), int64_t(-2)}) {
    indexes.at(0) = index;
    DatumMysqlRow row(BINARY, view, indexes, fallback, params, session, fields, CHARSET_UTF8MB4, nullptr);
    ObMySQLCellValue out; EXPECT_EQ(OB_STATE_NOT_MATCH, row.build_cell_value(0, owner, out));
  }
  indexes.at(0) = -3;
  view.set_legacy(nullptr); // a stale native encoding choice must not read a frame
  DatumMysqlRow row(BINARY, view, indexes, fallback, params, session, fields, CHARSET_UTF8MB4, nullptr);
  ObMySQLCellValue out; EXPECT_EQ(OB_INVALID_ARGUMENT, row.build_cell_value(0, owner, out));
}

TEST(NativeMysqlProtocol, all_old_formatter_payloads_and_errors_match_without_object_materialization)
{
  std::ifstream input("unittest/observer/data/mysql_protocol_values.golden");
  ASSERT_TRUE(input.good());
  sql::ObSQLSessionInfo session;
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto row = columns(line);
    ASSERT_EQ(21, row.size());
    SCOPED_TRACE(row[1] + ":protocol=" + row[2] + ":field=" + row[3] + ":oom=" + row[4]);
    ObArenaAllocator source, scratch;
    RejectOwner reject;
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, seed(row, source, value));
    const int mode = std::stoi(row[3]);
    ObField field;
    field.accuracy_.set_scale(mode == 1 ? -1 : mode == 2 ? 0 : mode == 3 ? 2 : 6);
    field.accuracy_.set_precision(mode == 4 ? 64 : mode == 3 ? 1 : 10);
    field.flags_ = mode == 5 ? ZEROFILL_FLAG : 0;
    field.length_ = mode == 5 ? 24 : 0;
    ObTimeZoneInfo zone;
    ObDataTypeCastParams parameters(&zone);
    ObIAllocator &owner = row[4] == "1" ? static_cast<ObIAllocator &>(reject)
                                         : static_cast<ObIAllocator &>(scratch);
    ObMySQLCellValue out;
    const int ret = ObSMUtils::build_cell_value(value,
        static_cast<MYSQL_PROTOCOL_TYPE>(std::stoi(row[2])), owner, out,
        parameters, mode == 0 ? nullptr : &field, session);
    std::string expected;
    for (size_t i = 6; i < row.size(); ++i) {
      if (i > 6) { expected += '\t'; }
      expected += row[i];
    }
    EXPECT_EQ(expected, neutral(ret, out));
    ++checked;
  }
  EXPECT_EQ(1784, checked);
}

TEST(NativeMysqlProtocol, typed_null_uses_datum_nullness_without_reading_a_scalar_payload)
{
  sql::ObSQLSessionInfo session;
  RejectOwner owner;
  ObTimeZoneInfo zone;
  ObDataTypeCastParams parameters(&zone);
  for (SqlValueType type : {ObIntType, ObNumberType, ObDateTimeType, ObVarcharType}) {
    DatumValue value;
    value.meta_.set_type(type);
    ASSERT_TRUE(value.datum_.is_null());
    for (auto protocol : {TEXT, BINARY}) {
      ObMySQLCellValue out;
      ASSERT_EQ(OB_SUCCESS, ObSMUtils::build_cell_value(value, protocol,
          owner, out, parameters, nullptr, session));
      EXPECT_EQ(ObMySQLCellValueKind::NULL_VALUE, out.kind_);
    }
  }
}

TEST(NativeMysqlProtocol, field_list_marker_and_anonymous_collection_type_are_metadata_only)
{
  ObField field;
  field.type_meta_.set_type(ObExtendType);
  field.type_meta_.set_extend_type(pl::PL_RECORD_TYPE);
  field.field_list_default_type_ = static_cast<uint32_t>(MYSQL_TYPE_LONG);

  ObMySQLField mysql_field;
  ASSERT_EQ(OB_SUCCESS, observer::ObMySQLResultSet::to_mysql_field(
      field, mysql_field, true));
  EXPECT_EQ(MYSQL_TYPE_COMPLEX, mysql_field.type_);
  EXPECT_EQ(MYSQL_TYPE_LONG, mysql_field.default_value_);
}

TEST(NativeMysqlProtocol, output_pack_scalar_native_and_generic_datum_formatters_match)
{
  std::ifstream input("unittest/observer/data/mysql_protocol_values.golden");
  ASSERT_TRUE(input.good());
  sql::ObSQLSessionInfo session;
  std::string line;
  int checked = 0;
  while (std::getline(input, line)) {
    const auto row = columns(line);
    ASSERT_EQ(21, row.size());
    if (row[4] == "1") { continue; } // this test compares values, not allocator failure injection
    ObArenaAllocator source, datum_scratch, generic_scratch;
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, seed(row, source, value));
    const SqlValueType obj_type = value.meta_.get_type();
    if (obj_type < 0 || obj_type >= ObMaxType) { continue; }
    const SqlValueDatumMapType map = ObDatum::get_sql_value_datum_map_type(obj_type);
    const bool supported = output_pack_scalar_datum_supported(value.meta_, map);
    if (supported) {
      ObField field;
      field.type_meta_.set_type(obj_type); // output_pack's native BINARY path requires an exact field type
      const int mode = std::stoi(row[3]);
      field.accuracy_.set_scale(mode == 1 ? -1 : mode == 2 ? 0 : mode == 3 ? 2 : 6);
      field.accuracy_.set_precision(mode == 4 ? 64 : mode == 3 ? 1 : 10);
      field.flags_ = mode == 5 ? ZEROFILL_FLAG : 0;
      field.length_ = mode == 5 ? 24 : 0;
      ObTimeZoneInfo zone;
      ObDataTypeCastParams parameters(&zone);
      for (auto protocol : {TEXT, BINARY}) {
        SCOPED_TRACE(row[1] + ", datum_type=" + std::to_string(static_cast<int>(obj_type))
                     + ", protocol=" + std::to_string(static_cast<int>(protocol))
                     + ", mode=" + std::to_string(mode)
                     + ", map=" + std::to_string(static_cast<int>(map)));
        ObMySQLCellValue datum_out, generic_out;
        ObDatum physical;
        alignas(8) char physical_scratch[64];
        ASSERT_EQ(OB_SUCCESS, value.export_legacy(physical, physical_scratch, sizeof(physical_scratch)));
        const int datum_ret = ObSMUtils::build_datum_cell_value(
            physical, value.meta_, protocol, datum_scratch, datum_out,
            &field, parameters);
        const int generic_ret = ObSMUtils::build_cell_value(
            value, protocol, generic_scratch, generic_out, parameters, &field, session);
        EXPECT_EQ(generic_ret, datum_ret);
        EXPECT_EQ(neutral(generic_ret, generic_out), neutral(datum_ret, datum_out));
        ++checked;
      }
    }
  }
  EXPECT_GT(checked, 0);
}

TEST(NativeMysqlProtocol, DISABLED_output_pack_scalar_formatter_ab)
{
  constexpr int64_t ITERATIONS = 20000;
  constexpr int64_t PAIRS = 7;
  std::ifstream input("unittest/observer/data/mysql_protocol_values.golden");
  ASSERT_TRUE(input.good());
  ObArenaAllocator source, preflight_datum_scratch, preflight_generic_scratch;
  sql::ObSQLSessionInfo session;
  ObTimeZoneInfo zone;
  ObDataTypeCastParams parameters(&zone);
  std::vector<CellFormatterBenchCase> cases;
  std::vector<int64_t> seen;
  std::string line;
  while (std::getline(input, line)) {
    const auto row = columns(line);
    ASSERT_EQ(21, row.size());
    // One accuracy-zero, successful sample per type and protocol is enough to
    // compare formatter cost; duplicate field settings and injected OOM rows
    // do not improve this microbenchmark.
    if (row[3] != "0" || row[4] != "0") { continue; }
    DatumValue value;
    ASSERT_EQ(OB_SUCCESS, seed(row, source, value));
    const SqlValueType type = value.meta_.get_type();
    if (type < 0 || type >= ObMaxType) { continue; }
    const SqlValueDatumMapType map = ObDatum::get_sql_value_datum_map_type(type);
    if (!output_pack_scalar_datum_supported(value.meta_, map)) { continue; }
    const MYSQL_PROTOCOL_TYPE protocol =
        static_cast<MYSQL_PROTOCOL_TYPE>(std::stoi(row[2]));
    const int64_t key = static_cast<int64_t>(type) * 4 + static_cast<int64_t>(protocol);
    if (std::find(seen.begin(), seen.end(), key) != seen.end()) { continue; }

    CellFormatterBenchCase candidate;
    candidate.value_ = value; // payload remains owned by the source arena
    candidate.map_ = map;
    candidate.protocol_ = protocol;
    candidate.field_.type_meta_.set_type(type);
    candidate.field_.accuracy_.set_scale(6);
    candidate.field_.accuracy_.set_precision(10);

    ObMySQLCellValue datum_out, generic_out;
    ObDatum physical;
    alignas(8) char physical_scratch[64];
    ASSERT_EQ(OB_SUCCESS, candidate.value_.export_legacy(physical, physical_scratch, sizeof(physical_scratch)));
    const int datum_ret = ObSMUtils::build_datum_cell_value(
        physical, candidate.value_.meta_, protocol,
        preflight_datum_scratch, datum_out, &candidate.field_, parameters);
    const int generic_ret = ObSMUtils::build_cell_value(
        candidate.value_, protocol, preflight_generic_scratch, generic_out,
        parameters, &candidate.field_, session);
    if (datum_ret != OB_SUCCESS || generic_ret != OB_SUCCESS) {
      EXPECT_EQ(generic_ret, datum_ret) << row[1] << ", protocol=" << int(protocol);
      continue; // only successful encodings are useful throughput samples
    }
    ASSERT_EQ(neutral(generic_ret, generic_out), neutral(datum_ret, datum_out));
    // Restrict the timed sample set to paths that do not allocate per cell.
    if (datum_out.get_bytes_len() > 0) {
      if (!datum_out.uses_local_buffer() || !generic_out.uses_local_buffer()) {
        continue;
      }
    }
    cases.push_back(candidate);
    seen.push_back(key);
  }
  ASSERT_FALSE(cases.empty());

  const int64_t cell_count = ITERATIONS * static_cast<int64_t>(cases.size());
  auto run_generic = [&](uint64_t &checksum) {
    for (int64_t i = 0; i < cell_count; ++i) {
      const CellFormatterBenchCase &item = cases[i % cases.size()];
      ObMySQLCellValue cell;
      const int ret = ObSMUtils::build_cell_value(
          item.value_, item.protocol_, preflight_generic_scratch,
          cell, parameters, &item.field_, session);
      checksum += static_cast<uint64_t>(ret) + cell_checksum(cell);
    }
  };
  auto run_native = [&](uint64_t &checksum) {
    for (int64_t i = 0; i < cell_count; ++i) {
      const CellFormatterBenchCase &item = cases[i % cases.size()];
      ObMySQLCellValue cell;
      ObDatum physical;
      alignas(8) char physical_scratch[64];
      const int export_ret = item.value_.export_legacy(physical, physical_scratch, sizeof(physical_scratch));
      const int ret = export_ret == OB_SUCCESS ? ObSMUtils::build_datum_cell_value(
          physical, item.value_.meta_, item.protocol_,
          preflight_datum_scratch, cell, &item.field_, parameters) : export_ret;
      checksum += static_cast<uint64_t>(ret) + cell_checksum(cell);
    }
  };

  // Warm up both paths; benchmark assertions concern semantic equality only.
  uint64_t warm_generic = 0, warm_native = 0;
  run_generic(warm_generic);
  run_native(warm_native);
  ASSERT_EQ(warm_generic, warm_native);

  std::vector<double> generic_wall, generic_cpu, native_wall, native_cpu;
  generic_wall.reserve(PAIRS); generic_cpu.reserve(PAIRS);
  native_wall.reserve(PAIRS); native_cpu.reserve(PAIRS);
  uint64_t checksum_generic = 0, checksum_native = 0;
  for (int64_t pair = 0; pair < PAIRS; ++pair) {
    BenchSample generic_first, native_first, native_second, generic_second;
    if (pair % 2 == 0) {
      generic_first = measure_cells(run_generic, cell_count, checksum_generic);
      native_first = measure_cells(run_native, cell_count, checksum_native);
      native_second = measure_cells(run_native, cell_count, checksum_native);
      generic_second = measure_cells(run_generic, cell_count, checksum_generic);
    } else {
      native_first = measure_cells(run_native, cell_count, checksum_native);
      generic_first = measure_cells(run_generic, cell_count, checksum_generic);
      generic_second = measure_cells(run_generic, cell_count, checksum_generic);
      native_second = measure_cells(run_native, cell_count, checksum_native);
    }
    generic_wall.push_back((generic_first.wall_ns_per_cell_ + generic_second.wall_ns_per_cell_) / 2.0);
    generic_cpu.push_back((generic_first.cpu_ns_per_cell_ + generic_second.cpu_ns_per_cell_) / 2.0);
    native_wall.push_back((native_first.wall_ns_per_cell_ + native_second.wall_ns_per_cell_) / 2.0);
    native_cpu.push_back((native_first.cpu_ns_per_cell_ + native_second.cpu_ns_per_cell_) / 2.0);
  }
  ASSERT_EQ(checksum_generic, checksum_native);

  const double generic_wall_ns = median(generic_wall);
  const double native_wall_ns = median(native_wall);
  const double generic_cpu_ns = median(generic_cpu);
  const double native_cpu_ns = median(native_cpu);
  std::cout << "Datum cell formatter A/B: cases=" << cases.size()
            << " cells/sample=" << cell_count << " paired_samples=" << PAIRS
            << " generic_wall_ns/cell=" << generic_wall_ns
            << " datum_wall_ns/cell=" << native_wall_ns
            << " wall_speedup=" << generic_wall_ns / native_wall_ns
            << " generic_cpu_ns/cell=" << generic_cpu_ns
            << " datum_cpu_ns/cell=" << native_cpu_ns
            << " cpu_speedup=" << generic_cpu_ns / native_cpu_ns
            << " generic_cells/s=" << 1.0e9 / generic_wall_ns
            << " datum_cells/s=" << 1.0e9 / native_wall_ns
            << " checksum=" << checksum_generic << "\n";
}
