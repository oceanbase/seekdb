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

#define USING_LOG_PREFIX RS
#include "ob_tablet_creator.h"
#include "storage/tx/ob_trans_service.h"
#include "observer/ob_inner_sql_connection.h"
#include "storage/tx/ob_tx_log.h"
#include "storage/tablet/ob_tablet_ddl_complete_mds_helper.h"

namespace oceanbase
{
using namespace share;
namespace rootserver
{


bool ObTabletCreatorArg::is_valid() const
{
  bool is_valid = table_schemas_.count() > 0
                  && table_schemas_.count() == tablet_ids_.count()
                  && data_format_version_ > 0
                  && need_create_empty_majors_.count() == table_schemas_.count()
                  && (create_commit_versions_.empty() || create_commit_versions_.count() == tablet_ids_.count());
  for (int64_t i = 0; i < tablet_ids_.count() && is_valid; i++) {
    is_valid = tablet_ids_.at(i).is_valid();
  }
  return is_valid;
}



int ObTabletCreatorArg::init(
    const ObIArray<common::ObTabletID> &tablet_ids,
    const ObTabletID data_tablet_id,
    const ObIArray<const share::schema::ObTableSchema*> &table_schemas,
    const bool is_create_bind_hidden_tablets,
    const uint64_t data_format_version,
    const ObIArray<bool> &need_create_empty_majors,
    const ObIArray<int64_t> &create_commit_versions,
    const ObIArray<share::ObForkTabletInfo> &fork_tablet_infos)
{
  int ret = OB_SUCCESS;
  bool is_valid = table_schemas.count() > 0
                  && table_schemas.count() == tablet_ids.count()
                  && data_format_version > 0
                  && need_create_empty_majors.count() == table_schemas.count()
                  && (fork_tablet_infos.count() == 0 || fork_tablet_infos.count() == tablet_ids.count());
  for (int64_t i = 0; i < tablet_ids.count() && is_valid; i++) {
    is_valid = tablet_ids.at(i).is_valid();
  }
  if (OB_UNLIKELY(!is_valid)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_ids),
             "count", table_schemas.count(), K(tablet_ids),
             K(data_format_version), "count_to_create_empty_major", need_create_empty_majors.count(),
             "fork_tablet_infos_count", fork_tablet_infos.count());
  } else if (OB_FAIL(tablet_ids_.assign(tablet_ids))) {
    LOG_WARN("failed to assign table schemas", KR(ret), K(tablet_ids));
  } else if (OB_FAIL(table_schemas_.assign(table_schemas))) {
    LOG_WARN("failed to assign table schemas", KR(ret), K(table_schemas));
  } else if (OB_FAIL(need_create_empty_majors_.assign(need_create_empty_majors))) {
    LOG_WARN("failed to assign need create empty majors", K(ret), K(need_create_empty_majors));
  } else if (OB_FAIL(create_commit_versions_.assign(create_commit_versions))) {
    LOG_WARN("failed to assign create commit versions", KR(ret), K(create_commit_versions));
  } else if (OB_FAIL(fork_tablet_infos_.assign(fork_tablet_infos))) {
    LOG_WARN("failed to assign fork tablet infos", KR(ret), K(fork_tablet_infos));
  } else {
    data_tablet_id_ = data_tablet_id;
    is_create_bind_hidden_tablets_ = is_create_bind_hidden_tablets;
    data_format_version_ = data_format_version;
  }
  return ret;
}

int ObTabletCreatorArg::init(
    const ObIArray<common::ObTabletID> &tablet_ids,
    const ObTabletID data_tablet_id,
    const ObIArray<const share::schema::ObTableSchema*> &table_schemas,
    const bool is_create_bind_hidden_tablets,
    const uint64_t data_format_version,
    const ObIArray<bool> &need_create_empty_majors,
    const ObIArray<int64_t> &create_commit_versions)
{
  ObArray<share::ObForkTabletInfo> empty_fork_tablet_infos;
  return init(tablet_ids, data_tablet_id, table_schemas,
              is_create_bind_hidden_tablets, data_format_version,
              need_create_empty_majors, create_commit_versions,
              empty_fork_tablet_infos);
}

DEF_TO_STRING(ObTabletCreatorArg)
{
  int64_t pos = 0;
  J_KV(K_(tablet_ids), K_(data_tablet_id), K_(table_schemas), K_(is_create_bind_hidden_tablets),
    K_(data_format_version), K_(need_create_empty_majors), K_(create_commit_versions), K_(fork_tablet_infos));
  return pos;
}

/////////////////////////////////////////////////////////

int ObBatchCreateTabletHelper::init(
  const SCN &major_frozen_scn,
  const bool need_check_tablet_cnt)
{
  int ret = OB_SUCCESS;
  const int64_t bucket_count = hash::cal_next_prime(100);
  if (OB_FAIL(batch_arg_.init_create_tablet(major_frozen_scn, need_check_tablet_cnt))) {
    LOG_WARN("failed to init create tablet", KR(ret), K(major_frozen_scn));
  } else if (OB_FAIL(table_schemas_map_.create(bucket_count, "CreateTablet", "CreateTablet"))) {
    LOG_WARN("failed to create hashmap", KR(ret));
  }
  return ret;
}

int ObBatchCreateTabletHelper::add_arg_to_batch_arg(
    const ObTabletCreatorArg &tablet_arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!tablet_arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", KR(ret), K(tablet_arg));
  } else {
    ObArray<int64_t> index_array;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_arg.table_schemas_.count(); ++i) {
      const share::schema::ObTableSchema *table_schema = tablet_arg.table_schemas_.at(i);
      const uint64_t data_format_version = tablet_arg.data_format_version_;
      const bool need_create_empty_major = tablet_arg.need_create_empty_majors_.at(i);
      int64_t index = OB_INVALID_INDEX;
      if (OB_ISNULL(table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("table schema is null", KR(ret), K(i), K(tablet_arg));
      } else if (OB_FAIL(try_add_table_schema(table_schema, data_format_version,
          need_create_empty_major, index))) {
        LOG_WARN("failed to add table schema to batch", KR(ret), K(table_schema), K(need_create_empty_major), K(index), K(batch_arg_));
      } else if (OB_UNLIKELY(OB_INVALID_INDEX == index)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("index can not be invalid", KR(ret), K(index), K(tablet_arg), K(batch_arg_));
      } else if (OB_FAIL(index_array.push_back(index))) {
        LOG_WARN("failed to push back index", KR(ret), K(index));
      }
    }
    if (OB_SUCC(ret)) {
      obcall::ObCreateTabletInfo info;
      if (OB_FAIL(info.init(tablet_arg.tablet_ids_,
                            tablet_arg.data_tablet_id_,
                            index_array,
                            tablet_arg.is_create_bind_hidden_tablets_,
                            tablet_arg.create_commit_versions_,
                            tablet_arg.fork_tablet_infos_))) {
        LOG_WARN("failed to init create tablet info", KR(ret), K(index_array), K(tablet_arg));
      } else if (OB_FAIL(batch_arg_.tablets_.push_back(info))) {
        LOG_WARN("failed to push back info", KR(ret), K(info));
      }
    }
  }
  return ret;
}

int ObBatchCreateTabletHelper::add_table_schema_(
    const share::schema::ObTableSchema &const_table_schema,
    const uint64_t data_format_version,
    const bool need_create_empty_major,
    int64_t &index)
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ObTableSchema, table_schema) {
  if (OB_FAIL(table_schema.assign(const_table_schema))) {
    LOG_WARN("failed to assign table_schema", KR(ret), K(const_table_schema));
  }

  if (OB_FAIL(ret)) {
  } else {
    index = batch_arg_.create_tablet_schemas_.count();
    ObCreateTabletSchema *create_tablet_schema = NULL;
    void *create_tablet_schema_ptr = batch_arg_.allocator_.alloc(sizeof(ObCreateTabletSchema));
    obcall::ObCreateTabletExtraInfo create_tablet_extr_info;
    if (OB_ISNULL(create_tablet_schema_ptr)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate storage schema", KR(ret), K(table_schema));
    } else if (FALSE_IT(create_tablet_schema = new (create_tablet_schema_ptr)ObCreateTabletSchema())) {
    } else if (OB_FAIL(create_tablet_schema->init(batch_arg_.allocator_, table_schema,
                                                  false /*skip_column_info*/,
                                                  data_format_version))) {
      LOG_WARN("failed to init storage schema", KR(ret), K(table_schema));
    } else if (OB_FAIL(batch_arg_.create_tablet_schemas_.push_back(create_tablet_schema))) {
      LOG_WARN("failed to push back table schema", KR(ret), K(table_schema));
    } else if (OB_FAIL(create_tablet_extr_info.init(data_format_version,
                                                    need_create_empty_major,
                                                    table_schema.get_micro_index_clustered()))) {
      LOG_WARN("init create table extra info failed", K(ret), K(data_format_version), K(need_create_empty_major), K(table_schema));
    } else if (OB_FAIL(batch_arg_.tablet_extra_infos_.push_back(create_tablet_extr_info))) {
      LOG_WARN("failed to push back tablet extra infos", K(ret), K(create_tablet_extr_info));
    }
  }
  }
  return ret;
}

int ObBatchCreateTabletHelper::try_add_table_schema(
    const share::schema::ObTableSchema *table_schema,
    const uint64_t data_format_version,
    const bool need_create_empty_major,
    int64_t &index)
{
  int ret = OB_SUCCESS;
  index = OB_INVALID_INDEX;
  if (OB_ISNULL(table_schema)
             || OB_UNLIKELY(!table_schema->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("table schema is invlaid", KR(ret), KPC(table_schema));
  } else if (OB_SUCC(table_schemas_map_.get_refactored(table_schema->get_table_id(), index))) {
    //nothing
  } else if(OB_HASH_NOT_EXIST == ret)  {
    ret = OB_SUCCESS;
    if (OB_FAIL(add_table_schema_(*table_schema, data_format_version, need_create_empty_major, index))) {
      LOG_WARN("failed to push back table schema", KR(ret), KPC(table_schema));
    } else if (OB_FAIL(table_schemas_map_.set_refactored(table_schema->get_table_id(), index))) {
      LOG_WARN("failed to set table schema map", KR(ret), K(index), KPC(table_schema));
    }
  } else {
    LOG_WARN("failed to find table schema in map", KR(ret), KP(table_schema));
  }
  return ret;
}

DEF_TO_STRING(ObBatchCreateTabletHelper)
{
  int64_t pos = 0;
  J_KV(K_(batch_arg), K_(result));
  return pos;

}

/////////////////////////////////////////////////////////

ObTabletCreator::~ObTabletCreator()
{
  reset();
}

void ObTabletCreator::reset()
{
  ObBatchCreateTabletHelper *batch_arg = single_batch_arg_;
  while (OB_NOT_NULL(batch_arg)) {
    ObBatchCreateTabletHelper *tmp = batch_arg;
    batch_arg = batch_arg->next_;
    tmp->~ObBatchCreateTabletHelper();
  }
  single_batch_arg_ = NULL;
  need_check_tablet_cnt_ = false;
}

int ObTabletCreator::init(const bool need_check_tablet_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletCreator init twice", KR(ret));
  } else {
    need_check_tablet_cnt_ = need_check_tablet_cnt;
    inited_ = true;
  }
  return ret;
}

int ObTabletCreator::add_create_tablet_arg(const ObTabletCreatorArg &arg)
{
  int ret = OB_SUCCESS;
  ObBatchCreateTabletHelper *batch_arg = NULL;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletCreator not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", KR(ret), K(arg));
  } else if (OB_ISNULL(single_batch_arg_)) {
    void *arg_buf = allocator_.alloc(sizeof(ObBatchCreateTabletHelper));
    if (OB_ISNULL(arg_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate new arg", KR(ret), KP(batch_arg));
    } else if (FALSE_IT(batch_arg = new (arg_buf)ObBatchCreateTabletHelper())) {
    } else if (OB_FAIL(batch_arg->init(major_frozen_scn_, need_check_tablet_cnt_))) {
      LOG_WARN("failed to init batch arg helper", KR(ret), K(arg));
    } else {
      single_batch_arg_ = batch_arg;
      LOG_INFO("new single log stream tablet create batch", K(arg));
    }
  } else {
    batch_arg = single_batch_arg_;
  }

  if (OB_FAIL(ret)) {
  } else if (batch_arg->batch_arg_.get_serialize_size() > BATCH_ARG_SIZE) {
    LOG_INFO("batch arg is more than 1M", KR(ret), K(batch_arg->batch_arg_.tablets_.count()), K(batch_arg->batch_arg_));
    void *arg_buf = allocator_.alloc(sizeof(ObBatchCreateTabletHelper));
    ObBatchCreateTabletHelper *new_arg = NULL;
    if (OB_ISNULL(arg_buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate new arg", KR(ret));
    } else if (FALSE_IT(new_arg = new (arg_buf)ObBatchCreateTabletHelper())) {
    } else if (OB_FAIL(new_arg->init(major_frozen_scn_, need_check_tablet_cnt_))) {
      LOG_WARN("failed to init batch arg helper", KR(ret), K(arg));
    } else {
      new_arg->next_ = batch_arg;
      single_batch_arg_ = new_arg;
      batch_arg = new_arg;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(batch_arg->add_arg_to_batch_arg(arg))) {
    LOG_WARN("failed to add arg to batch", KR(ret), K(arg));
  }
  return ret;
}

int ObTabletCreator::execute()
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  const int64_t default_timeout_ts = GCONF.rpc_timeout;
  observer::ObInnerSQLConnection *conn = NULL;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletCreator not init", KR(ret));
  } else if (OB_ISNULL(conn = dynamic_cast<observer::ObInnerSQLConnection *>
                       (trans_.get_connection()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn_ is NULL", KR(ret));
  } else if (OB_ISNULL(single_batch_arg_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("batch arg count is invalid", KR(ret));
  } else {
    ObBatchCreateTabletHelper *batch_arg = single_batch_arg_;
    while (OB_SUCC(ret) && OB_NOT_NULL(batch_arg)) {
      int64_t buf_len = batch_arg->batch_arg_.get_serialize_size();
      int64_t pos = 0;
      char *buf = (char*)allocator_.alloc(buf_len);
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail alloc memory", KR(ret));
      } else if (OB_FAIL(batch_arg->batch_arg_.serialize(buf, buf_len, pos))) {
        LOG_WARN("fail to serialize", KR(ret), K(batch_arg->batch_arg_));
      } else if (OB_FAIL(share::ObShareUtil::set_default_timeout_ctx(ctx, default_timeout_ts))) {
        LOG_WARN("fail to set timeout ctx", KR(ret), K(default_timeout_ts));
      } else {
        int64_t start_time = ObTimeUtility::current_time();
        if (ctx.is_timeouted()) {
          ret = OB_TIMEOUT;
          LOG_WARN("already timeout", KR(ret), K(ctx));
        } else if (OB_FAIL(conn->register_multi_data_source(transaction::ObTxDataSourceType::CREATE_TABLET_NEW_MDS, buf, buf_len))) {
          LOG_WARN("fail to register_tx_data", KR(ret), K(batch_arg->batch_arg_), K(buf), K(buf_len));
        }
        int64_t end_time = ObTimeUtility::current_time();
        LOG_INFO("generate create arg", KR(ret), K(buf_len), K(batch_arg->batch_arg_.tablets_.count()),
                                        K(batch_arg->batch_arg_), "cost_ts", end_time - start_time);
        if (OB_SUCC(ret) && batch_arg->batch_arg_.set_binding_info_outside_create()) {
          const int64_t start_time = ObTimeUtility::current_time();
          if (OB_FAIL(ObTabletBindingMdsHelper::modify_tablet_binding_for_create(batch_arg->batch_arg_, ctx.get_abs_timeout(), trans_))) {
            LOG_WARN("failed to modify tablet binding for create", K(ret));
          }
          const int64_t end_time = ObTimeUtility::current_time();
          LOG_INFO("modify binding for create", KR(ret), K(buf_len), K(batch_arg->batch_arg_.tablets_.count()),
                                                "cost_ts", end_time - start_time);
        }
      }
      batch_arg = batch_arg->next_;
    }
  }
  reset();
  return ret;
}

} // rootserver
} // oceanbase
