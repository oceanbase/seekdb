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
 

#ifndef OB_SQL_UDR_OB_UDR_CALLBACK_H_
#define OB_SQL_UDR_OB_UDR_CALLBACK_H_

#include "sql/udr/ob_udr_item_mgr.h"

namespace oceanbase
{
namespace sql
{

class ObUDRAtomicOp
{
protected:
  typedef common::hash::HashMapPair<ObUDRItemMgr::UDRKey, ObUDRItemMgr::UDRKeyNodePair*> RuleItemKV;

public:
  ObUDRAtomicOp()
    : rule_node_(NULL)
  {
  }
  virtual ~ObUDRAtomicOp() {}
  virtual int get_value(ObUDRItemMgr::UDRKeyNodePair *&rule_node);
  // get rule node and increase reference count
  void operator()(RuleItemKV &entry);

protected:
  // when get value, need lock
  virtual int lock(ObUDRItemMgr::UDRKeyNodePair &rule_node) = 0;
protected:
  ObUDRItemMgr::UDRKeyNodePair *rule_node_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObUDRAtomicOp);
};

class ObUDRWlockAndRefGuard : public ObUDRAtomicOp
{
public:
  ObUDRWlockAndRefGuard() : ObUDRAtomicOp()
  {
  }
  virtual ~ObUDRWlockAndRefGuard();
  int lock(ObUDRItemMgr::UDRKeyNodePair &rule_node)
  {
    return rule_node.lock(false/*wlock*/);
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObUDRWlockAndRefGuard);
};

class ObUDRRlockAndRefGuard : public ObUDRAtomicOp
{
public:
  ObUDRRlockAndRefGuard() : ObUDRAtomicOp()
  {
  }
  virtual ~ObUDRRlockAndRefGuard();
  int lock(ObUDRItemMgr::UDRKeyNodePair &rule_node)
  {
    return rule_node.lock(true/*rlock*/);
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObUDRRlockAndRefGuard);
};

}
}
#endif
