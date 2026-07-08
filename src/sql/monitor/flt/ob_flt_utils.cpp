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

#include "ob_flt_utils.h"
#include "sql/monitor/flt/ob_flt_control_info_mgr.h"

namespace oceanbase
{
namespace sql
{
  int ObFLTUtils::record_flt_last_trace_id(sql::ObSQLSessionInfo &session) {
    int ret = OB_SUCCESS;
    trace::UUID trc_uuid = OBTRACE->get_trace_id();
    trace::UUID span_id;
    char last_trace_id[OB_MAX_UUID_STR_LENGTH + 1];
    char last_span_id_buf[OB_MAX_UUID_STR_LENGTH + 1];
    ObString last_span_id;
    session.get_flt_span_id(last_span_id);
    int64_t pos = 0;
    span_id.deserialize(last_span_id.ptr(), last_span_id.length(), pos);
    pos = 0;
    trc_uuid.tostring(last_trace_id, OB_MAX_UUID_STR_LENGTH + 1, pos);
    pos = 0;
    span_id.tostring(last_span_id_buf, OB_MAX_UUID_STR_LENGTH + 1, pos);
    if (last_trace_id[0] == '\0') {
      // do nothing
    } else if (OB_FAIL(session.set_last_flt_trace_id(ObString(OB_MAX_UUID_STR_LENGTH + 1, last_trace_id)))) {
      LOG_WARN("failed  to set last flt trace id", K(ret), K(ObString(OB_MAX_UUID_STR_LENGTH + 1, last_trace_id)));
    } else if (OB_FAIL(session.set_last_flt_span_id(ObString(OB_MAX_UUID_STR_LENGTH + 1, last_span_id_buf)))) {
      LOG_WARN("failed  to set last flt span id", K(ret), K(ObString(OB_MAX_UUID_STR_LENGTH + 1, last_span_id_buf)));
    } else {
      // do nothing
    }
    return ret;
  }

  void ObFLTUtils::clean_flt_env() {
    if (OBTRACE->is_query_trace()) {
      FLT_END_TRACE();
    } else {
      if (OBTRACE->is_in_transaction()) {
        // do nothing
      } else {
        FLT_END_TRACE();
      }
    }
  }

  int ObFLTUtils::clean_flt_show_trace_env(sql::ObSQLSessionInfo &session) {
    int ret = OB_SUCCESS;
    if (OBTRACE->is_query_trace()) {
      // record current trace id
      if (OB_FAIL(record_flt_last_trace_id(session))) {
        LOG_WARN("failed to record last flt trace id", K(ret));
      } else {
        // do nothing
      }
      // transaction level show trace
    } else {
      if (OBTRACE->is_in_transaction()) {
        // do nothing
      } else {
        // record current trace id
        if (OB_FAIL(record_flt_last_trace_id(session))) {
          LOG_WARN("failed to record last flt trace id", K(ret));
        } else {
          // do nothing
        }
      }
    }
    return ret;
  }

  int ObFLTUtils::init_flt_info(sql::ObSQLSessionInfo &session,
                               bool enable_flt)
  {
    int ret = OB_SUCCESS;
    if (enable_flt) {
      OZ(init_flt_log_framework(session));
    } else {
      FLT_SET_TRACE_LEVEL(0);
      FLT_SET_AUTO_FLUSH(false);
      session.set_auto_flush_trace(false);
      session.set_trace_enable(false);
    }
    OBTRACE->set_enable_show_trace(session.is_use_trace_log());
    return ret;
  }

  int ObFLTUtils::init_flt_log_framework(sql::ObSQLSessionInfo &sess)
  {
    // initialize log framework - client FLT support removed
    // is_client_support_flt is always false; always use server-side self-generated trace
    int ret = OB_SUCCESS;
    FLT_RESET_SPAN();
    OBTRACE->set_in_transaction(sess.is_server_status_in_transaction());
    OBTRACE->set_is_query_trace(sess.is_query_trc_granuality());
    if (OBTRACE->is_query_trace()) {
      // reset log config
      sess.set_trace_enable(false);
      sess.set_auto_flush_trace(false);
      OZ(update_flush_policy_by_control_info(sess));
    } else {
      if (OBTRACE->is_in_transaction()) {
        // in the trans, do nothing
      } else {
        // reset log config
        sess.set_trace_enable(false);
        sess.set_auto_flush_trace(false);
        OZ(update_flush_policy_by_control_info(sess));
      }
    }

    // init log frame
    if (OB_FAIL(ret)) {
      // do nothing
    } else if (!sess.get_control_info().is_valid() || !sess.is_trace_enable()) {
      FLT_SET_TRACE_LEVEL(0);
      FLT_SET_AUTO_FLUSH(false);
      sess.set_auto_flush_trace(false);
      sess.set_trace_enable(false);
    } else {
      // update trace_id by server self
      // update trace_id in query granularity
      if (OBTRACE->is_query_trace()) {
        FLT_BEGIN_TRACE();
      } else {
        // update trace_id in transaction granularity
        if (!OBTRACE->is_in_transaction()) {
          // begin new trace
          FLT_BEGIN_TRACE();
        } else {
          // do nothing
        }
      }
      if (sess.is_use_trace_log()) {
        sess.set_auto_flush_trace(true);
      }
      FLT_SET_TRACE_LEVEL(sess.get_control_info().level_);
      FLT_SET_AUTO_FLUSH(sess.is_auto_flush_trace() || sess.is_use_trace_log());
    }
  
    LOG_TRACE("flt init log", K(sess.is_trace_enable()),
                  K(sess.is_auto_flush_trace()), K(sess.get_control_info()));
    return ret;
  }
  
  int ObFLTUtils::update_flush_policy_by_control_info(sql::ObSQLSessionInfo &sess)
  {
    int ret = OB_SUCCESS;
    FLTControlInfo con = sess.get_control_info();
    if (con.is_valid()) {
      // init trace enable
      con.print_sample_pct_ = ((double)(sess.get_tenant_print_sample_ppm()))/1000000;
      ObRandom r;
      double rand_num = 1.0 * (r.rand(0, RAND_MAX)/RAND_MAX);
      if (rand_num < con.sample_pct_) {
        sess.set_trace_enable(true);
      } else {
        sess.set_trace_enable(false);
      }
  
      // init flush trace
      if (con.rp_ == FLTControlInfo::RecordPolicy::RP_ALL) {
        if (sess.is_trace_enable()) {
          sess.set_auto_flush_trace(true);
        } else {
          sess.set_auto_flush_trace(false);
        }
      } else if (con.rp_ == FLTControlInfo::RecordPolicy::RP_ONLY_SLOW_QUERY) {
        // do nothing, slow query will must flush
      } else if (con.rp_ == FLTControlInfo::RecordPolicy::RP_SAMPLE_AND_SLOW_QUERY) {
        double rand_num2 = 1.0 * (r.rand(0, RAND_MAX)/RAND_MAX);
        if (rand_num2 < con.print_sample_pct_) {
          sess.set_auto_flush_trace(true);
        } else {
          sess.set_auto_flush_trace(false);
        }
      }
    } else {
      // invalid control info, do nothing
    }
    return ret;
  }

  int ObFLTUtils::init_app_info(sql::ObSQLSessionInfo &sess, FLTAppInfo &app_info)
  {
    int ret = OB_SUCCESS;
    if (!app_info.trace_module_.empty() &&
                OB_FAIL(sess.get_app_info_encoder().
                  set_module_name(&sess, app_info.trace_module_))) {
      LOG_WARN("failed to set mod name", K(ret));
    } else if (!app_info.trace_action_.empty() &&
                OB_FAIL(sess.get_app_info_encoder().
                  set_action_name(&sess, app_info.trace_action_))) {
      LOG_WARN("failed to set action name", K(ret));
    } else if (!app_info.trace_client_identifier_.empty() &&
                OB_FAIL(sess.set_client_id(app_info.trace_client_identifier_))) {
      LOG_WARN("failed to set client id", K(ret));
    } else {
      // do nothing
    }
    return ret;
  }

  int ObFLTUtils::process_flt_span_rec(const char *buf, const int64_t len) {
    int ret = OB_SUCCESS;
    if (len == 0) {
      // do nothing
    } else {
      ObArenaAllocator allocator;
      ObIJsonBase *j_tree = NULL;
      if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, buf, len,
            ObJsonInType::JSON_TREE, ObJsonInType::JSON_TREE, j_tree))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("fail to parse j_text", K(ret), K(ObString(len, buf)));
      } else if (j_tree->json_type() != ObJsonNodeType::J_ARRAY) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid json type", K(ret), K(j_tree->json_type()));
      } else {
        ObIJsonBase *jobject_ptr = NULL;
        ObIJsonBase *jval_ptr = NULL;
        //ObString key = "str";
        for (int64_t i = 0; OB_SUCC(ret) && i < j_tree->element_count(); i++) {
          if (OB_FAIL(j_tree->get_array_element(i, jobject_ptr))) {
            LOG_WARN("failed to get array element", K(ret), K(i));
          } else if (OB_ISNULL(jobject_ptr)) {
            ret = OB_ERR_NULL_VALUE;
            LOG_WARN("fail to get array child dom", K(ret), K(i));
          } else if (jobject_ptr->json_type() != ObJsonNodeType::J_OBJECT) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid json type", K(ret), K(jobject_ptr->json_type()));
          } else if (OB_FAIL(resolve_flt_span_rec(jobject_ptr, allocator))) {
            LOG_WARN("fail to resolve array child dom", K(ret));
          } else {
            // do nothing
          }
        }
      }
    }
    return ret;
  }

  int ObFLTUtils::resolve_flt_span_rec(ObIJsonBase *j_tree, ObArenaAllocator& alloc)
  {
    int ret = OB_SUCCESS;
    ObFLTSpanData data;
    if (OB_ISNULL(j_tree) || j_tree->json_type() != ObJsonNodeType::J_OBJECT) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid json type", K(ret), K(j_tree->json_type()));
    } else {
      const char* keys[] = {"trace_id", "name", "id", "start_ts", "end_ts",
                            "parent_id", "is_follow", "tags", "logs"};
      
      if (OB_FAIL(set_json_str_val(keys[0], j_tree, data.trace_id_))) {
        LOG_WARN("failed to set str val", K(keys[0]), K(ret));
      } else if (OB_FAIL(set_json_str_val(keys[1], j_tree, data.span_name_))) {
        LOG_WARN("failed to set str val", K(keys[1]), K(ret));
      } else if (OB_FAIL(set_json_str_val(keys[2], j_tree, data.span_id_))) {
        LOG_WARN("failed to set str val", K(keys[2]), K(ret));
      } else if (OB_FAIL(set_json_num_val(keys[3], j_tree, data.start_ts_))) {
        LOG_WARN("failed to set int val", K(keys[3]), K(ret));
      } else if (OB_FAIL(set_json_num_val(keys[4], j_tree, data.end_ts_))) {
        LOG_WARN("failed to set int val", K(keys[4]), K(ret));
      } else if (OB_FAIL(set_json_str_val(keys[5], j_tree, data.parent_span_id_))) {
        LOG_WARN("failed to set str val", K(keys[5]), K(ret));
      } else if (OB_FAIL(set_json_bool_val(keys[6], j_tree, data.ref_type_))) {
        LOG_WARN("failed to set bool val", K(keys[6]), K(ret));
      } else if (OB_FAIL(set_json_obj_val(keys[7], j_tree, data.tags_, alloc))) {
        LOG_WARN("failed to set str val", K(keys[7]), K(ret));
      } else if (OB_FAIL(set_json_obj_val(keys[8], j_tree, data.logs_, alloc))) {
        LOG_WARN("failed to set str val", K(keys[8]), K(ret));
      } else {
        // do nothing
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(get_flt_span_manager()->record_span(data, true))) {
        if (OB_SIZE_OVERFLOW == ret || OB_ALLOCATE_MEMORY_FAILED == ret) {
          LOG_TRACE("cannot allocate mem for record", K(ret));
          ret = OB_SUCCESS;
        } else {
          if (REACH_TIME_INTERVAL(100 * 1000)) { // in case logging is too frequent
            LOG_WARN("failed to record request info in request manager", K(ret));
          }
        }
      }
    }
    return ret;
  }

  int ObFLTUtils::set_json_str_val(ObString key, ObIJsonBase *jobject_ptr, ObString& val) {
    int ret = OB_SUCCESS;
    ObIJsonBase *jstr_ptr = NULL;
    if (OB_FAIL(jobject_ptr->get_object_value(key, jstr_ptr))) {
      if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
        val.reset();
      } else {
        LOG_WARN("failed to get object element", K(ret), K(key));
      }
    } else if (OB_ISNULL(jstr_ptr)) {
      // skip
    } else if (jstr_ptr->json_type() != ObJsonNodeType::J_STRING) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid json type", K(ret), K(jstr_ptr->json_type()));
    } else {
      val.assign_ptr(const_cast<char *>(jstr_ptr->get_data()), jstr_ptr->get_data_length());
    }
    return ret;
  }

  int ObFLTUtils::set_json_obj_val(ObString key, ObIJsonBase *jobject_ptr,
                                    ObString& val, ObArenaAllocator& alloc) {
    int ret = OB_SUCCESS;
    ObIJsonBase *jo_ptr = NULL;
    ObJsonBuffer j_buf(&alloc);
    if (OB_FAIL(jobject_ptr->get_object_value(key, jo_ptr))) {
      if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
        val.reset();
      } else {
        LOG_WARN("failed to get object element", K(ret), K(key));
      }
    } else if (OB_ISNULL(jo_ptr)) {
      //ret = OB_ERR_NULL_VALUE;
      //LOG_WARN("fail to get array child dom", K(ret));
      // skip
    } else if (jo_ptr->json_type() != ObJsonNodeType::J_OBJECT &&
              jo_ptr->json_type() != ObJsonNodeType::J_ARRAY) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid json type", K(ret), K(jo_ptr->json_type()));
    } else if (OB_FAIL(jo_ptr->print(j_buf, true, 0, false, 0))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("fail to convert json to string", K(ret));
    } else {
      val.assign_ptr(j_buf.ptr(), j_buf.length());
    }
    return ret;
  }

  int ObFLTUtils::set_json_num_val(ObString key, ObIJsonBase *jobject_ptr, int64_t& val) {
    int ret = OB_SUCCESS;
    ObIJsonBase *jint_ptr = NULL;
    if (OB_FAIL(jobject_ptr->get_object_value(key, jint_ptr))) {
      if (ret == OB_SEARCH_NOT_FOUND) {
        ret = OB_SUCCESS;
        val = 0;
      } else {
        LOG_WARN("failed to get object element", K(ret), K(key));
      }
    } else if (OB_ISNULL(jint_ptr)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("fail to get array child dom", K(ret));
    } else if (jint_ptr->json_type() != ObJsonNodeType::J_UINT) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid json type", K(ret), K(jint_ptr->json_type()));
    } else {
      val = jint_ptr->get_int();
    }
    return ret;
  }
 
  int ObFLTUtils::set_json_bool_val(ObString key, ObIJsonBase *jobject_ptr, int64_t& val) {
    int ret = OB_SUCCESS;
    ObIJsonBase *jbool_ptr = NULL;
    if (jobject_ptr->get_object_value(key, jbool_ptr)) {
      LOG_WARN("failed to get object element", K(ret), K(key));
    } else if (OB_ISNULL(jbool_ptr)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("fail to get array child dom", K(ret));
    } else if (jbool_ptr->json_type() != ObJsonNodeType::J_BOOLEAN) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid json type", K(ret), K(jbool_ptr->json_type()));
    } else {
      val = jbool_ptr->get_boolean();
    }
    return ret;
  }

  int handle_span_record(ObFLTSpanMgr *flt_span_manager, char* tag_buf, int64_t tag_len, ::oceanbase::trace::ObSpanCtx* span)
  {
    int ret = OB_SUCCESS;
  
    if (OB_ISNULL(flt_span_manager)) {
      // failed to get flt span manager, maybe tenant has been dropped, NOT NEED TO record;
    } else if (!OBTRACE->is_enable_show_trace()) {
      // do nothing
    } else {
      ObFLTSpanData data;
      ObArenaAllocator allocator;
      const int64_t len = MAX_TRACE_LOG_SIZE;
      char *buf = NULL;
      int64_t pos = 0;
      int64_t org_pos = 0;
  
      if (NULL == (buf = (char*)allocator.alloc(len))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          if (REACH_TIME_INTERVAL(100 * 1000)) {
            SERVER_LOG(WARN, "record concurrent fifoallocator alloc mem failed", K(len), K(ret));
          }
      } else {
        
        // trace id
        org_pos = pos;
        if (OB_FAIL(OBTRACE->get_trace_id().tostring(buf, len, pos))) {
          LOG_WARN ("failed to deserialize uuid", K(ret), K(buf), K(pos));
        } else {
          data.trace_id_.assign(buf+org_pos, pos - org_pos);
        }
  
        // span id
        org_pos = pos;
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(span->span_id_.tostring(buf, len, pos))) {
          LOG_WARN ("failed to deserialize uuid", K(ret), K(buf), K(pos));
        } else {
          data.span_id_.assign(buf+org_pos, pos - org_pos);
        }
  
        // span name
        data.span_name_.assign(const_cast<char *>(trace::__span_type_mapper[span->span_type_]),
                                static_cast<int32_t>(strlen(trace::__span_type_mapper[span->span_type_])));
  
        //parent_span_id_
        org_pos = pos;
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(span->source_span_) &&
            OB_FAIL(OBTRACE->get_root_span_id().tostring(buf, len, pos))) {
          LOG_WARN ("failed to deserialize uuid", K(ret), K(buf), K(pos));
        } else if (!OB_ISNULL(span->source_span_) &&
            OB_FAIL(span->source_span_->span_id_.tostring(buf, len, pos))){
          LOG_WARN ("failed to deserialize uuid", K(ret), K(buf), K(pos));
        } else {
          data.parent_span_id_.assign(buf+org_pos, pos - org_pos);
        }
        //data.parent_span_id_ = buf+org_pos;
  
        if (OB_NOT_NULL(tag_buf) && tag_len != 0) {
          //skip "tags":[ and trim ]
          data.tags_.assign(tag_buf+sizeof("\"tags\":["), static_cast<int32_t>(tag_len-sizeof("\"tags\":[")-2));
          //data.tags_.assign(tag_buf, tag_len);
        }
  
        // start ts
        data.start_ts_ = span->start_ts_;
        // end_ts
        data.end_ts_ = span->end_ts_;
        // is_follow
        data.ref_type_ = span->is_follow_;
      }
  
      // record span
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_FAIL(flt_span_manager->record_span(data, false))) {
        if (OB_SIZE_OVERFLOW == ret || OB_ALLOCATE_MEMORY_FAILED == ret) {
          LOG_TRACE("cannot allocate mem for record", K(ret));
          ret = OB_SUCCESS;
        } else {
          if (REACH_TIME_INTERVAL(100 * 1000)) { // in case logging is too frequent
            LOG_WARN("failed to record request info in request manager", K(ret));
          }
        }
      }
    }
    return ret;
  }
} // end of namespace sql
} // end of namespace oceanbase
