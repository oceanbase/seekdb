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

#ifndef OCEANBASE_SRC_PL_SYS_PACKAGE_OB_DBMS_SQL_H_
#define OCEANBASE_SRC_PL_SYS_PACKAGE_OB_DBMS_SQL_H_

#include "pl/ob_pl_type.h"
#include "pl/ob_pl_user_type.h"

namespace oceanbase
{
namespace sql
{
class ObExecContext;
}
namespace pl
{

typedef common::ParamStore ParamStore;

class ObDbmsInfo
{
  /*
   * The initial purpose of extracting this structure was to
   * investigate Oracle's behavior, and the dbms_sql param information is not strongly bound to dbms_cursor
   * Even if dbms_cursor is reopened or closed,
   * as long as the param information is not modified through methods like parse, bind_value, column_value,
   * the value of param will not change
   * 
   * Previously placed inside dbms_cursor, relying on cursor.entity's memory management,
   * cursor would clean up its own memory when reopening and closing, leading to unexpected values for param,
   * 
   * After refactoring, dbmsinfo has a separate memory management approach from cursor, avoiding such issues
   */
public:
  ObDbmsInfo(common::ObIAllocator &alloc)
    : sql_stmt_(),
      stmt_type_(sql::stmt::T_NONE),
      entity_(nullptr),
      ps_sql_(),
      param_names_(alloc),
      into_names_(alloc),
      bind_params_(alloc),
      exec_params_(/*ObWrapperAllocator(&alloc)*/),
      fields_(),
      define_columns_(),
      fetch_rows_() {}
  void reset();
  inline void reuse() {}
  int init();

  inline lib::MemoryContext &get_dbms_entity() { return entity_; }
  inline const lib::MemoryContext get_dbms_entity() const { return entity_; }
  ObIAllocator &get_area_allocator() { return entity_->get_arena_allocator(); }
  inline common::ObString &get_ps_sql() { return ps_sql_; }
  inline void set_ps_sql(ObString sql) { ps_sql_ = sql; }
  common::ObString &get_sql_stmt() { return sql_stmt_; }
  sql::stmt::StmtType get_stmt_type() const { return stmt_type_; }
  inline void set_stmt_type(sql::stmt::StmtType type) { stmt_type_ = type; }
  ParamStore &get_exec_params() { return exec_params_; }
  common::ColumnsFieldArray &get_field_columns() { return fields_; }
  static int deep_copy_field_columns(
    ObIAllocator& allocator,
    const common::ColumnsFieldIArray* src_fields,
    common::ColumnsFieldArray &dst_fields);

  int init_params(int64_t param_count);
  int64_t get_param_name_count() const { return param_names_.count(); }
  ObIArray<ObString>& get_param_names() { return param_names_; }

  int add_param_name(ObString &clone_name);
  ObIArray<ObString> &get_into_names() { return into_names_; }
protected:

  class BindParam
  {
  public:
    BindParam()
      : param_name_(),
        param_value_()
    {}
    BindParam(const common::ObString &param_name,
              const common::ObObjParam &param_value)
      : param_name_(param_name),
        param_value_(param_value)
    {}
    TO_STRING_KV(K(param_name_), K(param_value_));
  public:
    /*
     * Considering the BIND_ARRAY interface, the following modifications are needed:
     * 1. param_value_ needs to be expanded to an array;
     * 2. Record the lower and upper bounds, defaulting to 0 and size - 1;
     * 3. Record the current iteration position for use by the expand_next_params interface;
     */
    common::ObString param_name_;
    common::ObObjParam param_value_;
  };
  typedef common::ObFixedArray<common::ObString, common::ObIAllocator> ParamNames;
  typedef common::ObFixedArray<common::ObString, common::ObIAllocator> IntoNames;
  typedef common::ObFixedArray<BindParam, common::ObIAllocator> BindParams;

public:
  struct ArrayDesc
  {
    ArrayDesc() :
      id_(OB_INVALID_ID),
      cnt_(OB_INVALID_COUNT),
      lower_bnd_(OB_INVALID_INDEX),
      cur_idx_(OB_INVALID_INDEX),
      type_() {}
    ArrayDesc(uint64_t id, int64_t cnt, int64_t lower_bnd, ObDataType type) :
      id_(id),
      cnt_(cnt),
      lower_bnd_(lower_bnd),
      cur_idx_(0),
      type_(type) {}
    uint64_t id_;
    int64_t cnt_;
    int64_t lower_bnd_;
    int64_t cur_idx_;
    ObDataType type_;
  };

  typedef common::hash::ObHashMap<int64_t, ArrayDesc,
                                            common::hash::NoPthreadDefendMode> DefineArrays;
  inline const DefineArrays &get_define_arrays() const { return define_arrays_; }
  inline DefineArrays &get_define_arrays() { return define_arrays_; }

  typedef common::hash::ObHashMap<int64_t, int64_t,
                                          common::hash::NoPthreadDefendMode> DefineColumns;
  inline const DefineColumns &get_define_columns() const { return define_columns_; }

  /*
   * TODO: use hashmap may better?
   */
  typedef common::ObSEArray<ObNewRow, 16> RowBuffer;
  inline const RowBuffer &get_fetch_rows() const { return fetch_rows_; }
  inline RowBuffer &get_fetch_rows() { return fetch_rows_; }

protected:
  common::ObString sql_stmt_;
  sql::stmt::StmtType stmt_type_;
private:
  lib::MemoryContext entity_;
  common::ObString ps_sql_;
  ParamNames  param_names_;
  IntoNames   into_names_;
  BindParams  bind_params_;
  ParamStore exec_params_;
  common::ColumnsFieldArray fields_;
  DefineColumns define_columns_; //key: column pos, value: column size
  DefineArrays define_arrays_;
  RowBuffer fetch_rows_;
};

class ObDbmsCursorInfo : public ObPLCursorInfo, public ObDbmsInfo
{
public:
  // cursor id in OB
  /* ps cursor : always equal with stmt id, always smaller than candidate_cursor_id_
   * ref cursor : always start with CANDIDATE_CURSOR_ID, user can't get ref cursor id by SQL
   *              only client and server use the id when interacting
   * dbms sql cursor : always start with CANDIDATE_CURSOR_ID, user can get cursor id by SQL
   *              CANDIDATE_CURSOR_ID may be out of precision. 
   *              so we should use get_dbms_id and convert_to_dbms_cursor_id to provide a vaild id for user
   */ 
  static const int64_t CANDIDATE_CURSOR_ID = 1LL << 31;

public:
  ObDbmsCursorInfo(common::ObIAllocator &alloc)
    : ObPLCursorInfo(true),
      ObDbmsInfo(alloc),
      affected_rows_(-1) { }
  virtual ~ObDbmsCursorInfo() { reset(); }
  virtual int close(sql::ObSQLSessionInfo &session, 
                    bool is_cursor_reuse = false, 
                    bool is_dbms_reuse = false);

public:
  int init();
  void reset();
  void reset_private();
  inline void reset_dbms_info() {ObDbmsInfo::reset();};
  void set_affected_rows(int64_t affected_rows) { affected_rows_ = affected_rows; }
  int64_t get_affected_rows() const { return affected_rows_; }
  int prepare_entity(sql::ObSQLSessionInfo &session);
  int64_t search_array(const ObString &name, ObIArray<ObString> &array);

private:
  // affected_rows_ will be reset every time open is called
  int64_t affected_rows_;
};

}
}

#endif /* OCEANBASE_SRC_PL_SYS_PACKAGE_OB_DBMS_SQL_H_ */
