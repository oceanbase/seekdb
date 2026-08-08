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

#define USING_LOG_PREFIX SERVER

#include "observer/mysql/obmp_init_db.h"
#include "sql/ob_query_retry_ctrl.h"

using namespace oceanbase::rpc;
using namespace oceanbase::obmysql;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::observer;
using namespace oceanbase::share::schema;

int ObMPInitDB::deserialize()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid packet", K(ret), K_(req));
  } else if (OB_UNLIKELY(req_->get_type() != ObRequest::OB_MYSQL)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid packet", K(ret), K_(req), K(req_->get_type()));
  } else {
    const ObMySQLRawPacket &pkt = reinterpret_cast<const ObMySQLRawPacket&>(req_->get_packet());
    if (OB_UNLIKELY(ObMySQLCommandLayout::BYTES != pkt.get_command_layout())) {
      ret = OB_INVALID_DATA;
      LOG_WARN("unexpected init-db command layout", K(ret),
               K(pkt.get_command_layout()));
    } else if (OB_FAIL(pkt.get_command_field(0, db_name_))) {
    }
  }
  return ret;
}

int ObMPInitDB::process()
{
  LOG_INFO("init db", K_(db_name));
  int ret = OB_SUCCESS;
  bool need_disconnect = true;
  ObSQLSessionInfo *session = NULL;
  ObString tmp_db_name;
  ObDataBuffer allocator(db_name_conv_buf, sizeof(db_name_conv_buf));
  int64_t query_timeout = 0;
  bool is_packet_retry = false;
  bool need_response_error = true; //temporary placeholder
  if (OB_FAIL(get_session(session))) {
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("null pointer");
  } else if (OB_FAIL(session->get_query_timeout(query_timeout))) {
  } else if (OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret));
  } else {
    ObCollationType old_db_coll_type = CS_TYPE_INVALID;
    ObCollationType collation_connection = CS_TYPE_INVALID;
    ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
    setup_wb(*session);
    tmp_db_name = session->get_database_name();
    session->update_last_active_time();
    
    int64_t global_version = OB_INVALID_VERSION;
    int64_t local_version = OB_INVALID_VERSION;
    ObQueryRetryType retry_type = RETRY_TYPE_NONE;
    int64_t retry_times = 0;
    THIS_WORKER.set_timeout_ts(get_receive_timestamp() + query_timeout);
    ObNameCaseMode mode = OB_NAME_CASE_INVALID;
    if (OB_UNLIKELY(session->is_zombie())) {
      ret = OB_ERR_SESSION_INTERRUPTED;
      LOG_WARN("session has been killed", K(ret), KPC(session));
    } else if (OB_FAIL(gctx_.schema_service_->get_published_schema_version(global_version))) {
    } else if (OB_FAIL(gctx_.schema_service_->get_runtime_refreshed_schema_version(local_version))) {
    } else if (OB_FAIL(session->get_collation_database(old_db_coll_type))) {
    } else if (OB_FAIL(session->get_collation_connection(collation_connection))) {
    } else if (OB_FAIL(session->get_name_case_mode(mode))) {
    } else {
      need_disconnect = false;
      bool perserve_lettercase = (mode != OB_LOWERCASE_AND_INSENSITIVE);
      if (OB_FAIL(ObSQLUtils::convert_sql_text_to_schema_for_storing(allocator,
                                                                     session->get_dtc_params(),
                                                                     db_name_))) {
      } else if (OB_FAIL(ObSQLUtils::check_and_convert_db_name(
                  collation_connection, perserve_lettercase, db_name_))) {
      } else {
        bool force_local_retry = false;
        do {
          retry_type = RETRY_TYPE_NONE;
          ret = do_process(session);
          if (is_schema_error(ret)) {
            if (local_version < global_version) {
              if (!THIS_WORKER.is_timeout()) {
                if (force_local_retry
                    || retry_times < ObQueryRetryCtrl::MAX_SCHEMA_ERROR_LOCAL_RETRY_TIMES) {
                  retry_type = RETRY_TYPE_LOCAL;
                } else {
                  retry_type = RETRY_TYPE_PACKET;
                }
                retry_times++;
                if (RETRY_TYPE_LOCAL == retry_type) {
                  ob_usleep(ObQueryRetryCtrl::WAIT_LOCAL_SCHEMA_REFRESHED_US
                         * ObQueryRetryCtrl::linear_timeout_factor(retry_times));
                }
                int tmp_ret = gctx_.schema_service_->get_runtime_refreshed_schema_version(local_version);
                if (OB_SUCCESS != tmp_ret) {
                }
              }
              LOG_WARN("schema err, need retry", K(ret),
                       K(retry_type), K(retry_times), K(force_local_retry),
                       LITERAL_K(ObQueryRetryCtrl::MAX_SCHEMA_ERROR_LOCAL_RETRY_TIMES));
            }
          }
          force_local_retry = false;
          if (OB_UNLIKELY(session->is_zombie())) {
            ret = OB_ERR_SESSION_INTERRUPTED;
            LOG_WARN("session has been killed", K(ret),
                     K(session->get_server_sid()));
          } else if (RETRY_TYPE_LOCAL == retry_type) {
            // Retry in this thread
            force_local_retry = true;
          } else if (RETRY_TYPE_PACKET == retry_type) {
            // Put back into the queue for retry
            if (!THIS_WORKER.can_retry()) {
              // Do not requeue, retry in this thread
              // FIXME: when will we be here?
              force_local_retry = true;
              LOG_WARN("fail to set retry flag, force to do local retry");
            } else {
              THIS_WORKER.set_need_retry();
              is_packet_retry = true;
            }
          }
          if (force_local_retry) {
            clear_wb_content(*session);
          }
        } while (force_local_retry);
      }
    }
    if (OB_FAIL(ret)) {
      int set_db_ret = OB_SUCCESS;
      if (OB_SUCCESS != (set_db_ret = session->set_default_database(tmp_db_name, old_db_coll_type))) {
      }
    }

    session->set_show_warnings_buf(ret);
    session->reset_warnings_buf();
    ob_setup_tsi_warning_buffer(NULL);
  }  // end session guard

  if (OB_FAIL(ret)) {
    if (false == is_packet_retry && need_disconnect && is_conn_valid()) {
      force_disconnect();
      LOG_WARN("disconnect connection when process query", K(ret));
    } else  if (false == is_packet_retry && OB_FAIL(send_error_packet(ret, NULL))) { // override ret, no need to throw further
      LOG_WARN("failed to send error packet", K(ret));
    }
  } else if (OB_LIKELY(NULL != session)) {
    ObOKPParam ok_param; // use defualt value
    if (OB_FAIL(send_ok_packet(*session, ok_param))) {
    }
  }
  if (session != NULL) {
    if (OB_FAIL(revert_session(session))) {
    }
  }
  return ret;
}

int ObMPInitDB::do_process(sql::ObSQLSessionInfo *session)
{
  int ret = OB_SUCCESS;
  int sret = OB_SUCCESS;
  share::schema::ObSessionPrivInfo session_priv;
  ObSchemaGetterGuard schema_guard;

  if (OB_ISNULL(session) || OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session not init", K(ret), K(session), K(gctx_.schema_service_));
  } else if (OB_FAIL(gctx_.schema_service_->get_runtime_schema_guard(schema_guard))) {
  } else if (OB_FAIL(session->get_session_priv_info(session_priv))) {
  } else if (OB_FAIL(ObSQLUtils::cvt_db_name_to_org(schema_guard, session, db_name_, NULL /*allocator*/))) {
  } else if (OB_FAIL(schema_guard.check_db_access(session_priv, session->get_enable_role_array(), db_name_))) {
    LOG_WARN("fail to check db access.", K_(db_name), K(ret));
    if (OB_ERR_NO_DB_SELECTED == ret) {
      sret = OB_ERR_BAD_DATABASE; // Throw the error code to let the upper layer retry
    } else {
      sret = ret; // For safety, throw it as well
    }
  } else {
    uint64_t db_id = OB_INVALID_ID;
    session->set_db_priv_set(session_priv.db_priv_set_);
    if (OB_FAIL(session->set_default_database(db_name_))) {
    } else if (OB_FAIL(session->update_database_variables(&schema_guard))) {
    } else if (OB_FAIL(schema_guard.get_database_id(session->get_database_name(), db_id))) {
    } else {
      session->set_database_id(db_id);
    }
  }
  return (OB_SUCCESS != sret) ? sret : ret;
}
