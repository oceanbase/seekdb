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

#include "buffer_ctx.h"
#include "share/rc/ob_module_provider.h"
#include "storage/multi_data_source/compile_utility/compile_mapper.h"

namespace oceanbase
{
namespace storage
{
namespace mds
{

void BufferCtxNode::destroy_ctx() {
  if (OB_NOT_NULL(ctx_)) {
    ctx_->~BufferCtx();
    share::g_mp->mds_service()->get_buffer_ctx_allocator().free(ctx_);
    ctx_ = nullptr;
  }
}

int BufferCtxNode::serialize(char *buf, const int64_t buf_len, int64_t &pos) const
{
  int ret = OB_SUCCESS;
  MDS_TG(10_ms);
  if (OB_NOT_NULL(ctx_)) {
    // When serializing, if ctx is not null, then its type must be valid, here we defend against it, otherwise the error during deserialization will increase the difficulty of troubleshooting
    MDS_ASSERT(ctx_->get_binding_type_id() != INVALID_VALUE);
    int64_t type_id = ctx_->get_binding_type_id();
    if (MDS_FAIL(serialization::encode(buf, buf_len, pos, type_id))) {
      MDS_LOG(ERROR, "serialize buffer ctx id failed", KR(ret), K(type_id));
    } else if (MDS_FAIL(ctx_->serialize(buf, buf_len, pos))) {
      MDS_LOG(ERROR, "serialize buffer ctx impl failed", KR(ret), K(type_id));
    } else {
      MDS_LOG(DEBUG, "serialize buffer ctx impl success", KR(ret), K(type_id), K(buf_len), K(pos));
    }
  } else {
    int64_t type_id = INVALID_VALUE;
    if (MDS_FAIL(serialization::encode(buf, buf_len, pos, type_id))) {
      MDS_LOG(ERROR, "serialize invalid buffer ctx id failed", KR(ret), K(type_id));
    } else {
      MDS_LOG(DEBUG, "serialize invalid buffer ctx id failed", KR(ret), K(type_id), K(buf_len), K(pos));
    }
  }
  return ret;
}

template <int IDX>
int deserialize_(BufferCtx *&ctx_, int64_t type_idx, const char *buf, const int64_t buf_len, int64_t &pos, ObIAllocator &allocator) {
  int ret = OB_SUCCESS;
  MDS_TG(10_ms);
  using ImplType = GET_CTX_TYPE_BY_TUPLE_IDX(IDX);
  if (BufferCtxBindingTypeId<ImplType>::value == type_idx) {
    ImplType *p_impl = nullptr;
    if (OB_ISNULL(p_impl = (ImplType *)allocator.alloc(sizeof(ImplType),
                                                       ObMemAttr("MDS_CTX_DESE",
                                                       ObCtxIds::MDS_CTX_ID)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      MDS_LOG(ERROR, "fail to alloc buffer ctx memory", KR(ret), K(type_idx), K(IDX));
    } else if (FALSE_IT(new (p_impl) ImplType())) {
    } else if (MDS_FAIL(p_impl->deserialize(buf, buf_len, pos))) {
      allocator.free(p_impl);
      p_impl = nullptr;
      MDS_LOG(ERROR, "deserialzed from buffer failed", KR(ret), K(type_idx), K(IDX));
    } else {
      ctx_ = p_impl;
      ctx_->set_binding_type_id(type_idx);
      MDS_LOG(INFO, "deserialize ctx success", KR(ret), K(*p_impl), K(type_idx), K(IDX), K(buf_len), K(pos), K(lbt()));
    }
  } else if (MDS_FAIL(deserialize_<IDX + 1>(ctx_, type_idx, buf, buf_len, pos, allocator))) {
    MDS_LOG(ERROR, "deserialzed from buffer failed", KR(ret), K(type_idx), K(IDX));
  }
  return ret;
}

template <>
int deserialize_<BufferCtxTupleHelper::get_element_size()>(BufferCtx *&ctx_,
                                                           int64_t type_idx,
                                                           const char *buf,
                                                           const int64_t buf_len,
                                                           int64_t &pos,
                                                           ObIAllocator &allocator)
{
  int ret = OB_ERR_UNEXPECTED;
  MDS_LOG(ERROR, "type idx out of tuple range", KR(ret), K(type_idx), K(BufferCtxTupleHelper::get_element_size()));
  return ret;
}

int BufferCtxNode::deserialize(const char *buf, const int64_t buf_len, int64_t &pos, ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  MDS_TG(10_ms);
  int64_t ctx_type_idx = INVALID_VALUE;
  if (MDS_FAIL(serialization::decode(buf, buf_len, pos, ctx_type_idx))) {
    MDS_LOG(ERROR, "fail to deserialize buffer ctx id", KR(ret), K(ctx_type_idx));
  } else if (INVALID_VALUE == ctx_type_idx) {
    MDS_LOG(DEBUG, "deserialized INVALD buffer ctx", KR(ret), K(ctx_type_idx), K(buf_len), K(pos));
  } else if (MDS_FAIL(deserialize_<0>(ctx_, ctx_type_idx, buf, buf_len, pos, allocator))) {
    MDS_LOG(WARN, "deserialized buffer ctx failed", KR(ret), K(ctx_type_idx));
  }
  return ret;
}

int64_t BufferCtxNode::get_serialize_size(void) const
{
  int64_t size = 0;
  if (OB_NOT_NULL(ctx_)) {
    size += serialization::encoded_length(ctx_->get_binding_type_id());
    size += ctx_->get_serialize_size();
  } else {
    size += serialization::encoded_length(INVALID_VALUE);
  }
  return size;
}

}
}
}
