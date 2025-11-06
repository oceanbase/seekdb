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
#include "ob_lob_rpc_struct.h"

namespace oceanbase
{
using namespace common;
using namespace share;
using namespace obrpc;
using namespace storage;

namespace obrpc
{

ObLobQueryBlock::ObLobQueryBlock()
  : size_(0)
{
}

void ObLobQueryBlock::reset()
{
  size_ = 0;
}

bool ObLobQueryBlock::is_valid() const
{
  return size_ > 0;
}

OB_SERIALIZE_MEMBER(ObLobQueryBlock, size_);


OB_DEF_SERIALIZE_SIZE(ObLobQueryArg)
{
  int64_t len = 0;
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ADD_LEN,
              tenant_id_,
              offset_,
              len_,
              cs_type_,
              scan_backward_,
              qtype_,
              lob_locator_,
              enable_remote_retry_);
  return len;
}

OB_DEF_SERIALIZE(ObLobQueryArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE,
              tenant_id_,
              offset_,
              len_,
              cs_type_,
              scan_backward_,
              qtype_,
              lob_locator_,
              enable_remote_retry_);
  return ret;
}

OB_DEF_DESERIALIZE(ObLobQueryArg)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE,
              tenant_id_,
              offset_,
              len_,
              cs_type_,
              scan_backward_,
              qtype_,
              lob_locator_,
              enable_remote_retry_);
  return ret;
}

ObLobQueryArg::ObLobQueryArg()
  : tenant_id_(0),
    offset_(0),
    len_(0),
    cs_type_(common::ObCollationType::CS_TYPE_INVALID),
    scan_backward_(false),
    qtype_(QueryType::READ),
    lob_locator_(),
    enable_remote_retry_(false)
{}

ObLobQueryArg::~ObLobQueryArg()
{
}


} // obrpc

} // oceanbase
