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

#ifndef _OB_MYSQL_RESULT_SET_H_
#define _OB_MYSQL_RESULT_SET_H_

#include "rpc/obmysql/ob_mysql_field.h"
#include "rpc/obmysql/ob_mysql_row.h"
#include "sql/ob_result_set.h"

using namespace oceanbase::sql;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObIEndTransCallback;
}

namespace observer
{

using obmysql::ObMySQLField;
using obmysql::ObMySQLRow;

class ObMySQLResultSet
  : public ObResultSet, public common::ObDLinkBase<ObMySQLResultSet>
{
public:
  /**
   * Constructor
   *
   * @param [in] obrs Dataset returned by SQL execution
   */
  ObMySQLResultSet(sql::ObSQLSessionInfo &session, common::ObIAllocator &allocator)
      : ObResultSet(session, allocator), field_index_(0), param_index_(0), has_more_result_(false)
  {
    is_user_sql_ = true;
  }

  /**
   * Destructor
   */
  virtual ~ObMySQLResultSet() {};

  /**
   * Return the information of the next field
   *
   * @param [out] obmf Information of the next field
   *
   * @return Returns OB_SUCCESS on success. If there is no data, returns Ob_ITER_END
   */
  int next_field(ObMySQLField &obmf);

  /**
   * return next param
   *
   * @parm [out] obmp next param
   * @return return OB_SUCCESS if succeed, else return OB_ITER_END
   */
  int next_param(ObMySQLField &obmf);

  /**
   * For Multi-Query, indicates whether it is the last resultset
   */
  bool has_more_result() const
  {
    return has_more_result_;
  }
  void set_has_more_result(bool has_more)
  {
    has_more_result_ = has_more;
  }

  /**
   * Get the data of the next row
   *
   * @param [out] obmr Data of the next row
   *
   * @return Returns OB_SUCCESS on success. If there is no data, returns Ob_ITER_END
   */
  int next_row(const ObNewRow *&obmr);
  int64_t to_string(char *buf, const int64_t buf_len) const;
  int32_t get_type() {return 0;};
  static int to_mysql_field(const ObField &field, ObMySQLField &mfield);
  static int to_new_result_field(const ObField &field, ObMySQLField &mfield);
  static int to_oracle_field(const ObField &field, ObMySQLField &mfield);
  static void switch_ps(ObPrecision &pre, ObScale &scale, EMySQLFieldType type);

private:
  int64_t field_index_;     /**< The index of the next field to be read */
  int64_t param_index_;     /* < The index of the next parameter to be read */
  bool has_more_result_;
}; // end class ObMySQLResultSet

inline int64_t ObMySQLResultSet::to_string(char *buf, const int64_t buf_len) const
{
  return ObResultSet::to_string(buf, buf_len);
}

inline int ObMySQLResultSet::next_row(const ObNewRow *&obmr)
{
  return ObResultSet::get_next_row(obmr);
}

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_RESULT_SET_H_ */
