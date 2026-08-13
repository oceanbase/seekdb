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

#define USING_LOG_PREFIX SHARE

#include "common/ob_timeout_ctx.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/string/ob_sql_string.h"
#include "share/config/ob_server_config.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_tablet_checksum_operator.h"

namespace oceanbase
{
namespace share
{
using namespace oceanbase::common;
using namespace oceanbase::common::sqlclient;

void ObTabletChecksumItem::reset()
{

  tablet_id_.reset();
  data_checksum_ = -1;
  row_count_ = 0;
  compaction_scn_.reset();
  column_meta_.reset();
}

bool ObTabletChecksumItem::is_valid() const
{
  return tablet_id_.is_valid()
      && compaction_scn_.is_valid()
      && column_meta_.is_valid();
}

ObTabletChecksumItem &ObTabletChecksumItem::operator=(const ObTabletChecksumItem &other)
{

  tablet_id_ = other.tablet_id_;
  data_checksum_ = other.data_checksum_;
  row_count_ = other.row_count_;
  compaction_scn_ = other.compaction_scn_;
  column_meta_.assign(other.column_meta_);
  return *this;
}



int ObTabletChecksumItem::verify_tablet_column_checksum(const ObTabletLocalChecksumItem &local_item) const
{
  int ret = OB_SUCCESS;

  // __all_tablet_checksum is keyed by compaction_scn and tablet_id; runtime LS
  // placement is deliberately not part of the cross-cluster checksum identity.
  if (tablet_id_ != local_item.tablet_id_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(local_item), K(*this));
  } else {
    // Only compare row_count and column_checksum in the same compaction_scn. The
    // data checksum may differ between primary and restored copies after medium
    // compaction, so it is deliberately excluded here.
    if (compaction_scn_ == local_item.compaction_scn_) {
      bool is_same_column_checksum = false;
      if (OB_FAIL(column_meta_.check_equal(local_item.column_meta_, is_same_column_checksum))) {
      } else if ((row_count_ != local_item.row_count_) || !is_same_column_checksum) {
        ret = OB_CHECKSUM_ERROR;
        LOG_DBA_ERROR(OB_CHECKSUM_ERROR, "msg", "fatal checksum error", KR(ret), K(is_same_column_checksum), K(local_item), K(*this));
      }
    }
  }
  return ret;
}

int ObTabletChecksumItem::assign(const ObTabletLocalChecksumItem &local_item)
{
  int ret = OB_SUCCESS;
  if (!local_item.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(local_item));
  } else {

    tablet_id_ = local_item.tablet_id_;
    data_checksum_ = local_item.data_checksum_;
    row_count_ = local_item.row_count_;
    compaction_scn_ = local_item.compaction_scn_;
    if (OB_FAIL(column_meta_.assign(local_item.column_meta_))) {
    }
  }
  return ret;
}

int ObTabletChecksumItem::assign(const ObTabletChecksumItem &other)
{
  int ret = OB_SUCCESS;
  if (!other.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(other));
  } else if (this != &other) {
    reset();

    tablet_id_ = other.tablet_id_;
    data_checksum_ = other.data_checksum_;
    row_count_ = other.row_count_;
    compaction_scn_ = other.compaction_scn_;
    if (OB_FAIL(column_meta_.assign(other.column_meta_))) {
    }
  }
  return ret;
}

///////////////////////////////////////////////////////////////////////////////

int ObTabletChecksumOperator::load_tablet_checksum_items(
    ObISQLClient &sql_client,
    const ObIArray<ObTabletID> &tablet_ids,
    const SCN &compaction_scn,
    ObIArray<ObTabletChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  const int64_t tablet_cnt = tablet_ids.count();
  int64_t start_idx = 0;
  int64_t end_idx = min(MAX_BATCH_COUNT, tablet_cnt);
  ObSqlString sql;
  if (OB_UNLIKELY(tablet_cnt < 1 || !compaction_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(tablet_cnt), K(compaction_scn));
  }
  while (OB_SUCC(ret) && (start_idx < end_idx)) {
    sql.reuse();
    if (OB_FAIL(construct_load_sql_str_(tablet_ids, start_idx, end_idx, compaction_scn, sql))) {
    } else if (OB_FAIL(load_tablet_checksum_items(sql_client, sql, items))) {
    } else {
      start_idx = end_idx;
      end_idx = min(start_idx + MAX_BATCH_COUNT, tablet_cnt);
    }
  }
  return ret;
}

int ObTabletChecksumOperator::load_tablet_checksum_items(
    ObISQLClient &sql_client,
    const ObSqlString &sql,
    ObIArray<ObTabletChecksumItem> &items)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObISQLClient::ReadResult, res) {
    sqlclient::ObMySQLResult *result = NULL;
    if (OB_UNLIKELY(!sql.is_valid())) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid arguments", KR(ret), K(sql));
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected, query result must not be NULL", KR(ret), K(sql));
    } else {
      while (OB_SUCC(ret)) {
        ObTabletChecksumItem item;
        if (OB_FAIL(result->next())) {
          if (OB_ITER_END != ret) {
            LOG_WARN("fail to get next row", KR(ret));
          }
        } else {
          int64_t tablet_id = -1;
          uint64_t compaction_scn_val = 0;
          ObString column_meta_str;
          EXTRACT_UINT_FIELD_MYSQL(*result, "compaction_scn", compaction_scn_val, uint64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "tablet_id", tablet_id, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "data_checksum", item.data_checksum_, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "row_count", item.row_count_, int64_t);
          EXTRACT_VARCHAR_FIELD_MYSQL(*result, "column_checksums", column_meta_str);

          if (FAILEDx(item.compaction_scn_.convert_for_inner_table_field(compaction_scn_val))) {
            LOG_WARN("fail to convert val to SCN", KR(ret), K(compaction_scn_val));
          } else {
            item.tablet_id_ = (uint64_t)tablet_id;
            if (OB_FAIL(item.column_meta_.set_with_str(column_meta_str))) {
            }
#ifdef ERRSIM
            if (OB_SUCC(ret)) {
              ret = OB_E(EventTable::EN_MOCK_LARGE_COLUMN_META) ret;
              if (OB_FAIL(ret)) {
                ret = OB_SUCCESS;
                if (OB_FAIL(ObTabletLocalChecksumOperator::recover_mock_column_meta(item.column_meta_))) {
                  LOG_ERROR("fail to recover mock large column meta", KR(ret));
                } else {
                  LOG_INFO("ERRSIM EN_MOCK_LARGE_COLUMN_META", K(ret));
                }
              }
            }
#endif
            if (FAILEDx(items.push_back(item))) {
              LOG_WARN("fail to push back item", KR(ret), K(item));
            }
          }
        }
      }
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  return ret;
}

int ObTabletChecksumOperator::construct_load_sql_str_(const common::ObIArray<ObTabletID> &tablet_ids,
    const int64_t start_idx,
    const int64_t end_idx,
    const SCN &compaction_scn,
    common::ObSqlString &sql)
{
  int ret = OB_SUCCESS;

  const int64_t tablet_cnt = tablet_ids.count();
  if ((start_idx < 0) || (end_idx > tablet_cnt) ||
      (start_idx > end_idx) || (tablet_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(start_idx), K(end_idx), K(tablet_cnt));
  } else if (OB_FAIL(sql.append_fmt("SELECT * FROM %s WHERE compaction_scn = "
      "%lu and tablet_id IN (", OB_ALL_TABLET_CHECKSUM_TNAME,
      compaction_scn.get_val_for_inner_table_field()))) {
  } else {
    for (int64_t idx = start_idx; OB_SUCC(ret) && (idx < end_idx); ++idx) {
      const ObTabletID &tablet_id = tablet_ids.at(idx);
      if (OB_UNLIKELY(!tablet_id.is_valid())) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("invalid tablet id", KR(ret), K(tablet_id), K(idx));
      } else if (OB_FAIL(sql.append_fmt(
          "%ld%s",
          tablet_id.id(),
          ((idx == end_idx - 1) ? ")" : ", ")))) {
      }
    }
    if (FAILEDx(sql.append_fmt(" ORDER BY tablet_id"))) {
      SHARE_LOG(WARN, "fail to assign sql string", KR(ret), K(compaction_scn), K(tablet_cnt));
    }
  }
  return ret;
}


int ObTabletChecksumOperator::update_tablet_checksum_items(
    ObISQLClient &sql_client,
    ObIArray<ObTabletChecksumItem> &items)
{
  return insert_or_update_tablet_checksum_items_(sql_client, items, true/*is_update*/);
}

int ObTabletChecksumOperator::insert_or_update_tablet_checksum_items_(
    ObISQLClient &sql_client,
    ObIArray<ObTabletChecksumItem> &items,
    const bool is_update)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObMySQLTransaction trans;

  const int64_t item_cnt = items.count();
  if (OB_UNLIKELY(item_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(item_cnt));
  } else if (OB_FAIL(trans.start(&sql_client))) {
  } else {
    int64_t remain_cnt = item_cnt;
    int64_t report_idx = 0;
    while (OB_SUCC(ret) && (remain_cnt > 0)) {
      sql.reuse();
      if (OB_FAIL(sql.assign_fmt("INSERT INTO %s (compaction_scn, tablet_id, data_checksum, "
          "row_count, column_checksums, gmt_modified, gmt_create) VALUES", OB_ALL_TABLET_CHECKSUM_TNAME))) {
      } else {
        ObArenaAllocator allocator;

        int64_t cur_batch_cnt = ((remain_cnt < MAX_BATCH_COUNT) ? remain_cnt : MAX_BATCH_COUNT);
        int64_t bias = item_cnt - remain_cnt;
        for (int64_t i = 0; OB_SUCC(ret) && (i < cur_batch_cnt); ++i) {
          const ObTabletChecksumItem &item = items.at(bias + i);
          if (OB_UNLIKELY(!item.is_valid())) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid tablet checksum item", KR(ret), K(item));
          } else {
            const uint64_t compaction_scn_val = item.compaction_scn_.get_val_for_inner_table_field();
            ObString b_column_meta;
            if (OB_FAIL(sql.append_fmt("(%lu, '%lu', %ld, %ld, ",
              compaction_scn_val, item.tablet_id_.id(),
              item.data_checksum_, item.row_count_))) {
            } else if (OB_FAIL(item.column_meta_.get_hex_str(allocator, b_column_meta))) {
            } else if (OB_FAIL(sql.append_fmt("X"))) {
            } else if (OB_FAIL(sql.append_fmt("'%.*s', ", b_column_meta.length(), b_column_meta.ptr()))) {
            }
          }
          if (FAILEDx(sql.append_fmt("now(6), now(6))%s", ((i == cur_batch_cnt - 1) ? " " : ", ")))) {
            LOG_WARN("fail to assign sql", KR(ret), K(i), K(bias), K(item));
          }
        }

        if (OB_SUCC(ret) && is_update) {
          if (OB_FAIL(sql.append(" ON DUPLICATE KEY UPDATE "))) {
          } else if (OB_FAIL(sql.append(" data_checksum = values(data_checksum)"))
                    || OB_FAIL(sql.append(", row_count = values(row_count)"))
                    || OB_FAIL(sql.append(", column_checksums = values(column_checksums)"))) {
            LOG_WARN("fail to append sql string", KR(ret), K(sql));
          }
        }

        if (OB_SUCC(ret)) {
          int64_t affected_rows = 0;
          if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
          } else if (!is_update) {  // do not check affected_rows, when is_update = true
            if (OB_UNLIKELY(affected_rows != cur_batch_cnt)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("invalid affected rows", KR(ret), K(affected_rows), K(cur_batch_cnt));
            }
          }
          if (OB_SUCC(ret)) {
            remain_cnt -= cur_batch_cnt;
          }
        }
      }
    } // end loop while

    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true /*commit*/))) {
      }
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = trans.end(false /*commit*/))) {
      }
    }
  }
  return ret;
}

int ObTabletChecksumOperator::delete_tablet_checksum_items(
    ObISQLClient &sql_client,
    const SCN &gc_compaction_scn,
    const int64_t limit_cnt,
    int64_t &affected_rows)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  affected_rows = 0;

  const uint64_t gc_scn_val = gc_compaction_scn.is_valid() ? gc_compaction_scn.get_val_for_inner_table_field() : 0;
  if (OB_UNLIKELY(!gc_compaction_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(gc_compaction_scn));
  } else if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE compaction_scn <= %lu"
    " AND tablet_id != %ld limit %ld", OB_ALL_TABLET_CHECKSUM_TNAME,
    gc_scn_val, ObTabletID::MIN_VALID_TABLET_ID, limit_cnt))) {
  } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
  } else {
    LOG_INFO("succ to delete tablet checksum items", K(gc_compaction_scn), K(affected_rows), K(limit_cnt));
  }
  return ret;
}

int ObTabletChecksumOperator::delete_special_tablet_checksum_items(
    ObISQLClient &sql_client,
    const SCN &gc_compaction_scn)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;

  const uint64_t gc_scn_val = gc_compaction_scn.is_valid() ? gc_compaction_scn.get_val_for_inner_table_field() : 0;
  if (OB_UNLIKELY(!gc_compaction_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(gc_compaction_scn));
  } else if (OB_FAIL(sql.assign_fmt("DELETE FROM %s WHERE compaction_scn <= %lu"
    " AND tablet_id=%ld", OB_ALL_TABLET_CHECKSUM_TNAME,
    gc_scn_val, ObTabletID::MIN_VALID_TABLET_ID))) {
  } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
  } else {
    LOG_INFO("succ to delete special tablet checksum items", K(gc_compaction_scn), K(affected_rows));
  }
  return ret;
}


int ObTabletChecksumOperator::load_all_compaction_scn(
    ObISQLClient &sql_client,
    ObIArray<SCN> &compaction_scn_arr)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t estimated_timeout_us = 0;
  ObTimeoutCtx timeout_ctx;
  int64_t start_time_us = ObTimeUtility::current_time();
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObMySQLResult *result = nullptr;
    // set trx_timeout and query_timeout based on tablet_cnt
    if (OB_FAIL(ObTabletChecksumOperator::get_estimated_timeout_us(sql_client,
                                          estimated_timeout_us))) {
    } else if (OB_FAIL(timeout_ctx.set_trx_timeout_us(estimated_timeout_us))) {
    } else if (OB_FAIL(timeout_ctx.set_timeout(estimated_timeout_us))) {
    } else if (OB_FAIL(sql.assign_fmt("SELECT DISTINCT compaction_scn as dis_compaction_scn FROM %s"
        " ORDER BY compaction_scn ASC", OB_ALL_TABLET_CHECKSUM_TNAME))) {
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get sql result", KR(ret), K(sql));
    } else {
      while (OB_SUCC(ret)) {
        uint64_t compaction_scn_val = 0;
        if (OB_FAIL(result->next())) {
          if (OB_ITER_END != ret) {
            LOG_WARN("fail to get next row", KR(ret));
          }
        } else {
          EXTRACT_UINT_FIELD_MYSQL(*result, "dis_compaction_scn", compaction_scn_val, uint64_t);
        }

        SCN tmp_compaction_scn;
        if (FAILEDx(tmp_compaction_scn.convert_for_inner_table_field(compaction_scn_val))) {
          LOG_WARN("fail to convert val to SCN", KR(ret), K(compaction_scn_val));
        } else if (OB_UNLIKELY(!tmp_compaction_scn.is_valid())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid compaction_scn", KR(ret), K(tmp_compaction_scn), K(sql));
        } else if (OB_FAIL(compaction_scn_arr.push_back(tmp_compaction_scn))) {
        }
      } // end for while

      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
      }
    }
  }
  int64_t cost_time_us = ObTimeUtility::current_time() - start_time_us;
  LOG_INFO("finish to load all compaction_scn", KR(ret), K(cost_time_us),
           K(estimated_timeout_us), K(sql), K(compaction_scn_arr));
  return ret;
}

int ObTabletChecksumOperator::is_first_tablet_checksum_exist(
    common::ObISQLClient &sql_client,
    const SCN &compaction_scn,
    bool &is_exist)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!compaction_scn.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(compaction_scn));
  }
  if (OB_SUCC(ret)) {
    is_exist = false;

    ObSqlString sql;
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      uint64_t compaction_scn_val = compaction_scn.get_val_for_inner_table_field();
      if (OB_FAIL(sql.assign_fmt("SELECT COUNT(*) AS cnt FROM %s WHERE "
            "compaction_scn >= %lu AND tablet_id = %lu", OB_ALL_TABLET_CHECKSUM_TNAME,
            compaction_scn_val, ObTabletID::MIN_VALID_TABLET_ID))) {
      } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get sql result", KR(ret), K(sql));
      } else if (OB_FAIL(result->next())) {
      } else {
        int64_t cnt = 0;
        EXTRACT_INT_FIELD_MYSQL(*result, "cnt", cnt, int64_t);
        if (OB_SUCC(ret)) {
          if (cnt >= 1) {
            is_exist = true;
          } else if (0 == cnt) {
            is_exist = false;
          } else {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected first tablet checksum count", KR(ret), K(sql), K(cnt));
          }
        }
      }
    }
  }
  return ret;
}


int ObTabletChecksumOperator::get_tablet_cnt(
    ObISQLClient &sql_client,
    int64_t &tablet_cnt)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  SMART_VAR(ObISQLClient::ReadResult, res) {
    ObMySQLResult *result = nullptr;
    if (OB_FAIL(sql.append_fmt("SELECT COUNT(*) as cnt from %s", OB_ALL_TABLET_CHECKSUM_TNAME))) {
    } else if (OB_FAIL(sql_client.read(res, sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to get mysql result", KR(ret), K(sql));
    } else if (OB_FAIL(result->next())) {
    } else {
      EXTRACT_INT_FIELD_MYSQL(*result, "cnt", tablet_cnt, int64_t);
    }
  }
  return ret;
}

int ObTabletChecksumOperator::get_estimated_timeout_us(
    ObISQLClient &sql_client,
    int64_t &estimated_timeout_us)
{
  int ret = OB_SUCCESS;
  int64_t tablet_cnt = 0;
  if (OB_FAIL(ObTabletChecksumOperator::get_tablet_cnt(sql_client, tablet_cnt))) {
  } else {
    estimated_timeout_us = tablet_cnt * 1000L; // 1ms for each tablet
    const int64_t default_timeout_us = 9 * 1000 * 1000L;
    estimated_timeout_us = MAX(estimated_timeout_us, default_timeout_us);
    estimated_timeout_us = MIN(estimated_timeout_us, 3600 * 1000 * 1000L);
    estimated_timeout_us = MAX(estimated_timeout_us, GCONF.rpc_timeout);
  }
  return ret;
}

} // namespace share
} // namespace oceanbase
