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

#include "share/tablet/ob_tablet_table_operator.h"
namespace oceanbase
{
using namespace common;

namespace share
{
ObTabletTableOperator::ObTabletTableOperator()
    : inited_(false), storage_()
{
}

ObTabletTableOperator::~ObTabletTableOperator()
{
  reset();
}

int ObTabletTableOperator::init(share::ObSQLiteConnectionPool *pool)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_ISNULL(pool)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid pool", K(ret));
  } else {
    if (OB_FAIL(storage_.init(pool))) {
    } else {
      inited_ = true;
      LOG_INFO("tablet table operator init success");
    }
  }
  return ret;
}

void ObTabletTableOperator::reset()
{
  inited_ = false;
}

int ObTabletTableOperator::batch_get_tablet_info(
    const ObIArray<compaction::ObTabletCheckInfo> &tablet_ls_infos,
    ObArrayWithMap<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ObSEArray<ObTabletID, 64> tablet_ids;
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ls_infos.count(); ++i) {
      const compaction::ObTabletCheckInfo &check_info = tablet_ls_infos.at(i);
      if (OB_FAIL(tablet_ids.push_back(check_info.get_tablet_id()))) {
      }
    }
    if (OB_SUCC(ret)) {
      ObSEArray<ObTabletRuntimeInfo, 64> infos;
      if (OB_FAIL(storage_.batch_get(tablet_ids, infos))) {
      } else {
        // Convert to ObArrayWithMap
        for (int64_t i = 0; OB_SUCC(ret) && i < infos.count(); ++i) {
          if (OB_FAIL(tablet_infos.push_back(infos.at(i)))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_get(
    const ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  tablet_infos.reset();
  const int64_t tablet_cnt = tablet_ids.count();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(tablet_cnt < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_cnt));
  } else if (OB_FAIL(storage_.batch_get(tablet_ids, tablet_infos))) {
  }
  return ret;
}

int ObTabletTableOperator::batch_update(
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.batch_update(tablet_infos);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_update(
    ObSQLiteConnection *conn,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else {
    ret = storage_.batch_update(conn, tablet_infos);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_remove(
    ObSQLiteConnection *conn,
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(conn)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid connection", K(ret));
  } else {
    ret = storage_.batch_remove(conn, tablet_infos);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

int ObTabletTableOperator::batch_remove(
    const ObIArray<ObTabletRuntimeInfo> &tablet_infos)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ret = storage_.batch_remove(tablet_infos);
    if (OB_FAIL(ret)) {
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
