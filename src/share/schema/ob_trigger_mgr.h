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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_TRIGGER_MGR_H_
#define OCEANBASE_SHARE_SCHEMA_OB_TRIGGER_MGR_H_

#include "share/ob_define.h"
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

struct ObTriggerId
{
public:
  ObTriggerId()
    : trigger_id_(common::OB_INVALID_ID)
  {}
  ObTriggerId(uint64_t trigger_id)
    : trigger_id_(trigger_id)
  {}
  ObTriggerId(const ObTriggerId &other)
    : trigger_id_(other.trigger_id_)
  {}
  ObTriggerId &operator =(const ObTriggerId &other)
  {
    trigger_id_ = other.trigger_id_;
    return *this;
  }
  bool operator ==(const ObTriggerId &rhs) const
  { return (trigger_id_ == rhs.trigger_id_); }
  bool operator !=(const ObTriggerId &rhs) const
  { return !(*this == rhs); }
  bool operator <(const ObTriggerId &rhs) const
  {
    return trigger_id_ < rhs.trigger_id_;
  }
  inline uint64_t hash() const
  {
    uint64_t hash_ret = 0;
    hash_ret = common::murmurhash(&trigger_id_, sizeof(trigger_id_), 0);
    return hash_ret;
  }
  bool is_valid() const
  { return (trigger_id_ != common::OB_INVALID_ID); }
  
  inline uint64_t get_trigger_id() const { return trigger_id_; }
  TO_STRING_KV(K_(trigger_id));
private:
  uint64_t trigger_id_;
};

class ObSimpleTriggerSchema : public ObSchema, public IObErrorInfo
{
  OB_UNIS_VERSION(1);
public:
  ObSimpleTriggerSchema()
    : ObSchema()
  { reset(); }
  explicit ObSimpleTriggerSchema(common::ObIAllocator *allocator) : ObSchema(allocator) { reset(); }
  ObSimpleTriggerSchema(const ObSimpleTriggerSchema &other)
    : ObSchema()
  {
    reset();
    *this = other;
  }
  virtual ~ObSimpleTriggerSchema() {}
  ObSimpleTriggerSchema &operator =(const ObSimpleTriggerSchema &other);
  virtual void reset()
  {
    
    trigger_id_ = common::OB_INVALID_ID;
    database_id_ = common::OB_INVALID_ID;
    schema_version_ = common::OB_INVALID_VERSION;
    reset_string(trigger_name_);
    ObSchema::reset();
  }
  virtual int64_t get_convert_size() const;
  virtual bool is_valid() const
  {
    return (ObSchema::is_valid() &&
            common::OB_INVALID_ID != trigger_id_ &&
            common::OB_INVALID_ID != database_id_ &&
            !trigger_name_.empty() &&
            schema_version_ >= 0);
  }
  virtual int deep_copy(const ObSimpleTriggerSchema &other);

  
  virtual void set_trigger_id(uint64_t trigger_id) { trigger_id_ = trigger_id; }
  virtual void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  virtual void set_schema_version(int64_t schema_version) { schema_version_ = schema_version; }
  virtual int set_trigger_name(const common::ObString &trigger_name)
  { return deep_copy_str(trigger_name, trigger_name_); }

  
  inline uint64_t get_trigger_id() const { return trigger_id_; }
  inline uint64_t get_database_id() const { return database_id_; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline const common::ObString &get_trigger_name() const { return trigger_name_; }
  // TODO(jiuren):
  // consider if the database is in recyclebin after trigger support is enabled.
  inline bool is_in_recyclebin() const
  { return common::OB_RECYCLEBIN_SCHEMA_ID == database_id_; }
  
  inline uint64_t get_object_id() const { return get_trigger_id(); }
  inline ObObjectType get_object_type() const { return ObObjectType::TRIGGER; }

  TO_STRING_KV(
               K_(trigger_id),
               K_(database_id),
               K_(schema_version),
               K_(trigger_name));
protected:
  
  uint64_t trigger_id_;
  uint64_t database_id_;
  int64_t schema_version_;
  common::ObString trigger_name_;
};

class ObTriggerNameHashWrapper
{
public:
  ObTriggerNameHashWrapper()
    : database_id_(common::OB_INVALID_ID),
      trigger_name_()
  {}
  ObTriggerNameHashWrapper(uint64_t database_id,
                           const common::ObString &trigger_name)
    : database_id_(database_id),
      trigger_name_(trigger_name)
  {}
  ~ObTriggerNameHashWrapper() {}
  inline uint64_t hash() const;
  inline bool operator ==(const ObTriggerNameHashWrapper &rv) const;
  
  inline void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  inline void set_trigger_name(const common::ObString &trigger_name) { trigger_name_ = trigger_name;}
  
  inline uint64_t get_database_id() const { return database_id_; }
  inline const common::ObString &get_trigger_name() const { return trigger_name_; }
  TO_STRING_KV(K_(database_id), K_(trigger_name));
private:
  uint64_t database_id_;
  common::ObString trigger_name_;
};

inline bool ObTriggerNameHashWrapper::operator ==(const ObTriggerNameHashWrapper &rv) const
{
  ObSchemaNameComparator name_cmp;
  return (database_id_ == rv.get_database_id())
      && (0 == name_cmp.compare(trigger_name_, rv.get_trigger_name()));
}

inline uint64_t ObTriggerNameHashWrapper::hash() const
{
  uint64_t hash_ret = 0;
  common::ObCollationType cs_type = common::CS_TYPE_UTF8MB4_GENERAL_CI;
  hash_ret = common::murmurhash(&database_id_, sizeof(uint64_t), 0);
  hash_ret = common::ObCharset::hash(cs_type, trigger_name_, hash_ret);
  return hash_ret;
}

template<class T, class V>
struct ObGetTriggerKey
{
  void operator()(const T & t, const V &v) const
  {
    UNUSED(t);
    UNUSED(v);
  }
};

template<>
struct ObGetTriggerKey<uint64_t, ObSimpleTriggerSchema *>
{
  uint64_t operator()(const ObSimpleTriggerSchema *trigger_schema) const
  {
    return NULL != trigger_schema ? trigger_schema->get_trigger_id() : common::OB_INVALID_ID;
  }
};

template<>
struct ObGetTriggerKey<ObTriggerNameHashWrapper, ObSimpleTriggerSchema *>
{
  ObTriggerNameHashWrapper operator()(const ObSimpleTriggerSchema *trigger_schema) const
  {
    ObTriggerNameHashWrapper name_wrap;
    if (trigger_schema != NULL) {
      
      name_wrap.set_database_id(trigger_schema->get_database_id());
      name_wrap.set_trigger_name(trigger_schema->get_trigger_name());
    }
    return name_wrap;
  }
};

class ObTriggerMgr
{
  typedef common::ObSortedVector<ObSimpleTriggerSchema *> TriggerInfos;
  typedef common::hash::ObPointerHashMap<uint64_t, ObSimpleTriggerSchema *,
      ObGetTriggerKey, 128> TriggerIdMap;
  typedef common::hash::ObPointerHashMap<ObTriggerNameHashWrapper, ObSimpleTriggerSchema *,
      ObGetTriggerKey, 128> TriggerNameMap;
  typedef TriggerInfos::iterator TriggerIter;
  typedef TriggerInfos::const_iterator ConstTriggerIter;
public:
  ObTriggerMgr();
  explicit ObTriggerMgr(common::ObIAllocator &allocator);
  virtual ~ObTriggerMgr() {};
  int init();
  void reset();
  int assign(const ObTriggerMgr &other);
  int deep_copy(const ObTriggerMgr &other);
  void dump() const;
  int get_trigger_schema_count(int64_t &trigger_schema_count) const;
  int get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const;

  int add_trigger(const ObSimpleTriggerSchema &trigger_schema);
  int del_trigger(const ObTriggerId &trigger_id);
  int add_triggers(const common::ObIArray<ObSimpleTriggerSchema> &trigger_schemas);
  int get_trigger_schema(uint64_t trigger_id, const ObSimpleTriggerSchema *&trigger_schema) const;
  int get_trigger_schema( uint64_t database_id,
                         const common::ObString &trigger_name,
                         const ObSimpleTriggerSchema *&trigger_schema) const;
  int get_trigger_schemas_in_runtime(common::ObIArray<const ObSimpleTriggerSchema *> &trigger_schemas) const;
  int get_trigger_schemas_in_database(uint64_t database_id,
                                      common::ObIArray<const ObSimpleTriggerSchema *> &trigger_schemas) const;
  int try_rebuild_trigger_hashmap();
private:
  inline static bool compare_trigger(const ObSimpleTriggerSchema *lhs,
                                       const ObSimpleTriggerSchema *rhs);
  inline static bool equal_trigger(const ObSimpleTriggerSchema *lhs,
                                     const ObSimpleTriggerSchema *rhs);
  inline static bool compare_with_trigger_id(const ObSimpleTriggerSchema *lhs,
                                                    const ObTriggerId &trigger_id);
  inline static bool equal_with_trigger_id(const ObSimpleTriggerSchema *lhs,
                                                  const ObTriggerId &trigger_id);
private:
  common::ObArenaAllocator local_allocator_;
  common::ObIAllocator &allocator_;
  TriggerInfos trigger_infos_;
  TriggerIdMap trigger_id_map_;
  TriggerNameMap trigger_name_map_;
  bool is_inited_;
};

}  // namespace schema
}  // namespace share
}  // namespace oceanbase

#endif /* OCEANBASE_SHARE_SCHEMA_OB_TRIGGER_MGR_H_ */
