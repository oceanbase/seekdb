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

#ifndef _OB_PS_SESSION_INFO_H
#define _OB_PS_SESSION_INFO_H 1
#include "share/ob_define.h"
#include "lib/container/ob_2d_array.h"
#include "lib/objectpool/ob_pool.h"
#include "rpc/obmysql/ob_mysql_global.h" // for EMySQLFieldType
#include "sql/resolver/ob_stmt.h"
namespace oceanbase
{
namespace sql
{
static const int64_t OB_SESSION_SMALL_BLOCK_SIZE = 4 * 1024LL;

// Each prepared statements has one object of this type in the session.
// The object is immutable during the life of the prepared statement.
class ObPsSessionInfo
{
public:
  /*
    sizeof(oceanbase::obmysql::EMySQLFieldType)=4
    sizeof(oceanbase::sql::ObPsSessionInfo)=688
    ObPsSessionInfo is responsible for memory allocation by SmallBlockAllocator, with a small block size of 4K, so we need to adjust the definition of ParamsType to make the size
    as small as possible, and after cutting with 4K blocks, there should not be too much waste. 4096-688*5=656, each 4K block will waste this much memory. That is, every 5 ps wastes 656 bytes,
    which can be accepted.
    The one-dimensional array storing blocks in 2DArray is SEArray, and the size of its Local array is adjustable. On one hand, the maximum number of bind variables allowed by MySQL is
    65535, if we want to avoid memory allocation for the block array in the two-dimensional array, 65535*4/4K=64 small blocks are needed. Therefore, the size of SEArray is set to 64.
   */
  typedef common::ObSegmentArray<oceanbase::obmysql::EMySQLFieldType, OB_SESSION_SMALL_BLOCK_SIZE,
                            common::ObWrapperAllocator,
                            false /* use set alloc */, common::ObSEArray<char *, 64> > ParamsType;
public:
  ObPsSessionInfo()
      : sql_id_(0),
        params_count_(0),
        stmt_type_(stmt::T_NONE),
        params_type_(),
        prepare_sql_(),
        is_dml_(false)
  {}
  ~ObPsSessionInfo() {};

  void init(const common::ObWrapperAllocator &block_alloc) {params_type_.set_block_allocator(block_alloc);}

  uint64_t get_sql_id() const { return this->sql_id_; }
  void set_sql_id(uint64_t sql_id) { this->sql_id_ = sql_id; }

  int64_t get_params_count() const { return this->params_count_; }
  void set_params_count(int64_t params_count) { this->params_count_ = params_count; }

  //const stmt::StmtType &get_stmt_type() const { return this->stmt_type_; }
  //void set_stmt_type(const stmt::StmtType &stmt_type) { this->stmt_type_ = stmt_type; }

  int set_params_type(const common::ObIArray<obmysql::EMySQLFieldType> &types);
  const ParamsType &get_params_type() const {return params_type_;};

  void set_prepare_sql(const common::ObString sql) {prepare_sql_ = sql;}
  common::ObString get_prepare_sql() const {return prepare_sql_;}
  void set_dml(const bool is_dml) {is_dml_ = is_dml;}
  bool is_dml() const {return is_dml_;}
private:
  DISALLOW_COPY_AND_ASSIGN(ObPsSessionInfo);
private:
  uint64_t sql_id_; //key of plan cache
  int64_t params_count_;
  stmt::StmtType stmt_type_; // used by query result cache before got the plan
  ParamsType params_type_; /* params type */
  common::ObString prepare_sql_;
  bool is_dml_;            // is dml;  only DML has physical plan
};

inline int ObPsSessionInfo::set_params_type(const common::ObIArray<obmysql::EMySQLFieldType> &types)
{
  int ret = common::OB_SUCCESS;
  if (OB_UNLIKELY(params_count_ != types.count())) {
    ret = common::OB_ERR_UNEXPECTED;
    SQL_LOG(ERROR, "invalid params type count",
            "expected_count", params_count_, "real_count", types.count());
  } else if (0 < types.count()) {
    params_type_.reserve(types.count());
    // bound types
    if (OB_FAIL(params_type_.assign(types))) {
      SQL_LOG(WARN, "no memory");
      params_type_.reset();
    }
    SQL_LOG(DEBUG, "set ps param", K_(sql_id), "param_count", params_type_.count());
  }
  return ret;
}
}
} // end namespace oceanbase

#endif /* _OB_PS_SESSION_INFO_H */
