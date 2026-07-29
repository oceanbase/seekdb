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

#define USING_LOG_PREFIX SQL_RESV

#include "sql/resolver/cmd/ob_alter_system_resolver.h"
#include "sql/resolver/cmd/ob_alter_system_stmt.h"
#include "sql/resolver/ddl/ob_create_table_resolver.h"
#include "sql/resolver/ddl/ob_drop_table_stmt.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "observer/ob_server.h"

namespace oceanbase
{
using namespace common;
using namespace obcall;
using namespace share;
using namespace share::schema;
using namespace observer;
namespace sql
{
typedef ObAlterSystemResolverUtil Util;

int ObAlterSystemResolverUtil::sanity_check(const ParseNode *parse_tree, ObItemType item_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == parse_tree)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse tree should not be null");
  } else if (OB_UNLIKELY(item_type != parse_tree->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid type",
             "expect", get_type_name(item_type),
             "actual", get_type_name(parse_tree->type_));
  } else if (OB_UNLIKELY(parse_tree->num_child_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid num_child", "num_child", parse_tree->num_child_);
  } else if (OB_UNLIKELY(NULL == parse_tree->children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children should not be null");
  }

  return ret;
}

int ObAlterSystemResolverUtil::resolve_tablet_id(const ParseNode *opt_tablet_id, ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;

  if (NULL == opt_tablet_id) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("opt_tablet_id should not be null");
  } else if (OB_FAIL(sanity_check(opt_tablet_id, T_TABLET_ID))) {
    LOG_WARN("sanity check failed");
  } else {
    tablet_id = opt_tablet_id->children_[0]->value_;
    FLOG_INFO("resolve tablet_id", K(tablet_id));
  }
  return ret;
}


int ObAlterSystemResolverUtil::resolve_string(const ParseNode *node, ObString &string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node should not be null");
  } else if (OB_UNLIKELY(T_VARCHAR != node->type_ && T_CHAR != node->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node type is not T_VARCHAR/T_CHAR", "type", get_type_name(node->type_));
  } else if (OB_UNLIKELY(node->str_len_ <= 0)) {
    ret = OB_ERR_PARSER_SYNTAX;
    LOG_WARN("empty string");
  } else {
    string = ObString(node->str_len_, node->str_value_);
  }
  return ret;
}

int ObAlterSystemResolverUtil::resolve_relation_name(const ParseNode *node, ObString &string)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(NULL == node)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node should not be null");
  } else if (OB_UNLIKELY(T_IDENT != node->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("node type is not T_IDENT", "type", get_type_name(node->type_));
  } else if (OB_UNLIKELY(node->str_len_ <= 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("empty string");
  } else {
    string = ObString(node->str_len_, node->str_value_);
  }
  return ret;
}

int ObFreezeResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFreezeStmt *freeze_stmt = NULL;
  if (OB_UNLIKELY(NULL == parse_tree.children_)
      || OB_UNLIKELY(3 != parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong freeze parse tree", KP(parse_tree.children_),
             K(parse_tree.num_child_));
  } else if (OB_ISNULL(session_info_)) {
    ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info should not be null", K(ret));
  } else if (NULL == (freeze_stmt = create_stmt<ObFreezeStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObFreezeStmt failed");
  } else if (OB_ISNULL(parse_tree.children_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong freeze type", KP(parse_tree.children_[0]));
  } else if (T_INT != parse_tree.children_[0]->type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong freeze type", K(parse_tree.children_[0]->type_));
  } else {
    stmt_ = freeze_stmt;
    if (1 == parse_tree.children_[0]->value_) { // MAJOR FREEZE
      freeze_stmt->set_major_freeze(true);
      if (OB_NOT_NULL(parse_tree.children_[1])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("major freeze has an unexpected target", KR(ret));
      } else if (OB_FAIL(resolve_target_(freeze_stmt, parse_tree.children_[2]))) {
        LOG_WARN("resolve major freeze target failed", KR(ret));
      }
    } else if (2 == parse_tree.children_[0]->value_) {  // MINOR FREEZE
      freeze_stmt->set_major_freeze(false);
      if (OB_NOT_NULL(parse_tree.children_[1])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("minor freeze has an unexpected target", KR(ret));
      } else if (OB_FAIL(resolve_target_(freeze_stmt, parse_tree.children_[2]))) {
        LOG_WARN("resolve minor freeze target failed", KR(ret));
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown freeze type", K(parse_tree.children_[0]->value_));
    }
  }

  return ret;
}

int ObFreezeResolver::resolve_target_(ObFreezeStmt *freeze_stmt,
                                      const ParseNode *tablet_node)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(freeze_stmt)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("freeze statement is null", KR(ret));
  } else if (OB_NOT_NULL(tablet_node)
             && OB_FAIL(Util::resolve_tablet_id(tablet_node, freeze_stmt->get_tablet_id()))) {
      LOG_WARN("fail to resolve tablet id", KR(ret));
  }
  return ret;
}

int ObFlushCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFlushCacheStmt *stmt = NULL;
  ObSQLSessionInfo* sess = params_.session_info_;
  if (OB_ISNULL(sess)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "invalid session");
  } else if (OB_UNLIKELY(T_FLUSH_CACHE != parse_tree.type_ || parse_tree.num_child_ != 4)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid argument",
             "type", get_type_name(parse_tree.type_),
             "child_num", parse_tree.num_child_);
  } else if (NULL == parse_tree.children_[0]) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid parse tree", K(ret));
  } else if (NULL == (stmt = create_stmt<ObFlushCacheStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObFlushCacheStmt failed");
  } else {
    ObSchemaGetterGuard schema_guard;

    ParseNode *cache_type_node = parse_tree.children_[0];
    stmt->flush_cache_arg_.cache_type_ = static_cast<ObCacheType>(cache_type_node->value_);
    // second child: resolve namespace
    ParseNode *namespace_node = parse_tree.children_[1];
    // third child: resolve sql_id
    ParseNode *sql_id_node = parse_tree.children_[2];
    // fourth child: resolve database list
    ParseNode *db_node = parse_tree.children_[3];
    ObSEArray<common::ObString, 8> db_name_list;

    // namespace
    if (OB_FAIL(ret)) {
    } else if (NULL == namespace_node) {
      stmt->flush_cache_arg_.ns_type_ = ObLibCacheNameSpace::NS_INVALID;
    } else if (stmt->flush_cache_arg_.cache_type_ != CACHE_TYPE_LIB_CACHE) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("only support lib cache's cache evict by namespace", K(stmt->flush_cache_arg_.cache_type_), K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "only support lib cache's cache evict by namespace, other type");
    } else {
      if (OB_UNLIKELY(NULL == namespace_node->children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ParseNode *node = namespace_node->children_[0];
        if (OB_UNLIKELY(NULL == node)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("node should not be null");
        } else {
          if (node->str_len_ <= 0) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("empty namespace name");
          } else {
            ObString namespce_name(node->str_len_, node->str_value_);
            ObLibCacheNameSpace ns_type = ObLibCacheRegister::get_ns_type_by_name(namespce_name);
            if (ns_type <= ObLibCacheNameSpace::NS_INVALID || ns_type >= ObLibCacheNameSpace::NS_MAX) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid namespace type", K(ns_type));
            } else {
              stmt->flush_cache_arg_.ns_type_ = ns_type;
            }
          }
        }
      }
    }

    // sql_id
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (OB_ISNULL(sql_id_node)) {
      // do nothing
    // currently, only support plan cache's fine-grained cache evict
    } else if (stmt->flush_cache_arg_.cache_type_ != CACHE_TYPE_PLAN &&
               stmt->flush_cache_arg_.cache_type_ != CACHE_TYPE_PL_OBJ) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("only support plan cache's fine-grained cache evict", K(stmt->flush_cache_arg_.cache_type_), K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "only support plan cache's fine-grained cache evict, other type");
    } else if (OB_ISNULL(sql_id_node->children_)
               || OB_ISNULL(sql_id_node->children_[0])) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    } else if (T_SQL_ID == sql_id_node->type_) {
      if (sql_id_node->children_[0]->str_len_ > (OB_MAX_SQL_ID_LENGTH+1)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid argument", K(ret));
      } else {
        stmt->flush_cache_arg_.sql_id_.assign_ptr(
            sql_id_node->children_[0]->str_value_,
            static_cast<ObString::obstr_size_t>(sql_id_node->children_[0]->str_len_));
        stmt->flush_cache_arg_.is_fine_grained_ = true;
      }
    } else if (T_SCHEMA_ID == sql_id_node->type_) {
      stmt->flush_cache_arg_.schema_id_ = sql_id_node->children_[0]->value_;
      stmt->flush_cache_arg_.is_fine_grained_ = true;
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    }

    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "invalid argument", K(GCTX.schema_service_));
    } else if (OB_ISNULL(session_info_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info should not be null", K(ret));
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(
                schema_guard))) {
      SERVER_LOG(WARN, "get_schema_guard failed", K(ret));
    } else {
      // do nothing
    }

    // db names
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (!stmt->flush_cache_arg_.is_fine_grained_) {
      if (OB_ISNULL(db_node)) {
        // Evict the server runtime plan cache.
        // and not needs to specify db_name
      } else {
        ret = OB_NOT_SUPPORTED;
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "flushing cache in database level at coarse flushing");
      }
    } else if (NULL == db_node) { // db list is empty
      // empty db list means clear all db's in fine-grained cache evict
      // do nothing
    } else if (OB_ISNULL(db_node->children_)
               || OB_ISNULL(db_node->children_[0])
               || T_DATABASE_LIST != db_node->type_) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid argument", K(ret));
    } else {
      ObString db_names;
      ObString db_name;
      db_names.assign_ptr(db_node->children_[0]->str_value_,
                          static_cast<ObString::obstr_size_t>(db_node->children_[0]->str_len_));
      while (OB_SUCC(ret) && !db_names.empty()) {
        db_name = db_names.split_on(',').trim();
        if(db_name.empty() && NULL == db_names.find(',')) {
          db_name = db_names;
          db_names.reset();
        }
        if(!db_name.empty()) {
          if (OB_FAIL(db_name_list.push_back(db_name))) {
            SERVER_LOG(WARN, "failed to add database name", K(ret));
          }
        }
      } // for database name end
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < db_name_list.count(); ++i) {
      uint64_t db_id = OB_INVALID_ID;
      if (OB_FAIL(schema_guard.get_database_id(db_name_list.at(i), db_id))
          || OB_INVALID_ID == db_id) {
        ret = OB_ERR_BAD_DATABASE;
        SERVER_LOG(WARN, "database not exist", K(db_name_list.at(i)), K(ret));
      } else if (OB_FAIL(stmt->flush_cache_arg_.push_database(db_id))) {
        SERVER_LOG(WARN, "fail to push database id", K(db_name_list.at(i)), K(db_id), K(ret));
      }
    }
    LOG_INFO("resolve flush command finished!", K(ret),
                K(stmt->flush_cache_arg_.cache_type_),
                K(stmt->flush_cache_arg_.sql_id_), K(stmt->flush_cache_arg_.is_fine_grained_),
                K(stmt->flush_cache_arg_.db_ids_));
  }
  return ret;
}

int ObFlushKVCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_FLUSH_KVCACHE != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_FLUSH_KVCACHE", "type", get_type_name(parse_tree.type_));
  } else {
    ObFlushKVCacheStmt *stmt = create_stmt<ObFlushKVCacheStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObFlushKVCacheStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ParseNode *node = parse_tree.children_[0];
        if (NULL == node) {
          stmt->cache_name_.reset();
        } else {
          if (OB_UNLIKELY(NULL == node->children_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("children should not be null");
          } else {
            node = node->children_[0];
            if (OB_UNLIKELY(NULL == node)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("node should not be null");
            } else {
              if (node->str_len_ <= 0) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("empty cache name");
              } else {
                ObString cache_name(node->str_len_, node->str_value_);
                if (OB_FAIL(stmt->cache_name_.assign(cache_name))) {
                  LOG_WARN("assign cache name failed", K(cache_name), K(ret));
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObFlushIlogCacheResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFlushIlogCacheStmt *stmt = NULL;
  if (OB_UNLIKELY(T_FLUSH_ILOGCACHE != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type not match T_FLUSH_ILOGCACHE", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(stmt = create_stmt<ObFlushIlogCacheStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create ObFlushIlogCacheStmt error", K(ret));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of parse tree is null", K(ret));
  } else {
    ParseNode *opt_file_id_node = parse_tree.children_[0];
    ParseNode *file_id_val_node = NULL;
    if (OB_ISNULL(opt_file_id_node)) {
      stmt->file_id_ = 0;
      stmt_ = stmt;
    } else if (OB_ISNULL(opt_file_id_node->children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("opt_file_id_node.children is null", K(ret));
    } else if (OB_ISNULL(file_id_val_node = opt_file_id_node->children_[0])) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("file_id_val_node is null", K(ret));
    } else {
      const int64_t file_id_val = file_id_val_node->value_;
      if (file_id_val <= 0 || file_id_val >= INT32_MAX) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid file_id when flush ilogcache", K(ret), K(file_id_val));
      } else {
        stmt->file_id_ = static_cast<int32_t>(file_id_val);
        stmt_ = stmt;
        LOG_INFO("flush ilogcache resolve succ", K(file_id_val));
      }
    }
  }
  return ret;
}


int ObFlushDagWarningsResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObFlushDagWarningsStmt *stmt = NULL;
  if (OB_UNLIKELY(T_FLUSH_DAG_WARNINGS != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type not match T_FLUSH_DAG_WARNINGS", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(stmt = create_stmt<ObFlushDagWarningsStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("create ObFlushDagWarningsStmt error", K(ret));
  }
  return ret;
}


int ObAdminMergeResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_MERGE_CONTROL != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_MERGE_CONTROL", "type", get_type_name(parse_tree.type_));
  } else if (OB_UNLIKELY(1 != parse_tree.num_child_ || nullptr == parse_tree.children_
                         || nullptr == parse_tree.children_[0]
                         || T_INT != parse_tree.children_[0]->type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid merge control parse tree", KR(ret), K(parse_tree.num_child_));
  } else if (OB_ISNULL(stmt_ = create_stmt<ObAdminMergeStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObAdminMergeStmt failed", KR(ret));
  } else if (2 == parse_tree.children_[0]->value_) {
    static_cast<ObAdminMergeStmt *>(stmt_)->set_merge_type(ObAdminMergeStmt::MergeType::SUSPEND);
  } else if (3 == parse_tree.children_[0]->value_) {
    static_cast<ObAdminMergeStmt *>(stmt_)->set_merge_type(ObAdminMergeStmt::MergeType::RESUME);
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected merge control type", KR(ret), "value", parse_tree.children_[0]->value_);
  }
  return ret;
}




int ObRefreshMemStatResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_REFRESH_MEMORY_STAT != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_REFRESH_MEMORY_STAT", "type", get_type_name(parse_tree.type_));
  } else {
    ObRefreshMemStatStmt *stmt = create_stmt<ObRefreshMemStatStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObRefreshMemStatStmt failed");
    } else {
      stmt_ = stmt;
      if (OB_UNLIKELY(NULL == parse_tree.children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      }
    }
  }
  return ret;
}

int ObRefreshIOCalibrationResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  ObRefreshIOCalibraitonStmt *stmt = nullptr;
  ObRefreshIOCalibrationParam *param = nullptr;
  if (OB_UNLIKELY(T_REFRESH_IO_CALIBRATION != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_REFRESH_IO_CALIBRATION", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(stmt = create_stmt<ObRefreshIOCalibraitonStmt>())) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("create ObRefreshIOCalibraitonStmt failed");
  } else if (FALSE_IT(stmt_ = stmt)) {
  } else if (OB_UNLIKELY(NULL == parse_tree.children_ || 3 != parse_tree.num_child_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse tree children is invalid", K(ret), K(parse_tree.num_child_));
  } else {
    param = &stmt->get_param();
  }
  if (OB_SUCC(ret)) {
    // parse storage_name from child[0]
    const ParseNode *storage_name_node = parse_tree.children_[0];
    if (OB_ISNULL(storage_name_node) || storage_name_node->num_child_ <= 0) {
      // allow null, do nothing
    } else if (OB_FAIL(Util::resolve_string(storage_name_node->children_[0], param->storage_name_))) {
      LOG_WARN("resolve storage name failed", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    // parse calibration_list from child[1]
    const ParseNode *calibration_list_node = parse_tree.children_[1];
    if (OB_ISNULL(calibration_list_node)) {
      // null means refresh
      param->only_refresh_ = true;
    } else if (nullptr == calibration_list_node->children_ || calibration_list_node->num_child_ <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("calibration list node has no children", K(ret));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < calibration_list_node->num_child_; ++i) {
        common::ObIOBenchResult item;
        const ParseNode *calibration_info_node = calibration_list_node->children_[i];
        ObString calibration_string;
        if (OB_ISNULL(calibration_info_node)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("children of calibration_list should not be null", K(ret), KP(calibration_info_node), K(i));
        } else if (OB_FAIL(Util::resolve_string(calibration_info_node, calibration_string))) {
          LOG_WARN("resolve calibration info node failed", K(ret));
          if (0 == i && calibration_info_node->str_len_ <= 0) {
            // empty means reset, do nothing
            param->only_refresh_ = false;
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_FAIL(ObIOCalibration::parse_calibration_string(calibration_string, item))) {
          LOG_WARN("parse calibration info failed", K(ret), K(calibration_string), K(i));
        } else if (OB_FAIL(param->calibration_list_.push_back(item))) {
          LOG_WARN("push back calibration item failed", K(ret), K(i), K(item));
        }
      }
    }
  }
  return ret;
}

static int alter_system_set_reset_add_config_item(obcall::ObAdminSetConfigArg &rpc_arg,
                                                  ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(rpc_arg.items_.push_back(item))) {
    LOG_WARN("add config item failed", K(ret), K(item));
  }
  return ret;
}

/* for mysql mode */
int ObSetConfigResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_SYSTEM_SET_PARAMETER != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_SYSTEM_SET_PARAMETER", "type", get_type_name(parse_tree.type_));
  } else {
    if (OB_UNLIKELY(NULL == parse_tree.children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("children should not be null");
    } else {
      const ParseNode *list_node = parse_tree.children_[0];
      if (OB_UNLIKELY(NULL == list_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("list_node should not be null");
      } else {
        ObSetConfigStmt *stmt = create_stmt<ObSetConfigStmt>();
        if (OB_UNLIKELY(NULL == stmt)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("create stmt failed");
        } else {
          HEAP_VAR(ObCreateTableResolver, ddl_resolver, params_) {
            for (int64_t i = 0; OB_SUCC(ret) && i < list_node->num_child_; ++i) {
              if (OB_UNLIKELY(NULL == list_node->children_)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("children should not be null");
                break;
              }

              const ParseNode *action_node = list_node->children_[i];
              if (NULL == action_node) {
                continue;
              }

              // config name
              HEAP_VAR(ObAdminSetConfigItem, item) {
                if (OB_LIKELY(session_info_ != NULL)) {

                } else {
                  LOG_WARN("session is null");

                }

                if (OB_UNLIKELY(NULL == action_node->children_)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children should not be null");
                  break;
                } else if (OB_UNLIKELY(4 != action_node->num_child_)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("invalid system action child count", K(ret), K(action_node->num_child_));
                  break;
                } else if (OB_FAIL(ObResolverUtils::resolve_local_runtime_selector(action_node->children_[3]))) {
                  LOG_WARN("fail to resolve set-config runtime selector", K(ret));
                  break;
                }

                if (OB_UNLIKELY(NULL == action_node->children_[0])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children[0] should not be null");
                  break;
                }

                ObString name(action_node->children_[0]->str_len_,
                              action_node->children_[0]->str_value_);
                ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                if (OB_FAIL(item.name_.assign(name))) {
                  LOG_WARN("assign config name failed", K(name), K(ret));
                  break;
                }

                // config value
                ObObjParam val;
                ObDefaultValueRes resolve_res(val);
                if (OB_UNLIKELY(NULL == action_node->children_[1])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children[1] should not be null");
                  break;
                } else if (OB_FAIL(ddl_resolver.resolve_default_value(action_node->children_[1], resolve_res))) {
                  LOG_WARN("resolve config value failed", K(ret));
                  break;
                } else if (!resolve_res.is_literal_) {
                  ret = OB_ERR_ILLEGAL_TYPE;
                  LOG_WARN("resolve config value failed", K(ret), K(resolve_res.is_literal_));
                  break;
                }
                ObString str_val;
                ObCollationType cast_coll_type = CS_TYPE_INVALID;
                if (OB_LIKELY(session_info_ != NULL)) {
                  if (OB_SUCCESS != session_info_->get_collation_connection(cast_coll_type)) {
                    LOG_WARN("fail to get collation_connection");
                    cast_coll_type = ObCharset::get_default_collation(ObCharset::get_default_charset());
                  } else {}
                } else {
                  LOG_WARN("session is null");
                  cast_coll_type = ObCharset::get_system_collation();
                }
                ObArenaAllocator allocator(ObModIds::OB_SQL_COMPILE);
                ObCastCtx cast_ctx(&allocator,
                                   NULL,//to varchar. this field will not be used.
                                   0,//to varchar. this field will not be used.
                                   CM_NONE,
                                   cast_coll_type,
                                   NULL);
                EXPR_GET_VARCHAR_V2(val, str_val);
                if (OB_FAIL(ret)) {
                  LOG_WARN("get varchar value failed", K(ret), K(val));
                  break;
                } else if (OB_FAIL(item.value_.assign(str_val))) {
                  LOG_WARN("assign config value failed", K(ret), K(str_val));
                  break;
                } else if (NULL != action_node->children_[2]) {
                  ObString comment(action_node->children_[2]->str_len_,
                                   action_node->children_[2]->str_value_);
                  if (OB_FAIL(item.comment_.assign(comment))) {
                    LOG_WARN("assign comment failed", K(comment), K(ret));
                    break;
                  }
                }

                if (OB_SUCC(ret)
                    && 0 == STRCASECMP(item.name_.ptr(), DEFAULT_TABLE_ORGANIZATION)) {
                  bool valid = ObConfigDefaultTableOrganizationChecker::check(item);
                  if (!valid) {
                    ret = OB_OP_NOT_ALLOW;
                    LOG_WARN("can not set default_table_organization", "item", item, K(ret));
                  }
                }

                if (OB_SUCC(ret)) {
                  if (OB_FAIL(alter_system_set_reset_add_config_item(stmt->get_rpc_arg(), item))) {
                    LOG_WARN("constraint check failed", K(ret));
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObSetTPResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_SYSTEM_SETTP != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_SYSTEM", "type", get_type_name(parse_tree.type_));
  } else {
    if (OB_UNLIKELY(NULL == parse_tree.children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("children should not be null");
    } else {
      const ParseNode *list_node = parse_tree.children_[0];
      if (OB_UNLIKELY(NULL == list_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("list_node should not be null");
      } else {
        ObSetTPStmt *stmt = create_stmt<ObSetTPStmt>();
        if (OB_UNLIKELY(NULL == stmt)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("create stmt failed");
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < list_node->num_child_; ++i) {
            if (OB_UNLIKELY(NULL == list_node->children_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("children should not be null");
              break;
            }

            const ParseNode *action_node = list_node->children_[i];
            if (OB_ISNULL(action_node)
                || OB_ISNULL(action_node->children_)
                || OB_ISNULL(action_node->children_[0])) {
              continue;
            }

            const ParseNode *value = action_node->children_[0];
            switch (action_node->type_)
            {
            case T_TP_NO: {        // event no
              if (stmt->get_param().event_name_ != "") {
                ret = OB_NOT_SUPPORTED;
                SQL_RESV_LOG(WARN, "Setting tp_no and tp_name simultaneously is not supported.");
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "Setting tp_no and tp_name simultaneously is");
              } else {
                stmt->get_param().event_no_ = value->value_;
              }
              break;
            }
            case T_TP_NAME: {     // event name
              if (stmt->get_param().event_no_ != 0) {
                ret = OB_NOT_SUPPORTED;
                SQL_RESV_LOG(WARN, "Setting tp_no and tp_name simultaneously is not supported.");
                LOG_USER_ERROR(OB_NOT_SUPPORTED, "Setting tp_no and tp_name simultaneously is");
              } else {
                stmt->get_param().event_name_.assign_ptr(
                  value->str_value_, static_cast<ObString::obstr_size_t>(value->str_len_));
              }
              break;
            }
            case T_OCCUR: {        // occurrence
              if (value->value_ > 0) {
                stmt->get_param().occur_ = value->value_;
              } else {
                ret = OB_INVALID_ARGUMENT;
              }
            } break;
            case T_TRIGGER_MODE: {      // trigger frequency
              if (T_INT == value->type_) {
                if (value->value_ < 0) {
                  ret = OB_INVALID_ARGUMENT;
                  SQL_RESV_LOG(WARN, "invalid argument", K(value->value_));
                } else {
                  stmt->get_param().trigger_freq_ = value->value_;
                }
              }
            } break;
            case T_ERROR_CODE: {        // error code
              if (value->value_ > 0) {
                stmt->get_param().error_code_ = -value->value_;
              } else {
                stmt->get_param().error_code_ = value->value_;
              }
            } break;
            case T_TP_COND: {        // condition
              stmt->get_param().cond_ = value->value_;
            } break;
            default:
              break;
            }
          }
        }
        LOG_INFO("set tp", K(stmt->get_param()));
      }
    }
  }

  return ret;
}

int ObClearMergeErrorResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_CLEAR_MERGE_ERROR != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_CLEAR_MERGE_ERROR", "type", get_type_name(parse_tree.type_));
  } else {
    ObClearMergeErrorStmt *stmt = create_stmt<ObClearMergeErrorStmt>();
    if (NULL == stmt) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObClearMergeErrorStmt failed");
    } else {
      stmt_ = stmt;
    }
  }
  return ret;
}

int ObCancelTaskResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_CANCEL_TASK != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_CANCEL_TASK", "type", get_type_name(parse_tree.type_));
  } else if (OB_ISNULL(parse_tree.children_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree's children is null", K(ret));
  } else {
    ObCancelTaskStmt *cancel_task = create_stmt<ObCancelTaskStmt>();
    if (NULL == cancel_task) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("create ObCancelTaskStmt failed");
    } else {
      stmt_ = cancel_task;
      ParseNode *task_id = parse_tree.children_[0];
      ObString task_id_str;
      if (OB_ISNULL(task_id)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("task_id node is null", K(ret));
      } else if (OB_FAIL(Util::resolve_string(task_id, task_id_str))) {
        LOG_WARN("resolve string failed", K(ret));
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(cancel_task->set_task_id(task_id_str))) {
          LOG_WARN("failed to set cancel task id", K(ret), K(task_id_str));
        }
      }
    }
  }
  return ret;
}

// Resolve ALTER SYSTEM SET sys_var = val.
int ObAlterSystemSetResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  bool set_parameters = false;

  if (OB_UNLIKELY(T_ALTER_SYSTEM_SET != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree.type_ must be T_ALTER_SYSTEM_SET", K(ret), K(parse_tree.type_));
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session_info_ or allocator_ is NULL", K(ret), K(session_info_), K(allocator_));
  } else {
    /* first round: detect set variables or parameters */
    for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
      ParseNode *set_node = nullptr, *set_param_node = nullptr;
      if (OB_ISNULL(set_node = parse_tree.children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node should not be null", K(ret));
      } else if (T_ALTER_SYSTEM_SET_PARAMETER != set_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_SET_PARAMETER", K(ret),
                 K(set_node->type_));
      } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node is null", K(ret));
      } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
      } else {
        ParseNode *var = nullptr;
        if (OB_ISNULL(var = set_param_node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("var is NULL", K(ret));
        } else if (T_IDENT != var->type_) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED,
              "Variable name isn't identifier type");
        } else {
          ObString name(var->str_len_, var->str_value_);
          {
            if (true &&
                nullptr != GCONF.get_container().get(ObConfigStringKey(name))) {
                set_parameters = true;
                break;
            }
          }
        }
      }
    } // for
  }
  /* second round: gen stmt */
  if (OB_SUCC(ret)) {
    if (set_parameters) {
      FLOG_WARN("set parameters");
      ObSetConfigStmt *setconfig_stmt = create_stmt<ObSetConfigStmt>();
      if (OB_ISNULL(setconfig_stmt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("create set config stmt failed", KR(ret));
      } else {
        HEAP_VAR(ObCreateTableResolver, ddl_resolver, params_) {
          for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
            ParseNode *set_node = nullptr, *set_param_node = nullptr;
            if (OB_ISNULL(set_node = parse_tree.children_[i])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("set_node should not be null", K(ret));
            } else if (T_ALTER_SYSTEM_SET_PARAMETER != set_node->type_) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_SET_PARAMETER",
                       K(ret), K(set_node->type_));
            } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("set_node is null", K(ret));
            } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
            } else {
              ParseNode *name_node = nullptr, *value_node = nullptr;
              HEAP_VAR(ObAdminSetConfigItem, item) {

                /* name */
                if (OB_ISNULL(name_node = set_param_node->children_[0])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("var is NULL", K(ret));
                } else if (T_IDENT != name_node->type_) {
                  ret = OB_NOT_SUPPORTED;
                  LOG_USER_ERROR(OB_NOT_SUPPORTED,
                      "Variable name isn't identifier type");
                } else {
                  ObString name(name_node->str_len_, name_node->str_value_);
                  ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                  if (OB_FAIL(item.name_.assign(name))) {
                    LOG_WARN("assign config name failed", K(name), K(ret));
                  }
                }
                if (OB_FAIL(ret)) {
                  break;
                }
                /* value */
                if (OB_ISNULL(value_node = set_param_node->children_[1])) {
                  ret = OB_INVALID_ARGUMENT;
                  LOG_WARN("value node is NULL", K(ret));
                } else {
                  ObObjParam val;
                  ObDefaultValueRes resolve_res(val);
                  if (OB_FAIL(ddl_resolver.resolve_default_value(value_node, resolve_res))) {
                    LOG_WARN("resolve config value failed", K(ret));
                    break;
                  } else if (!resolve_res.is_literal_) {
                    ret = OB_ERR_ILLEGAL_TYPE;
                    LOG_WARN("resolve config value failed", K(ret), K(resolve_res.is_literal_));
                    break;
                  }
                  ObString str_val;
                  ObCollationType cast_coll_type = CS_TYPE_INVALID;
                  if (OB_SUCCESS !=
                      session_info_->get_collation_connection(cast_coll_type)) {
                    LOG_WARN("fail to get collation_connection");
                    cast_coll_type = ObCharset::get_default_collation(
                        ObCharset::get_default_charset());
                  }
                  ObArenaAllocator allocator(ObModIds::OB_SQL_COMPILE);
                  ObCastCtx cast_ctx(&allocator,
                                     NULL, //to varchar. this field will not be used.
                                     0, //to varchar. this field will not be used.
                                     CM_NONE,
                                     cast_coll_type,
                                     NULL);
                  EXPR_GET_VARCHAR_V2(val, str_val);
                  if (OB_FAIL(ret)) {
                    LOG_WARN("get varchar value failed", K(ret), K(val));
                    break;
                  } else if (OB_FAIL(item.value_.assign(str_val))) {
                    LOG_WARN("assign config value failed", K(ret), K(str_val));
                    break;
                  }
                }
                if (OB_SUCC(ret)) {
                  if (OB_FAIL(alter_system_set_reset_add_config_item(
                      setconfig_stmt->get_rpc_arg(), item))) {
                    LOG_WARN("add config item failed", K(ret));
                  } else if (OB_SUCC(ret) && (0 == STRCASECMP(item.name_.ptr(), DEFAULT_TABLE_ORGANIZATION))) {
                    LOG_WARN("can not set default_table_organization", "item", item);
                    LOG_USER_NOTE(OB_NOT_SUPPORTED, "'ALTER SYSTEM SET DEFAULT_TABLE_ORGANIZATION' syntax is");
                  }
                }
              }
            }
          } // for
        }
      }
    } else {
      ObVariableSetStmt *variable_set_stmt = create_stmt<ObVariableSetStmt>();
      if (OB_ISNULL(variable_set_stmt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("create variable set stmt failed", K(OB_ALLOCATE_MEMORY_FAILED));
      } else {
        ParseNode *set_node = nullptr, *set_param_node = NULL;
        ObVariableSetStmt::VariableSetNode var_node;
        for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
          if (OB_ISNULL(set_node = parse_tree.children_[i])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node should not be null", K(ret));
          } else if (T_ALTER_SYSTEM_SET_PARAMETER != set_node->type_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_SET_PARAMETER",
                     K(ret), K(set_node->type_));
          } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node is null", K(ret));
          } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
          } else {
            ParseNode *var = NULL;
            var_node.set_scope_ = ObSetVar::SET_SCOPE_GLOBAL;
            variable_set_stmt->set_has_global_variable(true);
            /* resolve var_name */
            if (OB_ISNULL(var = set_param_node->children_[0])) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("var is NULL", K(ret));
            } else {
              ObString var_name;
              if (T_IDENT != var->type_) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                    "Variable name isn't identifier type");
              } else {
                var_node.is_system_variable_ = true;
                var_name.assign_ptr(var->str_value_,
                    static_cast<int32_t>(var->str_len_));
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(ob_write_string(*allocator_, var_name,
                                            var_node.variable_name_))) {
                  LOG_WARN("Can not malloc space for variable name", K(ret));
                } else {
                  ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI,
                                    var_node.variable_name_);
                }
              }
              /* resolve value */
              if (OB_SUCC(ret)) {
                if (OB_ISNULL(set_param_node->children_[1])) {
                  ret = OB_INVALID_ARGUMENT;
                  LOG_WARN("value node is NULL", K(ret));
                } else if (var_node.is_system_variable_) {
                  ParseNode value_node;
                  MEMCPY(&value_node, set_param_node->children_[1], sizeof(ParseNode));
                  if (OB_FAIL(ObResolverUtils::resolve_const_expr(params_, value_node, var_node.value_expr_, NULL))) {
                    LOG_WARN("resolve variable value failed", K(ret));
                  }
                }
              }
              if (OB_SUCC(ret) && OB_FAIL(variable_set_stmt->add_variable_node(var_node))) {
                LOG_WARN("Add set entry failed", K(ret));
              }
            }
          } // end resolve variable and value
        } // end for
      }
    }
  } // if

  return ret;
}


//for mysql mode
int ObResetConfigResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(T_ALTER_SYSTEM_RESET_PARAMETER != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not T_ALTER_SYSTEM_RESET_PARAMETER", "type", get_type_name(parse_tree.type_));
  } else {
    if (OB_UNLIKELY(NULL == parse_tree.children_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("children should not be null");
    } else {
      const ParseNode *list_node = parse_tree.children_[0];
      if (OB_UNLIKELY(NULL == list_node)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("list_node should not be null");
      } else if (OB_UNLIKELY(NULL == list_node->children_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("children should not be null");
      } else {
        ObResetConfigStmt *stmt = create_stmt<ObResetConfigStmt>();
        if (OB_UNLIKELY(NULL == stmt)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_ERROR("create stmt failed", K(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < list_node->num_child_; i++) {
            const ParseNode *action_node = list_node->children_[i];
            if (NULL == action_node) {
              continue;
            } else {
              HEAP_VAR(ObAdminSetConfigItem, item) {
                if (OB_LIKELY(NULL != session_info_)) {

                } else {
                  LOG_WARN("session is null");

                }
                if (OB_UNLIKELY(NULL == action_node->children_)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children should not be null");
                } else if (OB_UNLIKELY(NULL == action_node->children_[0])) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("children[0] should not be null");
                } else {
                  // config name
                  ObString name(action_node->children_[0]->str_len_,
                                action_node->children_[0]->str_value_);
                  ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                  if (OB_FAIL(item.name_.assign(name))) {
                    LOG_WARN("assign config name failed", K(name), K(ret));
                  } else {
                    ObConfigItem * const *config_item =
                        GCONF.get_container().get(ObConfigStringKey(item.name_.ptr()));
                    if (OB_ISNULL(config_item) || OB_ISNULL(*config_item)) {
                      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
                      LOG_WARN("unknown config", K(ret), K(item));
                    } else if (OB_FAIL(item.value_.assign((*config_item)->default_str()))) {
                      LOG_WARN("assign config value failed", K(ret));
                    } else if (OB_FAIL(alter_system_set_reset_add_config_item(
                                   stmt->get_rpc_arg(), item))) {
                      LOG_WARN("constraint check failed", K(ret));
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAlterSystemResetResolver::resolve(const ParseNode &parse_tree)
{
  int ret = OB_SUCCESS;
  bool set_parameters = false;

  if (OB_UNLIKELY(T_ALTER_SYSTEM_RESET != parse_tree.type_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("parse_tree.type_ must be T_ALTER_SYSTEM_RESET", K(ret), K(parse_tree.type_));
  } else if (OB_ISNULL(session_info_) || OB_ISNULL(allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session_info_ or allocator_ is NULL", K(ret), K(session_info_), K(allocator_));
  } else {
    /* first round: detect set variables or parameters */
    for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
      ParseNode *set_node = nullptr, *set_param_node = nullptr;
      if (OB_ISNULL(set_node = parse_tree.children_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node should not be null", K(ret));
      } else if (T_ALTER_SYSTEM_RESET_PARAMETER != set_node->type_) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_RESET_PARAMETER", K(ret),
                 K(set_node->type_));
      } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("set_node is null", K(ret));
      } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
      } else {
        ParseNode *var = nullptr;
        if (OB_ISNULL(var = set_param_node->children_[0])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("var is NULL", K(ret));
        } else if (T_IDENT != var->type_) {
          ret = OB_NOT_SUPPORTED;
          LOG_USER_ERROR(OB_NOT_SUPPORTED,
              "Variable name isn't identifier type");
        } else {
          ObString name(var->str_len_, var->str_value_);
          {
            if (true &&
              nullptr != GCONF.get_container().get(ObConfigStringKey(name))) {
              set_parameters = true;
              break;
            }
          }
        }
      }
    } // for
  }
  /* second round: gen stmt */
  if (OB_SUCC(ret)) {
    if (set_parameters) {
      ObSetConfigStmt *setconfig_stmt = create_stmt<ObSetConfigStmt>();
      if (OB_ISNULL(setconfig_stmt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("create set config stmt failed", KR(ret));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < parse_tree.num_child_; ++i) {
          ParseNode *set_node = nullptr, *set_param_node = nullptr;
          if (OB_ISNULL(set_node = parse_tree.children_[i])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node should not be null", K(ret));
          } else if (T_ALTER_SYSTEM_RESET_PARAMETER != set_node->type_) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node->type_ must be T_ALTER_SYSTEM_RESET_PARAMETER",
                      K(ret), K(set_node->type_));
          } else if (OB_ISNULL(set_param_node = set_node->children_[0])) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("set_node is null", K(ret));
          } else if (OB_UNLIKELY(T_VAR_VAL != set_param_node->type_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("type is not T_VAR_VAL", K(ret), K(set_param_node->type_));
          } else {
            ParseNode *name_node = nullptr, *value_node = nullptr;
            HEAP_VAR(ObAdminSetConfigItem, item) {

              /* name */
              if (OB_ISNULL(name_node = set_param_node->children_[0])) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("var is NULL", K(ret));
              } else if (T_IDENT != name_node->type_) {
                ret = OB_NOT_SUPPORTED;
                LOG_USER_ERROR(OB_NOT_SUPPORTED,
                    "Variable name isn't identifier type");
              } else {
                ObString name(name_node->str_len_, name_node->str_value_);
                ObCharset::casedn(CS_TYPE_UTF8MB4_GENERAL_CI, name);
                if (OB_FAIL(item.name_.assign(name))) {
                  LOG_WARN("assign config name failed", K(name), K(ret));
                }
              }
              if (OB_FAIL(ret)) {
                continue;
              }
              //value
              ObConfigItem * const *config_item =
                  GCONF.get_container().get(ObConfigStringKey(item.name_.ptr()));
              if (OB_ISNULL(config_item) || OB_ISNULL(*config_item)) {
                ret = OB_ERR_SYS_CONFIG_UNKNOWN;
                LOG_WARN("unknown config", KR(ret), K(item));
              } else if (OB_FAIL(item.value_.assign((*config_item)->default_str()))) {
                LOG_WARN("assign config value failed", K(ret));
              }
              if (OB_SUCC(ret)) {
                if (OB_FAIL(alter_system_set_reset_add_config_item(
                    setconfig_stmt->get_rpc_arg(), item))) {
                  LOG_WARN("add config item failed", KR(ret));
                }
              }
            }
          }
        } // for
      }
    } else {
      ret = OB_ERR_SYS_CONFIG_UNKNOWN;
      LOG_WARN("variables do not support reset or unknown config item", KR(ret));
    }
  } // if
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
