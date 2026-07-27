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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_PACKAGE_MGR_H_
#define OCEANBASE_SHARE_SCHEMA_OB_PACKAGE_MGR_H_

#include "share/ob_define.h"
#include "lib/hash/ob_pointer_hashmap.h"
#include "lib/container/ob_vector.h"
#include "lib/allocator/page_arena.h"
#include "share/schema/ob_package_info.h"
namespace oceanbase
{
namespace share
{
namespace schema
{
struct ObPackageId
{
public:
  ObPackageId()
      : package_id_(common::OB_INVALID_ID)
  {}
  ObPackageId(uint64_t package_id)
      : package_id_(package_id)
  {}
  ObPackageId(const ObPackageId &other)
      : package_id_(other.package_id_)
  {}
  ObPackageId &operator =(const ObPackageId &other)
  {
    package_id_ = other.package_id_;
    return *this;
  }
  bool operator ==(const ObPackageId &rhs) const
  {
    return (package_id_ == rhs.package_id_);
  }
  bool operator !=(const ObPackageId &rhs) const
  {
    return !(*this == rhs);
  }
  bool operator <(const ObPackageId &rhs) const
  {
    return package_id_ < rhs.package_id_;
  }
  inline uint64_t hash() const
  {
    uint64_t hash_ret = 0;
    hash_ret = common::murmurhash(&package_id_, sizeof(package_id_), 0);
    return hash_ret;
  }

  bool is_valid() const
  {
    return (package_id_ != common::OB_INVALID_ID);
  }

  

  inline uint64_t get_package_id() const { return package_id_; }

  TO_STRING_KV(K_(package_id));
private:
  uint64_t package_id_;
};

class ObSimplePackageSchema : public ObSchema
{
public:
  ObSimplePackageSchema() : ObSchema() { reset(); }
  explicit ObSimplePackageSchema(common::ObIAllocator *allocator) : ObSchema(allocator) { reset(); }
  ObSimplePackageSchema(const ObSimplePackageSchema &other)
      : ObSchema()
  {
    reset();
    *this = other;
  }
  virtual ~ObSimplePackageSchema() {}
  ObSimplePackageSchema &operator =(const ObSimplePackageSchema &other);
  void reset()
  {
    
    database_id_ = common::OB_INVALID_ID;
    package_id_ = common::OB_INVALID_ID;
    reset_string(package_name_);
    schema_version_ = common::OB_INVALID_VERSION;
    type_ = INVALID_PACKAGE_TYPE;
    ObSchema::reset();
  }
  int64_t get_convert_size() const;
  bool is_valid() const
  {
    return (common::OB_INVALID_ID != database_id_ &&
            common::OB_INVALID_ID != package_id_ &&
            !package_name_.empty() &&
            schema_version_ >= 0);
  }
  
  
  inline void set_package_id(uint64_t package_id) { package_id_ = package_id; }
  inline uint64_t get_package_id() const { return package_id_; }
  inline void set_schema_version(const int64_t schema_version) { schema_version_ = schema_version; }
  inline int64_t get_schema_version() const { return schema_version_; }
  inline void set_database_id(const uint64_t database_id) { database_id_ = database_id; }
  inline uint64_t get_database_id() const { return database_id_; }
  inline int set_package_name(const common::ObString &package_name)
  { return deep_copy_str(package_name, package_name_); }
  inline const common::ObString &get_package_name() const { return package_name_; }
  ObPackageType get_type() const { return type_; }
  void set_type(ObPackageType type) { type_ = type; }
  TO_STRING_KV(
               K_(database_id),
               K_(package_id),
               K_(package_name),
               K_(type),
               K_(schema_version));
private:
  
  uint64_t database_id_;
  uint64_t package_id_;
  common::ObString package_name_;
  ObPackageType type_;
  int64_t schema_version_;
};

class ObPackageNameHashWrapper
{
public:
  ObPackageNameHashWrapper()
      : database_id_(common::OB_INVALID_ID),
        package_name_(),
        type_(INVALID_PACKAGE_TYPE) {}
  ObPackageNameHashWrapper(uint64_t database_id,
                             const common::ObString &package_name,
                             ObPackageType type)
      : database_id_(database_id),
        package_name_(package_name),
        type_(type) {}
  ~ObPackageNameHashWrapper() {}
  inline uint64_t hash() const;
  inline bool operator ==(const ObPackageNameHashWrapper &rv) const;
  
  inline void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  inline void set_package_name(const common::ObString &package_name) { package_name_ = package_name;}
  inline void set_type(ObPackageType type) { type_ = type; }
  
  inline uint64_t get_database_id() const { return database_id_; }
  inline const common::ObString &get_package_name() const { return package_name_; }
  inline ObPackageType get_type() const { return type_; }
  TO_STRING_KV(K_(database_id), K_(package_name), K_(type));
private:
  uint64_t database_id_;
  common::ObString package_name_;
  ObPackageType type_;
};

inline bool ObPackageNameHashWrapper::operator ==(const ObPackageNameHashWrapper &rv) const
{
  ObSchemaNameComparator name_cmp;
  return (database_id_ == rv.get_database_id())
      && (0 == name_cmp.compare(package_name_, rv.get_package_name()))
      && (type_ == rv.get_type());
}

inline uint64_t ObPackageNameHashWrapper::hash() const
{
  uint64_t hash_ret = 0;
  common::ObCollationType cs_type = common::CS_TYPE_UTF8MB4_GENERAL_CI;
  hash_ret = common::murmurhash(&database_id_, sizeof(uint64_t), 0);
  hash_ret = common::ObCharset::hash(cs_type, package_name_, hash_ret);
  hash_ret = common::murmurhash(&type_, sizeof(ObPackageType), hash_ret);
  return hash_ret;
}

template<class T, class V>
struct ObGetPackageKey
{
  void operator()(const T & t, const V &v) const
  {
    UNUSED(t);
    UNUSED(v);
  }
};

template<>
struct ObGetPackageKey<uint64_t, ObSimplePackageSchema *>
{
  uint64_t operator()(const ObSimplePackageSchema *package_schema) const
  {
    return NULL != package_schema ? package_schema->get_package_id() : common::OB_INVALID_ID;
  }
};

template<>
struct ObGetPackageKey<ObPackageNameHashWrapper, ObSimplePackageSchema *>
{
  ObPackageNameHashWrapper operator()(const ObSimplePackageSchema *package_schema) const
  {
    ObPackageNameHashWrapper name_wrap;
    if (package_schema != NULL) {
      
      name_wrap.set_database_id(package_schema->get_database_id());
      name_wrap.set_package_name(package_schema->get_package_name());
      name_wrap.set_type(package_schema->get_type());
    }
    return name_wrap;
  }
};

class ObPackageMgr
{
  typedef common::ObSortedVector<ObSimplePackageSchema *> PackageInfos;
  typedef common::hash::ObPointerHashMap<uint64_t, ObSimplePackageSchema *,
      ObGetPackageKey, 1024> PackageIdMap;
  typedef common::hash::ObPointerHashMap<ObPackageNameHashWrapper, ObSimplePackageSchema *,
      ObGetPackageKey, 1024> PackageNameMap;
  typedef PackageInfos::iterator PackageIter;
  typedef PackageInfos::const_iterator ConstPackageIter;
public:
  ObPackageMgr();
  explicit ObPackageMgr(common::ObIAllocator &allocator);
  virtual ~ObPackageMgr() {};
  int init();
  void reset();
  int assign(const ObPackageMgr &other);
  int deep_copy(const ObPackageMgr &other);
  void dump() const;
  int get_package_schema_count(int64_t &package_schema_count) const;
  int get_schema_statistics(ObSchemaStatisticsInfo &schema_info) const;

  int add_package(const ObSimplePackageSchema &package_schema);
  int del_package(const ObPackageId &package_id);
  int add_packages(const common::ObIArray<ObSimplePackageSchema> &package_schemas);
  int get_package_schema(uint64_t package_id, const ObSimplePackageSchema *&package_schema) const;
  int get_package_schema( uint64_t database_id,
                         const common::ObString &package_name, ObPackageType package_type,
                         const ObSimplePackageSchema *&package_schema) const;
  int get_package_schemas_in_runtime(common::ObIArray<const ObSimplePackageSchema *> &package_schemas) const;
  int get_package_schemas_in_database(uint64_t database_id,
                                      common::ObIArray<const ObSimplePackageSchema *> &package_schemas) const;
private:
  inline static bool compare_package(const ObSimplePackageSchema *lhs,
                                       const ObSimplePackageSchema *rhs);
  inline static bool equal_package(const ObSimplePackageSchema *lhs,
                                     const ObSimplePackageSchema *rhs);
  inline static bool compare_with_package_id(const ObSimplePackageSchema *lhs,
                                                    const ObPackageId &package_id);
  inline static bool equal_with_package_id(const ObSimplePackageSchema *lhs,
                                                  const ObPackageId &package_id);
private:
  common::ObArenaAllocator local_allocator_;
  common::ObIAllocator &allocator_;
  PackageInfos package_infos_;
  PackageIdMap package_id_map_;
  PackageNameMap package_name_map_;
  bool is_inited_;
};
}  // namespace schema
}  // namespace share
}  // namespace oceanbase
#endif /* OCEANBASE_SHARE_SCHEMA_OB_PACKAGE_MGR_H_ */
