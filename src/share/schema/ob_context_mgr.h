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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_MGR_H
#define OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_MGR_H

#include "share/ob_define.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_pointer_hashmap.h"
#include "lib/container/ob_vector.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObContextSchema;
class ObSchemaStatisticsInfo;
class ObContextKey;

class ObContextHashWrapper
{
public:
  ObContextHashWrapper()
    : ctx_namespace_() {}
  ObContextHashWrapper(const common::ObString &ctx_namespace)
    : ctx_namespace_(ctx_namespace) {}
  ~ObContextHashWrapper() {}
  inline uint64_t hash() const;
  inline bool operator==(const ObContextHashWrapper &rv) const;
  
  inline void set_context_namespace(const common::ObString &ctx_namespace) { ctx_namespace_ = ctx_namespace; }
  
  inline const common::ObString &get_context_namespace() const { return ctx_namespace_; }
  TO_STRING_KV(K_(ctx_namespace));

private:
  common::ObString ctx_namespace_;
};

inline bool ObContextHashWrapper::operator == (const ObContextHashWrapper &rv) const
{
  return (true)
      && (ctx_namespace_ == rv.get_context_namespace());
}

inline uint64_t ObContextHashWrapper::hash() const
{
  uint64_t hash_ret = 0;
  hash_ret = common::murmurhash(ctx_namespace_.ptr(), ctx_namespace_.length(), 0);
  return hash_ret;
}

template<class T, class V>
struct ObGetContextKey {
  void operator()(const T &t, const V &v) const {
    UNUSED(t);
    UNUSED(v);
  }
};

template<>
struct ObGetContextKey<ObContextHashWrapper, ObContextSchema *>
{
  ObContextHashWrapper operator() (const ObContextSchema * context) const;
};

class ObContextMgr
{
public:
  typedef common::ObSortedVector<ObContextSchema *> ContextInfos;
  typedef common::hash::ObPointerHashMap<ObContextHashWrapper, ObContextSchema *,
                                         ObGetContextKey, 128> ObContextMap;
  typedef ContextInfos::iterator ContextIter;
  typedef ContextInfos::const_iterator ConstContextIter;
  ObContextMgr();
  explicit ObContextMgr(common::ObIAllocator &allocator);
  virtual ~ObContextMgr();
  int init();
  void reset();
  int assign(const ObContextMgr &other);
  int deep_copy(const ObContextMgr &other);
  int get_context_schema_count(int64_t &context_schema_count) const;
  int get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const;
  int add_context(const ObContextSchema &context_schema);
  int add_contexts(const common::ObIArray<ObContextSchema> &context_schema);
  int del_context(const ObContextKey &context);

  int get_context_schema(uint64_t context_id,
                         const ObContextSchema *&context_schema) const;

  int get_context_schemas_in_tenant(common::ObIArray<const ObContextSchema *> &context_schemas) const;
  int get_context_schema_with_name(const common::ObString &ctx_namespace,
                                const ObContextSchema *&context_schema) const;

  template<typename Filter, typename Acation, typename EarlyStopCondition>
  int for_each(Filter &filter, Acation &action, EarlyStopCondition &condition);
  static bool compare_context(const ObContextSchema *lhs,
                                     const ObContextSchema *rhs);
  static bool equal_context(const ObContextSchema *lhs,
                                   const ObContextSchema *rhs);
  static int rebuild_context_hashmap(const ContextInfos &context_infos,
                                     ObContextMap &context_map);
private:
  inline static bool compare_with_context_key(const ObContextSchema *lhs,
                                              const ObContextKey &context_key);
  inline static bool equal_to_context_key(const ObContextSchema *lhs,
                                          const ObContextKey &context_key);
private:
  bool is_inited_;
  common::ObArenaAllocator local_allocator_;
  common::ObIAllocator &allocator_;
  ContextInfos context_infos_;
  ObContextMap context_map_;
};

} //end of schema
} //end of share
} //end of oceanbase
#endif //OCEANBASE_SHARE_SCHEMA_OB_CONTEXT_MGR_H
