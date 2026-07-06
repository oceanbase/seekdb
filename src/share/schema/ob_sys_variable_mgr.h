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

#ifndef OB_SYS_VARIABLE_MGR_H_
#define OB_SYS_VARIABLE_MGR_H_

#include "share/ob_define.h"
#include "lib/hash/ob_hashmap.h"
#include "lib/hash/ob_pointer_hashmap.h"
#include "lib/container/ob_vector.h"
#include "lib/allocator/page_arena.h"
#include "share/schema/ob_schema_struct.h"
namespace oceanbase
{
namespace share
{
namespace schema
{

class ObSimpleSysVariableSchema : public ObSchema
{
public:
  ObSimpleSysVariableSchema();
  explicit ObSimpleSysVariableSchema(common::ObIAllocator *allocator);
  ObSimpleSysVariableSchema(const ObSimpleSysVariableSchema &src_schema);
  virtual ~ObSimpleSysVariableSchema();
  ObSimpleSysVariableSchema &operator =(const ObSimpleSysVariableSchema &other);
  TO_STRING_KV(
               K_(schema_version),
               K_(name_case_mode),
               K_(read_only));
  virtual void reset();
  bool is_valid() const;
  int64_t get_convert_size() const;
  
  
  inline void set_schema_version(const int64_t schema_version) { schema_version_ = schema_version; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline void set_name_case_mode(const common::ObNameCaseMode cmp_mode) { name_case_mode_ = cmp_mode; }
  inline common::ObNameCaseMode get_name_case_mode() const { return name_case_mode_; }
  inline void set_read_only(const bool read_only) { read_only_ = read_only; }
  inline bool get_read_only() const { return read_only_; }

private:
  
  int64_t schema_version_;
  common::ObNameCaseMode name_case_mode_;
  bool read_only_;
};

class ObSysVariableHashWrapper
{
public:
  ObSysVariableHashWrapper()
    {}
  ~ObSysVariableHashWrapper() {}
  inline uint64_t hash() const
  {
    uint64_t hash_ret = 0;
    return hash_ret;
  }
  inline bool operator==(const ObSysVariableHashWrapper &rv) const{
    return (true);
  }
  
  
  TO_STRING_EMPTY();

private:
};

template<class T, class V>
struct ObGetSysVariableKey {
  void operator()(const T &t, const V &v) const {
    UNUSED(t);
    UNUSED(v);
  }
};

template<>
struct ObGetSysVariableKey<ObSysVariableHashWrapper, ObSimpleSysVariableSchema *>
{
  ObSysVariableHashWrapper operator() (const ObSimpleSysVariableSchema *sys_variable) const {
    ObSysVariableHashWrapper hash_wrap;
    if (!OB_ISNULL(sys_variable)) {
      
    }
    return hash_wrap;
  }
};


class ObSysVariableMgr
{
public:
  typedef common::ObSortedVector<ObSimpleSysVariableSchema *> SysVariableInfos;
  typedef common::hash::ObPointerHashMap<ObSysVariableHashWrapper, ObSimpleSysVariableSchema *, ObGetSysVariableKey, 128> ObSysVariableMap;
  typedef SysVariableInfos::iterator SysVariableIter;
  typedef SysVariableInfos::const_iterator ConstSysVariableIter;
  ObSysVariableMgr();
  explicit ObSysVariableMgr(common::ObIAllocator &allocator);
  virtual ~ObSysVariableMgr();
  int init();
  void reset();
  int assign(const ObSysVariableMgr &other);
  int deep_copy(const ObSysVariableMgr &other);
  void dump() const;
  int get_sys_variable_schema_count(int64_t &sys_variable_count) const;
  int get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const;
  int add_sys_variable(const ObSimpleSysVariableSchema &sys_variable);
  int add_sys_variables(const common::ObIArray<ObSimpleSysVariableSchema> &sys_variable_schemas);
  int del_sys_variable();
  int del_sys_variable(const uint64_t) { return del_sys_variable(); }

  int get_sys_variable_schema(
                              const ObSimpleSysVariableSchema *&sys_variable) const;
  inline static bool compare_sys_variable(const ObSimpleSysVariableSchema *lhs,
                                          const ObSimpleSysVariableSchema *rhs) {
    return false;
  }
  inline static bool equal_sys_variable(const ObSimpleSysVariableSchema *lhs,
                                        const ObSimpleSysVariableSchema *rhs) {
    return true;
  }
  static int rebuild_sys_variable_hashmap(const SysVariableInfos &sys_var_infos,
                                          ObSysVariableMap &sys_var_map);
private:
private:
  bool is_inited_;
  common::ObArenaAllocator local_allocator_;
  common::ObIAllocator &allocator_;
  SysVariableInfos sys_variable_infos_;
  ObSysVariableMap sys_variable_map_;
};

} //end of schema
} //end of share
} //end of oceanbase
#endif
