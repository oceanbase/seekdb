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

#ifndef OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_CACHE_CALLBACK_
#define OCEANBASE_SQL_PLAN_CACHE_OB_PLAN_CACHE_CALLBACK_

#include "ob_pcv_set.h"

namespace oceanbase
{
namespace sql
{

class ObLibCacheAtomicOp
{
protected:
  typedef common::hash::HashMapPair<ObILibCacheKey*, ObILibCacheNode *> LibCacheKV;

public:
  ObLibCacheAtomicOp()
    : cache_node_(NULL)
  {
  }
  virtual ~ObLibCacheAtomicOp() {}
  // get cache node and lock
  virtual int get_value(ObILibCacheNode *&cache_node);
  // get cache node and increase reference count
  void operator()(LibCacheKV &entry);

protected:
  // when get value, need lock
  virtual int lock(ObILibCacheNode &cache_node) = 0;
protected:
  // According to the interface of ObHashTable, all returned values will be passed
  // back to the caller via the callback functor.
  // cache_node_ - the plan cache value that is referenced.
  ObILibCacheNode *cache_node_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObLibCacheAtomicOp);
};

class ObLibCacheWlockAndRef : public ObLibCacheAtomicOp
{
public:
  ObLibCacheWlockAndRef()
    : ObLibCacheAtomicOp()
  {
  }
  virtual ~ObLibCacheWlockAndRef() {}
  int lock(ObILibCacheNode &cache_node)
  {
    return cache_node.lock(false/*wlock*/);
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObLibCacheWlockAndRef);
};

class ObLibCacheRlockAndRef : public ObLibCacheAtomicOp
{
public:
  ObLibCacheRlockAndRef()
    : ObLibCacheAtomicOp()
  {
  }
  virtual ~ObLibCacheRlockAndRef() {}
  int lock(ObILibCacheNode &cache_node)
  {
    return cache_node.lock(true/*rlock*/);
  };
private:
  DISALLOW_COPY_AND_ASSIGN(ObLibCacheRlockAndRef);
};

class ObCacheObjAtomicOp
{
protected:
  typedef common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> ObjKV;

public:
  ObCacheObjAtomicOp(): cache_obj_(NULL) {}
  virtual ~ObCacheObjAtomicOp() {}
  // get lock and increase reference count
  void operator()(ObjKV &entry);

  ObILibCacheObject *get_value() const { return cache_obj_; }

protected:
  ObILibCacheObject *cache_obj_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObCacheObjAtomicOp);
};

}
}
#endif // _OB_PLAN_CACHE_CALLBACK_H
