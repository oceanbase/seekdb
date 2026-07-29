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

#define USING_LOG_PREFIX STORAGE
#include "ob_lob_meta_manager.h"
#include "storage/lob/ob_lob_persistent_iterator.h"

namespace oceanbase
{
namespace storage
{

int ObLobMetaManager::write(ObLobAccessParam& param, ObLobMetaInfo& in_row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persistent_lob_adapter_.write_lob_meta(param, in_row))) {
    LOG_WARN("write lob meta failed.", K(ret), K(param));
  }
  return ret;
}

int ObLobMetaManager::batch_insert(ObLobAccessParam& param, blocksstable::ObDatumRowIterator &iter)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persistent_lob_adapter_.write_lob_meta(param, iter))) {
    LOG_WARN("batch write lob meta failed.", K(ret), K(param));
  }
  return ret;
}

int ObLobMetaManager::batch_delete(ObLobAccessParam& param, blocksstable::ObDatumRowIterator &iter)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persistent_lob_adapter_.erase_lob_meta(param, iter))) {
    LOG_WARN("batch write lob meta failed.", K(ret), K(param));
  }
  return ret;
}

// append
int ObLobMetaManager::append(ObLobAccessParam& param, ObLobMetaWriteIter& iter)
{
  int ret = OB_SUCCESS;
  UNUSED(param);
  UNUSED(iter);
  return ret;
}

// generate LobMetaRow at specified range on demands
// rebuild specified range
// get specified range LobMeta info
int ObLobMetaManager::scan(ObLobAccessParam& param, ObLobMetaScanIter &iter)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(iter.open(param, &persistent_lob_adapter_))) {
    LOG_WARN("open lob scan iter failed.", K(ret), K(param));
  }
  return ret;
}

int ObLobMetaManager::open(ObLobAccessParam &param, ObLobMetaSingleGetter* getter)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(getter->open(param, &persistent_lob_adapter_))) {
    LOG_WARN("open lob scan iter failed.", K(ret), K(param));
  }
  return ret;
}

// erase specified range
int ObLobMetaManager::erase(ObLobAccessParam& param, ObLobMetaInfo& in_row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persistent_lob_adapter_.erase_lob_meta(param, in_row))) {
    LOG_WARN("erase lob meta failed.", K(ret), K(param));
  }
  return ret;
}

// update specified range
int ObLobMetaManager::update(ObLobAccessParam& param, ObLobMetaInfo& old_row, ObLobMetaInfo& new_row)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(persistent_lob_adapter_.update_lob_meta(param, old_row, new_row))) {
    LOG_WARN("update lob meta failed.");
  }
  return ret;
}

int ObLobMetaManager::fetch_lob_id(ObLobAccessParam& param, uint64_t &lob_id)
{
  int ret = OB_SUCCESS;
  if (nullptr != param.lob_id_geneator_) {
    if (OB_FAIL(param.lob_id_geneator_->next_value(lob_id))) {
      LOG_WARN("fail to get next lob_id", K(ret), KPC(param.lob_id_geneator_));
    }
  } else if (OB_FAIL(persistent_lob_adapter_.fetch_lob_id(param, lob_id))) {
    LOG_WARN("fetch lob id failed.", K(ret), K(param));
  }
  return ret;
}

int ObLobMetaManager::getlength(ObLobAccessParam &param, uint64_t &char_len)
{
  int ret = OB_SUCCESS;
  ObLobMetaScanIter meta_iter;
  ObLobMetaScanResult result;
  if (OB_FAIL(scan(param, meta_iter))) {
    LOG_WARN("open lob scan iter failed.", K(ret), K(param));
  }
  while (OB_SUCC(ret)) {
    if (OB_FAIL(meta_iter.get_next_row(result))) {
      if (ret != OB_ITER_END) {
        LOG_WARN("failed to get next row.", K(ret));
      }
    } else if (OB_FAIL(param.is_timeout())) {
      LOG_WARN("access timeout", K(ret), K(param));
    } else {
      char_len += result.info_.char_len_;
    }
  }
  if (ret == OB_ITER_END) {
    ret = OB_SUCCESS;
  }
  return ret;
}


} // storage
} // oceanbase
