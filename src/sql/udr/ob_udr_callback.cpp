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
 
#define USING_LOG_PREFIX SQL_QRR
#include "sql/udr/ob_udr_callback.h"

namespace oceanbase
{
namespace sql
{

int ObUDRAtomicOp::get_value(ObUDRItemMgr::UDRKeyNodePair *&rule_node)
{
  int ret = OB_SUCCESS;
  rule_node = nullptr;
  if (OB_ISNULL(rule_node_)) {
    ret = OB_NOT_INIT;
   LOG_WARN("invalid argument", K(rule_node_));
  } else if (OB_SUCC(lock(*rule_node_))) {
    rule_node = rule_node_;
  } else {
    if (NULL != rule_node_) {
      rule_node_->dec_ref_count();
      rule_node_ = NULL;
    }
  }
  return ret;
}

void ObUDRAtomicOp::operator()(RuleItemKV &entry)
{
  if (NULL != entry.second) {
    entry.second->inc_ref_count();
    rule_node_ = entry.second;
  }
}

ObUDRWlockAndRefGuard::~ObUDRWlockAndRefGuard()
{
  if (NULL != rule_node_) {
    rule_node_->unlock();
    rule_node_->dec_ref_count();
  }
}

ObUDRRlockAndRefGuard::~ObUDRRlockAndRefGuard()
{
  if (NULL != rule_node_) {
    rule_node_->unlock();
    rule_node_->dec_ref_count();
  }
}

} // namespace sql end
} // namespace oceanbase end
