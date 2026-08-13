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
#include "ob_tablet_drop.h"
#include "common/mysqlclient/ob_isql_connection.h"
#include "share/ob_share_util.h"
#include "share/tablet/ob_tablet_mapping_operator.h"
#include "share/tablet/ob_tablet_to_table_history_operator.h" // ObTabletToTableHistoryOperator
#include "query/session/ob_inner_sql_connection_access.h"
#include "storage/tx/ob_multi_data_source.h"

namespace oceanbase
{
namespace rootserver
{

ObTabletDrop::~ObTabletDrop()
{
  if (OB_NOT_NULL(tablet_ids_)
      && FALSE_IT(tablet_ids_->~ObIArray<ObTabletID>())) {
  }
}


int ObTabletDrop::init()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTabletDrop init twice", KR(ret));
  } else {
    inited_ = true;
  }
  return ret;
}

int ObTabletDrop::add_drop_tablets_of_table_arg(
                  const common::ObIArray<const share::schema::ObTableSchema*> &schemas)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletCreator not init", KR(ret));
  } else if (schemas.count() < 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("schemas count is less 1", KR(ret), K(schemas));
  } else if (OB_ISNULL(schemas.at(0))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("NULL ptr", KR(ret), K(schemas));
  } else {
    const share::schema::ObTableSchema &table_schema = *schemas.at(0);
    if (is_inner_table(table_schema.get_table_id())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("sys table cannot drop", K(table_schema), KR(ret));
    } else if (schemas.count() > 1) {
      int64_t data_table_id = OB_INVALID_ID;
      if (table_schema.is_index_local_storage()
          || table_schema.is_aux_lob_table()) {
        data_table_id = table_schema.get_data_table_id();
      } else {
        data_table_id = table_schema.get_table_id();
      }
      for (int64_t i = 1; OB_SUCC(ret) && i < schemas.count(); ++i) {
        const share::schema::ObTableSchema *aux_table_schema = schemas.at(i);
        if (OB_ISNULL(aux_table_schema)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("ptr is null", KR(ret), K(schemas));
        } else if (is_inner_table(aux_table_schema->get_table_id())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("sys table cannot drop", KPC(aux_table_schema), KR(ret));
        } else if (!aux_table_schema->is_index_local_storage()
            && !aux_table_schema->is_aux_lob_table()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("aux_table_schema must be local index or aux lob table", KR(ret), K(schemas), KPC(aux_table_schema));
        } else if (data_table_id != aux_table_schema->get_data_table_id()) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("aux table schema must be of same data table", KR(ret), K(schemas), KPC(aux_table_schema));
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      ObPartitionLevel part_level = table_schema.get_part_level();
      if (PARTITION_LEVEL_ZERO == part_level) {
        if (OB_FAIL(drop_tablet_(schemas, OB_INVALID_INDEX, OB_INVALID_INDEX, false/*is_hidden*/))) {
        }
      } else {
        ObPartition **part_array = table_schema.get_part_array();
        int64_t part_num = table_schema.get_partition_num();
        ObPartition **hidden_part_array = table_schema.get_hidden_part_array();
        int64_t hidden_part_num = table_schema.get_hidden_partition_num();
        int64_t total_part_num = part_num + hidden_part_num; 
        if (OB_ISNULL(part_array)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("part array is null", K(table_schema), KR(ret));
        } else {
          for (int64_t i = 0; i < total_part_num && OB_SUCC(ret); ++i) {
            ObPartition *part = NULL;
            bool is_hidden = false;
            if (i < part_num) {
              part = part_array[i];
            } else if (OB_ISNULL(hidden_part_array) || i - part_num >= hidden_part_num) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("NULL ptr", K(i), K(table_schema), KR(ret)); 
            } else {
              is_hidden = true;
              part = hidden_part_array[i - part_num];
            }
            if (OB_FAIL(ret)) {
            } else if (OB_ISNULL(part)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("NULL ptr", K(i), K(table_schema), KR(ret));
            } else if (PARTITION_LEVEL_ONE == part_level) {
              if (is_hidden && OB_FAIL(drop_tablet_(schemas, i - part_num, OB_INVALID_INDEX, is_hidden))) {
                LOG_ERROR("fail to drop tablet", K(table_schema), KR(ret));
              } else if (!is_hidden && OB_FAIL(drop_tablet_(schemas, i, OB_INVALID_INDEX, is_hidden))) {
                LOG_ERROR("fail to drop tablet", K(table_schema), KR(ret));
              }
            } else if (PARTITION_LEVEL_TWO == part_level) {
              ObSubPartition **subpart_array = part->get_subpart_array();
              int64_t sub_part_num = part->get_subpartition_num();
              if (OB_ISNULL(subpart_array)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("part array is null", K(table_schema), KR(ret));
              } else {
                for (int64_t j = 0; j < sub_part_num && OB_SUCC(ret); j++) {
                  if (OB_ISNULL(subpart_array[j])) {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("NULL ptr", K(j), K(table_schema), KR(ret));
                  } else {
                    if (OB_FAIL(drop_tablet_(schemas, i, j, false/*is_hidden*/))) {
                    }
                  }
                }
              }
            } else {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("4.0 not support part type", K(table_schema), KR(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletDrop::drop_tablet_(
    const common::ObIArray<const share::schema::ObTableSchema *> &table_schema_ptr_array,
    const int64_t part_idx,
    const int64_t subpart_idx,
    const bool is_hidden)
{
  int ret = OB_SUCCESS;

  if (table_schema_ptr_array.count() < 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("table_schema_ptr_array is empty", K(table_schema_ptr_array), KR(ret));
  } else if (OB_ISNULL(tablet_ids_)) {
    void *ptr1 = allocator_.alloc(sizeof(ObArray<ObTabletID>));
    if (OB_ISNULL(ptr1)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail alloc memory", KR(ret));
    } else {
      tablet_ids_ = new (ptr1)ObArray<ObTabletID, ObIAllocator &>(
                                OB_MALLOC_NORMAL_BLOCK_SIZE, allocator_);
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(tablet_ids_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(table_schema_ptr_array), KR(ret));
  } else if (OB_ISNULL(table_schema_ptr_array.at(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(table_schema_ptr_array), KR(ret));
  } else {
    bool only_drop_index = table_schema_ptr_array.at(0)->is_index_local_storage();
    ObBasePartition *first_table_part = NULL;
    ObBasePartition *part = NULL;
    for (int r = 0; r < table_schema_ptr_array.count() && OB_SUCC(ret); r++) {
      const share::schema::ObTableSchema *table_schema_ptr = table_schema_ptr_array.at(r);
      if (OB_ISNULL(table_schema_ptr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(r), K(table_schema_ptr_array), KR(ret));
      } else if (is_hidden && 
               (PARTITION_LEVEL_ONE != table_schema_ptr->get_part_level()
             || table_schema_ptr->get_hidden_partition_num() <= part_idx
             || 0 > part_idx)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("hidden tablet only support partition-levle-one", K(table_schema_ptr), KR(ret), K(part_idx));
      } else if (PARTITION_LEVEL_ZERO == table_schema_ptr->get_part_level()) {
        ObTabletID tablet_id = table_schema_ptr->get_tablet_id();
        if (OB_FAIL(tablet_ids_->push_back(tablet_id))) {
        }
      } else if(is_hidden && OB_FALSE_IT(part = table_schema_ptr->get_hidden_part_array()[part_idx])) {
      } else if (!is_hidden && OB_FAIL(table_schema_ptr->get_part_by_idx(part_idx, subpart_idx, part))) {
        LOG_WARN("fail to get index part", KR(ret), KPC(table_schema_ptr), K(part_idx), K(subpart_idx));
      } else if (OB_ISNULL(part)) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("NULL ptr", KR(ret), KPC(table_schema_ptr), K(part_idx), K(subpart_idx));
      } else {
        if (r == 0) {
          first_table_part = part;
        } else {
          if (OB_UNLIKELY(!first_table_part->same_base_partition(*part))) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("parts in table and index table is not equal", KR(ret), KPC(first_table_part), KPC(part));
          }
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(tablet_ids_->push_back(part->get_tablet_id()))) {
        }
      }
    }
  }
  return ret;
}
int ObTabletDrop::execute()
{
  int ret = OB_SUCCESS;
  ObTimeoutCtx ctx;
  const int64_t default_timeout_ts = GCONF.rpc_timeout;
  const int64_t SLEEP_INTERVAL = 100 * 1000L; // 100ms
  common::sqlclient::ObISQLConnection *conn = NULL;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObTabletCreator not init", KR(ret));
  } else if (OB_ISNULL(conn = trans_.get_connection())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn_ is NULL", KR(ret));
  } else if (OB_ISNULL(tablet_ids_) || OB_UNLIKELY(tablet_ids_->count() < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("batch arg count is invalid", KR(ret));
  } else {
    obcall::ObBatchRemoveTabletArg arg;
    if (OB_FAIL(share::ObTabletMappingTableOperator::batch_remove(trans_, *tablet_ids_))) {
    } else if (OB_FAIL(share::ObTabletToTableHistoryOperator::drop_tablet_to_table_history(
                       trans_, schema_version_, *tablet_ids_))) {
    } else if (OB_FAIL(arg.init(*tablet_ids_))) {
    } else {
      LOG_INFO("generate remove arg", K(arg), K(lbt()), KPC(tablet_ids_));
      int64_t buf_len = arg.get_serialize_size();
      int64_t pos = 0;
      char *buf = (char*)allocator_.alloc(buf_len);
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail alloc memory", KR(ret));
      } else if (OB_FAIL(arg.serialize(buf, buf_len, pos))) {
      } else if (OB_FAIL(share::ObShareUtil::set_default_timeout_ctx(ctx, default_timeout_ts))) {
      } else {
        do {
          if (ctx.is_timeouted()) {
            ret = OB_TIMEOUT;
            LOG_WARN("already timeout", KR(ret), K(ctx));
          } else if (OB_FAIL(query::ObInnerSQLConnectionAccess::register_multi_data_source(
                                 conn,
                                 transaction::ObTxDataSourceType::DELETE_TABLET_NEW_MDS,
                                 buf,
                                 buf_len))) {
            LOG_WARN("fail to register_tx_data", KR(ret), K(arg), K(buf), K(buf_len));
            if (OB_LS_LOCATION_LEADER_NOT_EXIST == ret || OB_NOT_MASTER == ret) {
              LOG_INFO("fail to find leader, try again", K(arg));
              ob_usleep(SLEEP_INTERVAL);
            }
          }
        } while (OB_LS_LOCATION_LEADER_NOT_EXIST == ret || OB_NOT_MASTER == ret);
      }
    }
  }
  return ret;
}

} // rootserver
} // oceanbase
