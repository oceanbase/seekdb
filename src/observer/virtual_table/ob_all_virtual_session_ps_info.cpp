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

#include "observer/virtual_table/ob_all_virtual_session_ps_info.h"

#include "sql/resolver/ob_resolver_utils.h"

#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"

using namespace oceanbase;
using namespace sql;
using namespace observer;
using namespace common;

int ObAllVirtualSessionPsInfo::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  bool is_filled = false;
  if (is_iter_end_) {
    ret = OB_ITER_END;
  } else {
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(get_next_row_from_sessions(is_filled))) {
        if (ret != OB_ITER_END) {
          SERVER_LOG(WARN, "get_rows_from_sessions failed", K(ret));
        }
      }
    } else {
      // Database modules are not ready.
      ret = OB_ITER_END;
    }
  }
  if (ret == OB_ITER_END && !is_iter_end_) {
    is_iter_end_ = true;
  }
  return ret;
}

int ObAllVirtualSessionPsInfo::inner_open()
{
  int ret = OB_SUCCESS;



  session_ids_.reset();
  if (OB_SUCC(ret)) {
    ObSQLSessionMgr &session_mgr = OBSERVER.get_sql_session_mgr();
    if (OB_FAIL(session_mgr.for_each_session(all_sql_session_iterator_))) {
    } else {
      int64_t cnt = 0;
      session_mgr.get_session_count(cnt);
      SERVER_LOG(WARN, "all virtual ssinfo get_session_count", K(cnt));
    }
  }

  return ret;
}

int format_param_types(const ObIArray<obmysql::EMySQLFieldType> &param_types,
                       ObIAllocator *allocator, const char *&ptr,
                       uint64_t &len)
{
  int ret = OB_SUCCESS;
  ptr = nullptr;
  len = 0;
  ObStringBuffer str_buf(allocator);
  for (int64_t idx = 0; OB_SUCC(ret) && idx < param_types.count(); ++idx) {
    std::string str = std::to_string(param_types.at(idx));
    const char *charPtr = str.c_str();
    if (OB_FAIL(str_buf.append(charPtr))) {
    } else if (idx < param_types.count()-1 && OB_FAIL(str_buf.append(", "))) {
      SERVER_LOG(WARN, "failed to format param_types", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    ptr = str_buf.ptr();
    len = str_buf.length();
  }
  return ret;
}

int ObAllVirtualSessionPsInfo::fill_cells(ObPsStmtId ps_client_stmt_id,
                                          bool &is_filled)
{
  int ret = OB_SUCCESS;
  int64_t col_count = output_column_ids_.count();
  is_filled = false;
  fetcher_.reuse();
  ObObj *cells = cur_row_.cells_;
  if (OB_ISNULL(cells)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "cells pointer is NULL", K(ret));
  } else {
    if (OB_ISNULL(cur_session_info_)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "cur_session_info_ pointer is NULL", K(ret));
    } else if (OB_FAIL(cur_session_info_->visit_ps_session_info(ps_client_stmt_id,
                                                     fetcher_))) {
      if (ret == OB_EER_UNKNOWN_STMT_HANDLER) {
        ret = OB_SUCCESS;
      } else {
        SERVER_LOG(WARN, "cannot get ps_session_info", K(ret),
                  K(ps_client_stmt_id));
      }
    } else if (OB_FAIL(fetcher_.get_error_code())) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < col_count; ++i) {
        uint64_t col_id = output_column_ids_.at(i);
        switch (col_id) {
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::SESSION_ID: {
          cells[i].set_uint64(cur_session_info_->get_sid());
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::PS_CLIENT_STMT_ID: {
          cells[i].set_int(ps_client_stmt_id);
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::PS_INNER_STMT_ID: {
          cells[i].set_int(fetcher_.get_inner_stmt_id());
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::STMT_TYPE: {
          ObString stmt_type_str;
          ObString stmt_type_tmp =
              ObResolverUtils::get_stmt_type_string(fetcher_.get_stmt_type());
          if (OB_FAIL(
                  ob_write_string(*allocator_, stmt_type_tmp, stmt_type_str))) {
          } else {
            cells[i].set_varchar(stmt_type_str);
            cells[i].set_collation_type(ObCharset::get_default_collation(
                ObCharset::get_default_charset()));
          }
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::PARAM_COUNT: {
          cells[i].set_int(fetcher_.get_param_count());
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::PARAM_TYPES: {
          const char *types_str = nullptr;
          uint64_t len = 0;
          if (OB_FAIL(format_param_types(fetcher_.get_param_types(),
                                        allocator_, types_str, len))) {
          } else {
            cells[i].set_lob_value(ObLongTextType, types_str,
                                  static_cast<int32_t>(len));
            cells[i].set_collation_type(ObCharset::get_default_collation(
                ObCharset::get_default_charset()));
          }
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::REF_COUNT: {
          cells[i].set_int(fetcher_.get_ref_count());
          break;
        }
        case share::ALL_VIRTUAL_SESSION_PS_INFO_CDE::CHECKSUM: {
          cells[i].set_int(fetcher_.get_ps_stmt_checksum());
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          SERVER_LOG(WARN, "invalid column id", K(ret), K(i),
                    K(output_column_ids_), K(col_id));
          break;
        }
        }
      }
      if (OB_SUCC(ret)) {
        is_filled = true;
      }
    }
  }
  return ret;
}

int ObAllVirtualSessionPsInfo::get_next_row_from_sessions(
    bool &is_filled)
{
  int ret = OB_SUCCESS;
  is_filled = false;
  do {
    if (ps_client_stmt_ids_.count() == 0) {
      if (OB_FAIL(all_sql_session_iterator_.next(cur_session_info_))) {
        if (ret == OB_ITER_END) {
          // do nothing
        } else {
          SERVER_LOG(WARN, "get next session failed", K(ret));
        }
      } else {
        if (OB_NOT_NULL(cur_session_info_)) {
          if (OB_FAIL(cur_session_info_->for_each_ps_session_info(*this))) {
          }
        } else {
          SERVER_LOG(WARN, "cur_session_info_ is nullptr", K(ret));
        }
      }
    }
    if (OB_SUCC(ret)) {
      ObPsStmtId ps_client_stmt_id = 0;
      if (ps_client_stmt_ids_.count() == 0) {
      } else if (OB_FAIL(ps_client_stmt_ids_.pop_back(ps_client_stmt_id))) {
      } else if (OB_FAIL(fill_cells(ps_client_stmt_id,
                                    is_filled))) {
      }
    }
  } while (!is_filled && OB_SUCC(ret));
  if (ret != OB_SUCCESS && ret != OB_ITER_END) {
    SERVER_LOG(WARN, "generate rows failed", K(ret),
               K(output_column_ids_));
  }
  return ret;
}

void ObAllVirtualSessionPsInfo::reset()
{
  ObAllPlanCacheBase::reset();
  fetcher_.reset();
  session_ids_.reset();
  all_sql_session_iterator_.reset();
  cur_session_info_ = nullptr;
  
  ps_client_stmt_ids_.reset();
  is_iter_end_ = false;
}

int ObAllVirtualSessionPsInfo::operator()(
    common::hash::HashMapPair<uint64_t, ObPsSessionInfo *> &entry)
{
  ObPsStmtId ps_client_stmt_id = entry.first;
  return ps_client_stmt_ids_.push_back(ps_client_stmt_id);
}

bool ObAllVirtualSessionPsInfo::ObSessionInfoIterator::operator()(
    ObSQLSessionMgr::Key key, ObSQLSessionInfo *sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNDEFINED;
    SERVER_LOG(WARN, "sess_info is NULL", K(ret));
  } else {
    if (sess_info->is_shadow()) {
    } else {
      ObArray<SessionID> *session_id_list = &session_ids_;
      if (OB_ISNULL(session_id_list)) {
      } else if (OB_FAIL(session_id_list->push_back(sess_info->get_server_sid()))) {
      }
    }
  }
  return ret == OB_SUCCESS;
}

int ObAllVirtualSessionPsInfo::ObSessionInfoIterator::next(
    ObSQLSessionInfo *&sess_info)
{
  int ret = OB_SUCCESS;
  sess_info = nullptr;
  SessionID session_id = 0;
  if (OB_NOT_NULL(last_attach_session_info_)) {
    OBSERVER.get_sql_session_mgr().revert_session(
        last_attach_session_info_);
    last_attach_session_info_ = nullptr;
  }
  if (OB_SUCC(ret) && OB_ISNULL(cur_session_id_list_)) {
    cur_session_id_list_ = &session_ids_;
    if (OB_NOT_NULL(cur_session_id_list_)) {
      
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  if (OB_SUCC(ret)) {
    do {
      if (0 == cur_session_id_list_->count()) {
        cur_session_id_list_ = nullptr;
        ret = OB_ITER_END;
      } else {
        if (OB_FAIL(cur_session_id_list_->pop_back(session_id))) {
        } else {
          if (OB_FAIL(OBSERVER.get_sql_session_mgr().get_session(
                  session_id, sess_info))) {
            if (OB_ENTRY_NOT_EXIST == ret) {
              ret = OB_SUCCESS;
            }
          } else {
            last_attach_session_info_ = sess_info;
            break;
          }
        }
      }
    } while (OB_SUCC(ret));
  }
  return ret;
}

void ObAllVirtualSessionPsInfo::ObSessionInfoIterator::reset()
{
  if (OB_NOT_NULL(last_attach_session_info_)) {
    OBSERVER.get_sql_session_mgr().revert_session(
        last_attach_session_info_);
    last_attach_session_info_ = nullptr;
  }
  cur_session_id_list_ = nullptr;
  
}

int ObAllVirtualSessionPsInfo::ObPsSessionInfoFetcher::operator()(
    common::hash::HashMapPair<uint64_t, ObPsSessionInfo *> &entry)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(entry.second)) {
    ObPsSessionInfo *ps_session_info =
        static_cast<ObPsSessionInfo *>(entry.second);
    inner_stmt_id_ = ps_session_info->get_inner_stmt_id();
    stmt_type_ = ps_session_info->get_stmt_type();
    param_count_ = ps_session_info->get_param_count();
    ref_count_ = ps_session_info->get_ref_cnt();
    checksum_ = ps_session_info->get_ps_stmt_checksum();
    if (OB_FAIL(param_types_.assign(ps_session_info->get_param_types()))) {
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "ps session info pointer is NULL", K(ret));
  }
  error_code_ = ret;
  return ret;
}
