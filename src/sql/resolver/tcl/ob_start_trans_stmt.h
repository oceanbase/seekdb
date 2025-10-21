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

#ifndef _OB_START_TRANS_STMT_H
#define _OB_START_TRANS_STMT_H
#include "sql/resolver/tcl/ob_tcl_stmt.h"
namespace oceanbase
{
namespace sql
{
class ObStartTransStmt: public ObTCLStmt
{
public:
  ObStartTransStmt();
  virtual ~ObStartTransStmt();
  virtual void print(FILE *fp, int32_t level, int32_t index);
  void set_read_only(bool val);
  bool get_read_only() const;
  void set_with_consistent_snapshot(bool val);
  bool get_with_consistent_snapshot() const;
  virtual bool cause_implicit_commit() const { return true; }
  const ObString &get_hint() const { return hint_; }
  void set_hint(const ObString hint) { hint_ = hint; }
  TO_STRING_KV(N_STMT_TYPE, ((int)stmt_type_),
      K_(read_only),
      K_(with_consistent_snapshot),
      K_(hint));
private:
  // types and constants
private:
  // disallow copy
  ObStartTransStmt(const ObStartTransStmt &other);
  ObStartTransStmt &operator=(const ObStartTransStmt &other);
  // function members
private:
  // data members
  bool with_consistent_snapshot_;
  bool read_only_;
  ObString hint_;
};

inline ObStartTransStmt::ObStartTransStmt()
    : ObTCLStmt(stmt::T_START_TRANS),
    with_consistent_snapshot_(false),
    read_only_(false), hint_()
{
}
inline ObStartTransStmt::~ObStartTransStmt()
{
}
inline void ObStartTransStmt::set_read_only(bool val)
{
  read_only_ = val;
}
inline bool ObStartTransStmt::get_read_only() const
{
  return read_only_;
}
inline void ObStartTransStmt::set_with_consistent_snapshot(bool val)
{
  with_consistent_snapshot_ = val;
}
inline bool ObStartTransStmt::get_with_consistent_snapshot() const
{
  return with_consistent_snapshot_;
}
inline void ObStartTransStmt::print(FILE *fp, int32_t level, int32_t index)
{
  print_indentation(fp, level);
  fprintf(fp, "<ObStartTransStmt id=%d>\n", index);
  print_indentation(fp, level + 1);
  fprintf(fp, "WithConsistentSnapshot := %c\n", with_consistent_snapshot_ ? 'Y' : 'N');
  print_indentation(fp, level);
  fprintf(fp, "</ObStartTransStmt>\n");
}
} // end namespace sql
} // end namespace oceanbase

#endif // _OB_START_TRANS_STMT_H
