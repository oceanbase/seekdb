/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#ifndef _OB_MYSQL_ROW_H_
#define _OB_MYSQL_ROW_H_

#include "rpc/obmysql/ob_mysql_util.h"


namespace oceanbase
{
namespace obmysql
{

class ObMySQLRow
{
public:
  explicit ObMySQLRow(MYSQL_PROTOCOL_TYPE type) : type_(type), is_packed_(false) {}

public:
  /**
   * Serialize this row of data into a format recognized by MySQL, output position: buf + pos, after execution pos points to the first free position in buf.
   *
   * @param [in] buf The space for the serialized output sequence
   * @param [in] len Length of buf
   * @param [out] pos Current first free position in buf
   *
   * @return Returns OB_SUCCESS on success, returns oceanbase error code on failure
   */
  int serialize(char *buf, const int64_t len, int64_t &pos) const;
  bool is_packed() const { return is_packed_; }
  void set_packed(const bool is_packed) { is_packed_ = is_packed; }
protected:
  virtual int64_t get_cells_cnt() const = 0;
  virtual int encode_cell(
      int64_t idx, char *buf,
      int64_t len, int64_t &pos, char *bitmap) const = 0;

protected:
  const MYSQL_PROTOCOL_TYPE type_;
  //parallel encoding of output_expr in advance to speed up packet response
  bool is_packed_;
}; // end class ObMySQLRow

} // end of namespace obmysql
} // end of namespace oceanbase

#endif /* _OB_MYSQL_ROW_H_ */
