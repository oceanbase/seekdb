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

#include <cstring>
#include <string>

#include "gtest/gtest.h"
#include "common/mysqlclient/ob_isql_result_handler.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "share/ob_global_stat_proxy.h"
#include "share/ob_schema_version_info.h"

namespace oceanbase
{
namespace unittest
{

using namespace common;
using namespace common::sqlclient;
using namespace share;

static std::string hex_literal(const char *text)
{
  static const char digits[] = "0123456789ABCDEF";
  std::string result("X'");
  for (const unsigned char *it = reinterpret_cast<const unsigned char *>(text); '\0' != *it; ++it) {
    result.push_back(digits[*it >> 4]);
    result.push_back(digits[*it & 0x0F]);
  }
  result.push_back('\'');
  return result;
}

struct ResultRow
{
  ResultRow() : row_id_(OB_INVALID_INDEX), name_(), value_() {}
  ResultRow(const int64_t row_id, const char *name, const char *value)
      : row_id_(row_id), name_(ObString::make_string(name)), value_(ObString::make_string(value))
  {}
  int64_t row_id_;
  ObString name_;
  ObString value_;
  TO_STRING_KV(K_(row_id), K_(name), K_(value));
};

class RecordingMySQLResult : public ObMySQLResult
{
public:
  explicit RecordingMySQLResult(const ObIArray<ResultRow> &rows)
      : rows_(rows), row_idx_(-1)
  {}
  virtual ~RecordingMySQLResult() {}

  virtual int64_t get_column_count() const override { return 3; }
  virtual int close() override { return OB_SUCCESS; }
  virtual int next() override
  {
    ++row_idx_;
    return row_idx_ < rows_.count() ? OB_SUCCESS : OB_ITER_END;
  }
  virtual int get_int(const int64_t col_idx, int64_t &int_val) const override
  {
    int ret = check_position();
    if (OB_SUCC(ret) && 0 != col_idx) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_SUCC(ret)) {
      int_val = rows_.at(row_idx_).row_id_;
    }
    return ret;
  }
  virtual int get_varchar(const int64_t col_idx, ObString &value) const override
  {
    int ret = check_position();
    if (OB_FAIL(ret)) {
    } else if (1 == col_idx) {
      value = rows_.at(row_idx_).name_;
    } else if (2 == col_idx) {
      value = rows_.at(row_idx_).value_;
    } else {
      ret = OB_INVALID_ARGUMENT;
    }
    return ret;
  }

#define DEFINE_UNSUPPORTED_INDEX_GETTER(method, type) \
  virtual int method(const int64_t, type &) const override { return OB_NOT_SUPPORTED; }
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_uint, uint64_t)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_datetime, int64_t)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_date, int32_t)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_time, int64_t)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_year, uint8_t)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_bool, bool)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_float, float)
  DEFINE_UNSUPPORTED_INDEX_GETTER(get_double, double)
#undef DEFINE_UNSUPPORTED_INDEX_GETTER

  virtual int get_timestamp(const int64_t, const ObTimeZoneInfo *, int64_t &) const override
  { return OB_NOT_SUPPORTED; }
  virtual int get_type(const int64_t, ObObjMeta &) const override
  { return OB_NOT_SUPPORTED; }
  virtual int get_obj(const int64_t, ObObj &, const ObTimeZoneInfo *, ObIAllocator *) const override
  { return OB_NOT_SUPPORTED; }

#define DEFINE_UNSUPPORTED_NAME_GETTER(method, type) \
  virtual int method(const char *, type &) const override { return OB_NOT_SUPPORTED; }
  DEFINE_UNSUPPORTED_NAME_GETTER(get_int, int64_t)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_uint, uint64_t)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_datetime, int64_t)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_date, int32_t)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_time, int64_t)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_year, uint8_t)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_bool, bool)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_varchar, ObString)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_float, float)
  DEFINE_UNSUPPORTED_NAME_GETTER(get_double, double)
#undef DEFINE_UNSUPPORTED_NAME_GETTER

  virtual int get_timestamp(const char *, const ObTimeZoneInfo *, int64_t &) const override
  { return OB_NOT_SUPPORTED; }
  virtual int get_type(const char *, ObObjMeta &) const override
  { return OB_NOT_SUPPORTED; }
  virtual int get_obj(const char *, ObObj &) const override
  { return OB_NOT_SUPPORTED; }

private:
  int check_position() const
  {
    return row_idx_ >= 0 && row_idx_ < rows_.count() ? OB_SUCCESS : OB_ERR_UNEXPECTED;
  }
  virtual int inner_get_number(const int64_t, number::ObNumber &,
                               ObMySQLResult::IAllocator &) const override
  { return OB_NOT_SUPPORTED; }
  virtual int inner_get_number(const char *, number::ObNumber &,
                               ObMySQLResult::IAllocator &) const override
  { return OB_NOT_SUPPORTED; }

  const ObIArray<ResultRow> &rows_;
  int64_t row_idx_;
};

class RecordingResultHandler : public common::sqlclient::ObISQLResultHandler
{
public:
  explicit RecordingResultHandler(ObArray<ResultRow> &rows) : result_(rows) {}
  virtual ~RecordingResultHandler() {}
  virtual common::sqlclient::ObMySQLResult *mysql_result() override { return &result_; }
private:
  RecordingMySQLResult result_;
};

class RecordingSQLClient : public ObISQLClient
{
public:
  RecordingSQLClient()
      : read_count_(0), write_count_(0), physical_affected_rows_(1), write_ret_(OB_SUCCESS),
        last_sql_(), rows_()
  {}

  void reset_write(const int64_t physical_affected_rows)
  {
    read_count_ = 0;
    write_count_ = 0;
    physical_affected_rows_ = physical_affected_rows;
    write_ret_ = OB_SUCCESS;
    last_sql_.clear();
  }

  virtual int escape(const char *from, const int64_t from_size, char *to,
                     const int64_t to_size, int64_t &out_size) override
  {
    int ret = OB_SUCCESS;
    out_size = 0;
    if (NULL == from || from_size < 0 || NULL == to || to_size < from_size) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      MEMCPY(to, from, from_size);
      out_size = from_size;
    }
    return ret;
  }

  virtual int read(ReadResult &res, const char *, const int32_t) override
  {
    ++read_count_;
    RecordingResultHandler *handler = NULL;
    return res.create_handler(handler, rows_);
  }

  virtual int write(const char *sql, const int32_t, int64_t &affected_rows) override
  {
    ++write_count_;
    last_sql_ = NULL == sql ? "" : sql;
    affected_rows = physical_affected_rows_;
    return write_ret_;
  }

  virtual ObISQLConnection *get_connection() override { return NULL; }

  int64_t read_count_;
  int64_t write_count_;
  int64_t physical_affected_rows_;
  int write_ret_;
  std::string last_sql_;
  ObArray<ResultRow> rows_;
};

class GlobalStatProxyTest : public ::testing::Test
{
protected:
  static int atomic_upsert(ObCoreTableProxy &proxy, const int64_t row_id,
                           const ObIArray<ObCoreTableProxy::UpdateCell> &cells,
                           int64_t &affected_rows)
  {
    return proxy.atomic_incremental_upsert_row(row_id, cells, affected_rows);
  }

  static ObCoreTableProxy::UpdateCell make_cell(const char *name, const char *value,
                                                 const bool is_filter = false)
  {
    ObCoreTableProxy::UpdateCell cell;
    cell.is_filter_cell_ = is_filter;
    cell.cell_.name_ = ObString::make_string(name);
    cell.cell_.value_ = ObString::make_string(value);
    return cell;
  }
};

TEST_F(GlobalStatProxyTest, incremental_setter_uses_one_targeted_upsert)
{
  const int64_t physical_counts[] = {0, 1, 2, 7};
  for (int64_t i = 0; i < ARRAYSIZEOF(physical_counts); ++i) {
    RecordingSQLClient client;
    client.reset_write(physical_counts[i]);
    ObGlobalStatProxy proxy(client);
    ASSERT_EQ(OB_SUCCESS, proxy.set_baseline_schema_version(123));
    EXPECT_EQ(0, client.read_count_);
    EXPECT_EQ(1, client.write_count_);
    EXPECT_NE(std::string::npos, client.last_sql_.find("INSERT INTO __all_core_table"));
    EXPECT_NE(std::string::npos, client.last_sql_.find(", 1, "));
    EXPECT_NE(std::string::npos,
              client.last_sql_.find("X'626173656C696E655F736368656D615F76657273696F6E'"));
    EXPECT_NE(std::string::npos, client.last_sql_.find("X'313233'"));
    EXPECT_NE(std::string::npos, client.last_sql_.find("values(column_value) != -1"));
    EXPECT_EQ(std::string::npos, client.last_sql_.find("SELECT"));
  }
}

TEST_F(GlobalStatProxyTest, invalid_version_special_case_is_preserved)
{
  RecordingSQLClient client;
  ObGlobalStatProxy proxy(client);
  ASSERT_EQ(OB_SUCCESS, proxy.set_baseline_schema_version(OB_INVALID_SCHEMA_VERSION));
  EXPECT_EQ(0, client.read_count_);
  EXPECT_EQ(1, client.write_count_);
  EXPECT_NE(std::string::npos, client.last_sql_.find("X'2D31'"));
  EXPECT_NE(std::string::npos, client.last_sql_.find("values(column_value) != -1"));
}

TEST_F(GlobalStatProxyTest, every_incremental_setter_targets_its_own_field)
{
  RecordingSQLClient client;
  ObGlobalStatProxy proxy(client);
  const auto expect_target = [&client](const char *field) {
    EXPECT_EQ(0, client.read_count_);
    EXPECT_EQ(1, client.write_count_);
    EXPECT_NE(std::string::npos, client.last_sql_.find(hex_literal(field)));
    EXPECT_NE(std::string::npos, client.last_sql_.find(", 1, "));
    EXPECT_EQ(std::string::npos, client.last_sql_.find("SELECT"));
  };

  client.reset_write(1);
  ASSERT_EQ(OB_SUCCESS, proxy.set_core_schema_version(101));
  expect_target("core_schema_version");
  client.reset_write(1);
  ASSERT_EQ(OB_SUCCESS, proxy.set_sys_schema_version(102));
  expect_target("sys_schema_version");
  client.reset_write(1);
  ASSERT_EQ(OB_SUCCESS, proxy.set_normal_schema_version(103));
  expect_target("normal_schema_version");
  client.reset_write(1);
  ASSERT_EQ(OB_SUCCESS, proxy.set_baseline_schema_version(104));
  expect_target("baseline_schema_version");
  client.reset_write(1);
  ASSERT_EQ(OB_SUCCESS, proxy.set_ddl_epoch(105));
  expect_target("ddl_epoch");
}

TEST_F(GlobalStatProxyTest, helper_validates_and_sorts_cells)
{
  RecordingSQLClient client;
  ObCoreTableProxy proxy("__all_global_stat", client);
  int64_t affected_rows = -1;
  ObArray<ObCoreTableProxy::UpdateCell> cells;

  EXPECT_EQ(OB_INVALID_ARGUMENT, atomic_upsert(proxy, 1, cells, affected_rows));
  ASSERT_EQ(OB_SUCCESS, cells.push_back(make_cell("a", "1", true)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, atomic_upsert(proxy, 1, cells, affected_rows));
  cells.reset();
  ASSERT_EQ(OB_SUCCESS, cells.push_back(make_cell("", "1")));
  EXPECT_EQ(OB_INVALID_ARGUMENT, atomic_upsert(proxy, 1, cells, affected_rows));
  cells.reset();
  ASSERT_EQ(OB_SUCCESS, cells.push_back(make_cell("a", "1")));
  EXPECT_EQ(OB_INVALID_ARGUMENT, atomic_upsert(proxy, 2, cells, affected_rows));
  ASSERT_EQ(OB_SUCCESS, cells.push_back(make_cell("a", "2")));
  EXPECT_EQ(OB_INVALID_ARGUMENT, atomic_upsert(proxy, 1, cells, affected_rows));

  cells.reset();
  ASSERT_EQ(OB_SUCCESS, cells.push_back(make_cell("b", "2")));
  ASSERT_EQ(OB_SUCCESS, cells.push_back(make_cell("a", "1")));
  client.reset_write(7);
  ASSERT_EQ(OB_SUCCESS, atomic_upsert(proxy, 1, cells, affected_rows));
  EXPECT_EQ(1, affected_rows);
  const size_t a_pos = client.last_sql_.find("X'61'");
  const size_t b_pos = client.last_sql_.find("X'62'");
  EXPECT_NE(std::string::npos, a_pos);
  EXPECT_NE(std::string::npos, b_pos);
  EXPECT_LT(a_pos, b_pos);
}

TEST_F(GlobalStatProxyTest, multiple_logical_rows_are_still_rejected_on_read)
{
  RecordingSQLClient client;
  ASSERT_EQ(OB_SUCCESS,
            client.rows_.push_back(ResultRow(1, "baseline_schema_version", "100")));
  ASSERT_EQ(OB_SUCCESS,
            client.rows_.push_back(ResultRow(2, "baseline_schema_version", "200")));
  ObGlobalStatProxy proxy(client);
  int64_t baseline_schema_version = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, proxy.get_baseline_schema_version(baseline_schema_version));
  EXPECT_EQ(1, client.read_count_);
  EXPECT_EQ(0, client.write_count_);
}

} // namespace unittest
} // namespace oceanbase
