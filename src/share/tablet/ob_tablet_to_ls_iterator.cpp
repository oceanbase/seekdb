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

#include "share/tablet/ob_tablet_to_ls_iterator.h"
#include "src/share/ob_rpc_struct.h"

namespace oceanbase
{
namespace share
{
ObTenantTabletToLSIterator::ObTenantTabletToLSIterator()
    : inited_(false),
      inner_idx_(0),
      ls_white_list_(),
      inner_tablet_infos_(),
      sql_proxy_(NULL)
{
}

int ObTenantTabletToLSIterator::init(
    common::ObISQLClient &sql_proxy)
{
  const ObArray<ObLSID> ls_white_list;
  return init(sql_proxy, ls_white_list);
}

int ObTenantTabletToLSIterator::init(
    common::ObISQLClient &sql_proxy,
    const common::ObIArray<ObLSID> &ls_white_list)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", KR(ret));
  } else if (OB_FAIL(ls_white_list_.assign(ls_white_list))) {
  } else {
    sql_proxy_ = &sql_proxy;
    inited_ = true;
  }
  return ret;
}

int ObTenantTabletToLSIterator::next(ObTabletLSPair &pair)
{
  int ret = OB_SUCCESS;
  ObTabletToLSInfo info;
  if (OB_FAIL(next(info))) {
    if (OB_ITER_END != ret) {
      LOG_WARN("next tablet to LS info fail", KR(ret));
    }
  } else if (OB_FAIL(pair.init(info.get_tablet_id(), info.get_ls_id()))) {
  }
  return ret;
}

int ObTenantTabletToLSIterator::next(ObTabletToLSInfo &info)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(inner_idx_ < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner_idx_ can't be smaller than 0", KR(ret), K_(inner_idx));
  } else {
    info.reset();
    if (inner_idx_ >= inner_tablet_infos_.count()) {
      if (OB_FAIL(prefetch_())) {
        if (OB_UNLIKELY(OB_ITER_END != ret)) {
          LOG_WARN("fail to prfetch", KR(ret));
        }
      } else {
        inner_idx_ = 0;
      }
    }
    if (FAILEDx(info.assign(inner_tablet_infos_[inner_idx_]))) {
    } else {
      ++inner_idx_;
    }
  }
  return ret;
}

int ObTenantTabletToLSIterator::prefetch_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else {
    ObTabletID last_tablet_id; // start with INVALID_TABLET_ID = 0
    if (inner_tablet_infos_.count() > 0) {
      const int64_t last_idx = inner_tablet_infos_.count() - 1;
      last_tablet_id = inner_tablet_infos_.at(last_idx).get_tablet_id();
    }
    inner_tablet_infos_.reset();
    const int64_t range_size = GCONF.tablet_meta_table_scan_batch_count;
    if (OB_FAIL(ObTabletToLSTableOperator::range_get_tablet_info(
        *sql_proxy_,
        ls_white_list_,
        last_tablet_id,
        range_size,
        inner_tablet_infos_))) {
    } else if (inner_tablet_infos_.count() <= 0) {
      ret = OB_ITER_END;
    }
  }
  return ret;
}

} // end namespace share
} // end namespace oceanbase
