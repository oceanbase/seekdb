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

#include "lib/lock/ob_mutex.h"
#include "share/ob_global_autoinc_service.h"
#include "share/ob_server_struct.h"
#include "share/sequence/ob_sequence_cache.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::share;

namespace share
{

int ObAutoIncCacheNode::init(const uint64_t start,
                             const uint64_t end,
                             const uint64_t sync_value,
                             const int64_t autoinc_version)
{
  int ret = OB_SUCCESS;
  if (start <= 0 || end < start || sync_value > start || autoinc_version < OB_INVALID_VERSION) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(start), K(end), K(sync_value));
  } else {
    start_ = start;
    end_ = end;
    sync_value_ = sync_value;
    autoinc_version_ = autoinc_version;
  }
  return ret;
}

bool ObAutoIncCacheNode::need_fetch_next_node(const uint64_t base_value,
                                              const uint64_t desired_cnt,
                                              const uint64_t max_value) const
{
  bool bret = false;
  if (OB_UNLIKELY(end_ == max_value)) {
    bret = false;
  } else if (OB_LIKELY(end_ >= desired_cnt)) {
    uint64_t new_base_value = std::max(base_value, start_);
    bret = new_base_value > (end_ - desired_cnt + 1);
  } else {
    bret = true;
  }
  return bret;
}

int ObAutoIncCacheNode::with_new_start(const uint64_t new_start)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
    LOG_WARN("update invalid cache is not allowed", K(ret));
  } else if (OB_UNLIKELY(new_start > end_ || new_start < start_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(new_start), K_(start), K_(end));
  } else {
    start_ = new_start;
  }
  return ret;
}

int ObAutoIncCacheNode::with_new_end(const uint64_t new_end)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid())) {
    ret = OB_NOT_INIT;
    LOG_WARN("update invalid cache is not allowed", K(ret));
  } else if (OB_UNLIKELY(new_end < end_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(new_end), K_(end));
  } else {
    end_ = new_end;
  }
  return ret;
}


int ObGlobalAutoIncService::init(ObMySQLProxy *mysql_proxy)
{
  int ret = OB_SUCCESS;
  ObMemAttr attr(ObModIds::OB_AUTOINCREMENT);
  if (OB_ISNULL(mysql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(mysql_proxy));
  } else if (OB_FAIL(inner_table_proxy_.init(mysql_proxy))) {
    LOG_WARN("init inner table proxy failed", K(ret));
  } else if (OB_FAIL(autoinc_map_.create(ObGlobalAutoIncService::INIT_HASHMAP_SIZE,
                                         attr,
                                         attr))) {
    LOG_WARN("init autoinc_map_ failed", K(ret));
  } else {
    for (int64_t i = 0; i < MUTEX_NUM; ++i) {
      op_mutex_[i].set_latch_id(common::ObLatchIds::AUTO_INCREMENT_GAIS_LOCK);
    }
    is_inited_ = true;
  }
  return ret;
}

int ObGlobalAutoIncService::mtl_init(ObGlobalAutoIncService *&gais)
{
  int ret = OB_SUCCESS;
  ObMySQLProxy *mysql_proxy = GCTX.sql_proxy_;
  ret = gais->init(mysql_proxy);
  return ret;
}

void ObGlobalAutoIncService::destroy()
{
  autoinc_map_.destroy();
  inner_table_proxy_.reset();
  is_inited_ = false;
}

int ObGlobalAutoIncService::clear()
{
  int ret = OB_SUCCESS;
  if (autoinc_map_.size() > 0) {
    ret = autoinc_map_.clear();
  }
  return ret;
}

int ObGlobalAutoIncService::handle_next_autoinc_request(
    const ObGAISNextAutoIncValReq &request,
    obcall::ObGAISNextValResult &result)
{
  int ret = OB_SUCCESS;
  const AutoincKey &key = request.autoinc_key_;
  const uint64_t desired_count = request.desired_cnt_;
  lib::ObMutex &mutex = op_mutex_[key.hash() % MUTEX_NUM];
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("global service is not init", K(ret));
  } else if (OB_FAIL(mutex.lock())) {
    LOG_WARN("fail to get lock", K(ret));
  } else {
    ObAutoIncCacheNode cache_node;
    int err = autoinc_map_.get_refactored(key.table_id_, cache_node);
    
    const int64_t request_version = request.autoinc_version_;
    LOG_TRACE("begin handle req autoinc request", K(request), K(cache_node));
    if (OB_UNLIKELY(OB_SUCCESS != err && OB_HASH_NOT_EXIST != err)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get seq value", K(ret), K(key));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(!cache_node.is_valid()
                          || (request_version == cache_node.autoinc_version_
                            && cache_node.need_fetch_next_node(
                              request.base_value_, desired_count, request.max_value_)))) {
      OZ(fetch_next_node_(request, cache_node));
    } else if (OB_UNLIKELY(request_version > cache_node.autoinc_version_)) {
      LOG_INFO("start to reset old global table node", K(key), K(request_version),
                K(cache_node.autoinc_version_));
      cache_node.reset();
      OZ(fetch_next_node_(request, cache_node));
    } else if (OB_UNLIKELY(request_version < cache_node.autoinc_version_)) {
      ret = OB_AUTOINC_CACHE_NOT_EQUAL;
      LOG_WARN("request autoinc_version is less than autoinc_version_ in table_node,"
               "it should retry", KR(ret), K(key), K(request_version), K(cache_node));
    }
    if (OB_SUCC(ret)) {
      if (OB_UNLIKELY(!cache_node.is_valid())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected cache node", K(ret), K(cache_node));
      } else {
        const uint64_t start_inclusive = std::max(cache_node.start_, request.base_value_);
        const uint64_t max_value = request.max_value_;
        uint64_t end_inclusive = 0;
        if (max_value >= request.desired_cnt_ &&
             start_inclusive <= max_value - request.desired_cnt_ + 1) {
          end_inclusive = start_inclusive + request.desired_cnt_ - 1;
          if (OB_UNLIKELY(end_inclusive > cache_node.end_)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected value", K(ret), K(end_inclusive), K(cache_node));
          } else if (OB_UNLIKELY(end_inclusive == cache_node.end_)) {
            // the cache node is run out
            cache_node.reset();
          } else if (OB_FAIL(cache_node.with_new_start(end_inclusive + 1))) {
            LOG_WARN("fail to update sequence value", K(ret), K(cache_node), K(end_inclusive));
          }
        } else if (OB_FAIL(cache_node.with_new_start(max_value))) {
          LOG_WARN("fail to update sequence value", K(ret), K(cache_node), K(max_value));
        } else {
          end_inclusive = max_value;
        }
        if (OB_SUCC(ret)) {
          uint64_t sync_value = cache_node.sync_value_;
          if (request.base_value_ != 0 && request.base_value_ - 1 > sync_value) {
            sync_value = request.base_value_ - 1;
          }
          if (OB_FAIL(result.init(start_inclusive, end_inclusive, sync_value))) {
            LOG_WARN("init result failed", K(ret), K(cache_node));
          } else if (OB_FAIL(autoinc_map_.set_refactored(key.table_id_, cache_node, 1))) {
            LOG_WARN("set autoinc_map_ failed", K(ret));
          }
        }
        LOG_TRACE("after handle req autoinc request", K(request), K(cache_node));
      }
    }
    mutex.unlock();
  }
  return ret;
}

int ObGlobalAutoIncService::handle_curr_autoinc_request(const ObGAISAutoIncKeyArg &request,
                                                        obcall::ObGAISCurrValResult &result)
{
  int ret = OB_SUCCESS;
  const AutoincKey &key = request.autoinc_key_;
  uint64_t sequence_value = 0;
  uint64_t sync_value = 0;
  lib::ObMutex &mutex = op_mutex_[key.hash() % MUTEX_NUM];
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("global service is not init", K(ret));
  } else if (OB_FAIL(mutex.lock())) {
    LOG_WARN("fail to get lock", K(ret));
  } else {
    ObAutoIncCacheNode cache_node;
    
    int err = autoinc_map_.get_refactored(key.table_id_, cache_node);
    const int64_t request_version = request.autoinc_version_;
    LOG_TRACE("start handle get autoinc request", K(request), K(cache_node));
    if (OB_UNLIKELY(OB_SUCCESS != err && OB_HASH_NOT_EXIST != err)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get seq value", K(ret), K(key));
    } else if (OB_LIKELY(cache_node.is_valid())
              && request_version == cache_node.autoinc_version_) {
      // get autoinc values from cache
      sequence_value = cache_node.start_;
      sync_value = cache_node.sync_value_;
      // hash not exist or cache node is non-valid,
      // read value from inner table
    } else if (OB_FAIL(read_value_from_inner_table_(key, request_version, sequence_value,
                                                    sync_value))) {
      LOG_WARN("fail to read value from inner table", KR(ret), K_(key.table_id));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(result.init(sequence_value, sync_value))) {
        LOG_WARN("failed to init result", KR(ret), K_(key.table_id),
                  K(request_version), K(cache_node));
      }
    }
    mutex.unlock();
  }
  return ret;
}

int ObGlobalAutoIncService::handle_push_autoinc_request(
    const ObGAISPushAutoIncValReq &request,
    uint64_t &sync_value)
{
  int ret = OB_SUCCESS;
  const AutoincKey &key = request.autoinc_key_;
  lib::ObMutex &mutex = op_mutex_[key.hash() % MUTEX_NUM];
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("global service is not init", K(ret));
  } else if (OB_FAIL(mutex.lock())) {
    LOG_WARN("fail to get lock", K(ret));
  } else {
    ObAutoIncCacheNode cache_node;
    
    int err = autoinc_map_.get_refactored(key.table_id_, cache_node);
    const int64_t request_version = request.autoinc_version_;
    const uint64_t insert_value = request.base_value_;
    LOG_TRACE("start handle push global autoinc request", K(request), K(cache_node));
    if (OB_UNLIKELY(OB_SUCCESS != err && OB_HASH_NOT_EXIST != err)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get seq value", K(ret), K(key), K(err));
    } else if (OB_UNLIKELY(OB_HASH_NOT_EXIST == err
                        || (request_version == cache_node.autoinc_version_
                            && cache_node.need_sync(insert_value))
                        // cache node is expired
                        || (request_version > cache_node.autoinc_version_))) {
      cache_node.reset();
      if (OB_FAIL(sync_value_to_inner_table_(request, cache_node, sync_value))) {
        LOG_WARN("sync to inner table failed", K(ret));
      } else if (OB_FAIL(autoinc_map_.set_refactored(key.table_id_, cache_node, 1))) {
        LOG_WARN("set autoinc_map_ failed", K(ret));
      }
    // old request just ignore
    } else if (OB_UNLIKELY(request_version < cache_node.autoinc_version_)) {
      ret = OB_AUTOINC_CACHE_NOT_EQUAL;
      LOG_WARN("request autoinc_version is less than cache_node autoinc_version", KR(ret),
               K(key), K(request_version), K(cache_node.autoinc_version_));
    } else if (OB_LIKELY(request_version == cache_node.autoinc_version_)) {
      if (insert_value < cache_node.start_ && insert_value < cache_node.sync_value_) {
        // insert value is too small and no need to update node
      } else {
        sync_value = MAX(MAX(insert_value, cache_node.sync_value_), cache_node.start_ - 1);
        if (cache_node.is_valid()) {
          cache_node.start_ = sync_value + 1;
          cache_node.sync_value_ = sync_value;
        }
        if (OB_SUCC(ret) && OB_FAIL(autoinc_map_.set_refactored(key.table_id_, cache_node, 1))) {
          LOG_WARN("set autoinc_map_ failed", K(ret));
        }
      }
    }
    mutex.unlock();
  }
  return ret;
}

int ObGlobalAutoIncService::handle_clear_autoinc_cache_request(const ObGAISAutoIncKeyArg &request)
{
  int ret = OB_SUCCESS;
  const AutoincKey &key = request.autoinc_key_;
  lib::ObMutex &mutex = op_mutex_[key.hash() % MUTEX_NUM];
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("global service is not init", K(ret));
  } else if (OB_FAIL(mutex.lock())) {
    LOG_WARN("fail to get lock", K(ret));
  } else {
    LOG_TRACE("start clear autoinc cache request", K(request));
    if (OB_FAIL(autoinc_map_.erase_refactored(key.table_id_))) {
      LOG_WARN("fail to erase autoinc cache map key", K(ret));
    }
    if (ret == OB_HASH_NOT_EXIST) {
      ret = OB_SUCCESS;
    }
    mutex.unlock();
  }
  return ret;
}

int ObGlobalAutoIncService::handle_next_sequence_request(
  const ObGAISNextSequenceValReq &request,
  obcall::ObGAISNextSequenceValResult &result)
{
  int ret = OB_SUCCESS;
  ObSequenceCache *sequence_cache = &share::ObSequenceCache::get_instance();
  ObArenaAllocator allocator;
  return sequence_cache->nextval(request.schema_, allocator ,result.nextval_);
}

// moved definition to storage/tx_storage/ob_ls_service.cpp(ObLS real user)
// Note: master tenant-elim changed the original body(removed the tenant_id parameter), HOST(ob_ls_service.cpp) must be synced (see routing item)

int ObGlobalAutoIncService::fetch_next_node_(const ObGAISNextAutoIncValReq &request,
                                             ObAutoIncCacheNode &node)
{
  int ret = OB_SUCCESS;
  uint64_t desired_count = std::max(request.cache_size_, request.desired_cnt_);
  uint64_t start_inclusive = 0;
  uint64_t end_inclusive = 0;
  uint64_t sync_value = 0;
  const int64_t autoinc_version =  request.autoinc_version_;
  if (OB_FAIL(inner_table_proxy_.next_autoinc_value(request.autoinc_key_,
                                                    request.offset_,
                                                    request.increment_,
                                                    request.base_value_,
                                                    request.max_value_,
                                                    desired_count,
                                                    autoinc_version,
                                                    start_inclusive,
                                                    end_inclusive,
                                                    sync_value))) {
    LOG_WARN("fail to require autoinc value from inner table", K(ret));
  } else if (OB_LIKELY(node.is_valid() && (node.end_ == start_inclusive - request.increment_))) {
    if (OB_FAIL(node.with_new_end(end_inclusive))) {
      LOG_WARN("fail to update available value", K(ret), K(node), K(end_inclusive));
    } else {
      LOG_TRACE("fetch next node done", K(request), K(node));
    }
  } else if (OB_FAIL(node.init(start_inclusive, end_inclusive, sync_value, autoinc_version))){
    LOG_WARN("fail to init node", K(ret), K(start_inclusive), K(end_inclusive), K(sync_value));
  } else {
    LOG_TRACE("fetch next node done", K(request), K(node));
  }
  return ret;
}

int ObGlobalAutoIncService::read_value_from_inner_table_(const share::AutoincKey &key,
                                                         const int64_t &autoinc_version,
                                                         uint64_t &sequence_val,
                                                         uint64_t &sync_val)
{
  return inner_table_proxy_.get_autoinc_value(key, autoinc_version, sequence_val, sync_val);
}

int ObGlobalAutoIncService::sync_value_to_inner_table_(
    const ObGAISPushAutoIncValReq &request,
    ObAutoIncCacheNode &node,
    uint64_t &sync_value)
{
  int ret = OB_SUCCESS;
  const uint64_t insert_value = request.base_value_;
  const int64_t autoinc_version = request.autoinc_version_;
  const uint64_t next_cache_boundary =
    calc_next_cache_boundary(insert_value, request.cache_size_, request.max_value_);
  uint64_t seq_value = insert_value;
  if (OB_FAIL(inner_table_proxy_.sync_autoinc_value(request.autoinc_key_,
                                                    next_cache_boundary,
                                                    request.max_value_,
                                                    autoinc_version,
                                                    seq_value,
                                                    sync_value))) {
    LOG_WARN("fail to sync autoinc value to inner table", K(ret));
  } else if (insert_value == request.max_value_) {
    if (OB_FAIL(node.init(request.max_value_, request.max_value_,
                          request.max_value_, autoinc_version))) {
      LOG_WARN("fail to init node", K(ret), K(request.max_value_));
    }
  } else {
    // updates directly without checking, this node may be invalid.
    node.start_ = seq_value;
    node.end_ = sync_value;
    node.sync_value_ = seq_value - 1;
    node.autoinc_version_ = autoinc_version;
  }
  return ret;
}

} // share
} // oceanbase
