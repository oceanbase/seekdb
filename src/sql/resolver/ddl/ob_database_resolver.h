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

#ifndef OCEANBASE_SQL_OB_DATABASE_RESOLVER_
#define OCEANBASE_SQL_OB_DATABASE_RESOLVER_

#include "lib/oblog/ob_log.h"
#include "lib/string/ob_sql_string.h"
#include "share/ob_rpc_struct.h"
#include "sql/resolver/ob_stmt.h"
#include "lib/charset/ob_charset.h"
#include "sql/session/ob_sql_session_info.h"
namespace oceanbase
{
namespace sql
{
template <class T>
class ObDatabaseResolver
{
public:
  ObDatabaseResolver() :
    alter_option_bitset_(),
    collation_already_set_(false)
  {
  }
  ~ObDatabaseResolver() {};
private:
  DISALLOW_COPY_AND_ASSIGN(ObDatabaseResolver);
public:
  int resolve_database_options(T *stmt, ParseNode *node, ObSQLSessionInfo *session_info);
  const common::ObBitSet<> &get_alter_option_bitset() const { return alter_option_bitset_; };
private:
  int resolve_database_option(T *stmt, ParseNode *node, ObSQLSessionInfo *session_info);
private:
  common::ObBitSet<> alter_option_bitset_;
  // A create/alter database statement may contain multiple charset/collate, used to mark whether it has appeared before flag
  bool collation_already_set_;
};

template <class T>
int ObDatabaseResolver<T>::resolve_database_options(T *stmt, ParseNode *node, ObSQLSessionInfo *session_info)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(stmt) || OB_ISNULL(node)) {
    ret = common::OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(stmt), K(node));
  } else if (OB_UNLIKELY(T_DATABASE_OPTION_LIST != node->type_)
             || OB_UNLIKELY(0 > node->num_child_)
             || OB_ISNULL(node->children_)) {
    ret = common::OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "invalid node info", K(node->type_), K(node->num_child_), K(node->children_));
  } else {
    ParseNode *option_node = NULL;
    int32_t num = node->num_child_;
    for (int32_t i = 0; ret == common::OB_SUCCESS && i < num; i++) {
      option_node = node->children_[i];
      if (OB_FAIL(resolve_database_option(stmt, option_node, session_info))) {
      }
    }
  }
  return ret;
}

template <class T>
int ObDatabaseResolver<T>::resolve_database_option(T *stmt, ParseNode *node, ObSQLSessionInfo *session_info)
{
  int ret = common::OB_SUCCESS;
  ParseNode *option_node = node;
  if (OB_ISNULL(stmt)) {
    ret = common::OB_INVALID_ARGUMENT;
    OB_LOG(WARN, "invalid argument", K(stmt), K(node));
  } else if (OB_ISNULL(option_node)) {
    //nothing to do
  } else {
    switch (option_node->type_) {
      case T_CHARSET:
      case T_COLLATION: {
        common::ObCharsetType charset_type = common::CHARSET_INVALID;
        common::ObCollationType collation_type = common::CS_TYPE_INVALID;
        if (T_CHARSET == option_node->type_) {
          common::ObString charset(option_node->str_len_, option_node->str_value_);
          charset_type = common::ObCharset::charset_type(charset.trim());
          collation_type = common::ObCharset::get_default_collation(charset_type);
          if (OB_UNLIKELY(common::CHARSET_INVALID == charset_type)) {
            ret = common::OB_ERR_UNKNOWN_CHARSET;
            LOG_USER_ERROR(OB_ERR_UNKNOWN_CHARSET, charset.length(), charset.ptr());
          } else if (OB_UNLIKELY(common::CS_TYPE_INVALID == collation_type)) {
            ret = common::OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "all valid charset types should have default collation type",
                            K(ret), K(charset_type), K(collation_type));
          } else if (OB_UNLIKELY(collation_already_set_
                              && stmt->get_charset_type() != charset_type)) {
            // mysql executes the following sql statement and will report an error, to be consistent with mysql behavior, check for collation/charset inconsistency issues during resolve
            // create database db charset utf8 charset utf16; 
            ret = OB_ERR_CONFLICTING_DECLARATIONS;
            SQL_RESV_LOG(WARN, "charsets mismatch", K(stmt->get_charset_type()), K(charset_type));
            const char *charset_name1 = ObCharset::charset_name(stmt->get_charset_type());
            const char *charset_name2 = ObCharset::charset_name(charset_type);
            LOG_USER_ERROR(OB_ERR_CONFLICTING_DECLARATIONS, charset_name1, charset_name2);
          }
        } else {
          common::ObString collation(option_node->str_len_, option_node->str_value_);
          collation_type = common::ObCharset::collation_type(collation.trim());
          charset_type = ObCharset::charset_type_by_coll(collation_type);
          if (OB_UNLIKELY(common::CS_TYPE_INVALID == collation_type)) {
            ret = common::OB_ERR_UNKNOWN_COLLATION;
            LOG_USER_ERROR(OB_ERR_UNKNOWN_COLLATION, collation.length(), collation.ptr());
          } else if (OB_UNLIKELY(common::CHARSET_INVALID == charset_type)) {
            ret = common::OB_ERR_UNEXPECTED;
            SQL_RESV_LOG(WARN, "all valid collation types should have corresponding charset type",
                            K(ret), K(charset_type), K(collation_type));
          } else if (OB_UNLIKELY(collation_already_set_
                              && stmt->get_charset_type() != charset_type)) {
            ret = OB_ERR_COLLATION_MISMATCH;
            SQL_RESV_LOG(WARN, "charset and collation mismatch",
                          K(stmt->get_charset_type()), K(charset_type));
          }
        }
        if (OB_SUCC(ret)) {
          stmt->set_charset_type(charset_type);
          stmt->set_collation_type(collation_type);
          collation_already_set_ = true;
          if (stmt::T_ALTER_DATABASE == stmt->get_stmt_type()) {
            if (OB_FAIL(alter_option_bitset_.add_member(
                    obcall::ObAlterDatabaseArg::COLLATION_TYPE))) {
            }
          }
        }
        break;
      }
      case T_READ_ONLY: {
        if (OB_ISNULL(option_node->children_[0])) {
          ret = common::OB_ERR_UNEXPECTED;
          OB_LOG(WARN, "invalid option node for read_only", K(option_node),
                 K(option_node->children_[0]));
        } else if (T_ON == option_node->children_[0]->type_) {
          stmt->set_read_only(true);
        } else if (T_OFF == option_node->children_[0]->type_) {
          stmt->set_read_only(false);
        } else {
          ret = common::OB_ERR_UNEXPECTED;
          OB_LOG(WARN, "unknown read only options", K(ret));
        }
        if (common::OB_SUCCESS == ret && stmt->get_stmt_type() == stmt::T_ALTER_DATABASE) {
          if (OB_FAIL(alter_option_bitset_.add_member(
                  obcall::ObAlterDatabaseArg::READ_ONLY))) {
          }
        }
        break;
      }
      default: {
        OB_LOG(WARN, "invalid type of parse node", K(option_node));
        break;
      }
    }
  }
  return ret;
}

}  // namespace sql
} //namespace oceanbase

#endif
