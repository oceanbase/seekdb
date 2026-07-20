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

#ifndef OCEANBASE_SHARE_SCHEMA_OB_PACKAGE_INFO_H_
#define OCEANBASE_SHARE_SCHEMA_OB_PACKAGE_INFO_H_

#include "share/schema/ob_schema_struct.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
enum ObPackageType
{
  INVALID_PACKAGE_TYPE = 0,
  PACKAGE_TYPE = 1,
  PACKAGE_BODY_TYPE = 2,
};
enum ObPackageFlag
{
  PKG_FLAG_INVALID = 1,
  PKG_FLAG_NONEDITIONABLE = 2,
  PKG_FLAG_INVOKER_RIGHT = 4,
  PKG_FLAG_ACCESSIBLE_BY = 8,
};

class ObPackageInfo: public ObSchema, public IObErrorInfo
{
  OB_UNIS_VERSION(1);
public:
  ObPackageInfo() { reset(); }

  explicit ObPackageInfo(common::ObIAllocator *allocator)
  : ObSchema(allocator)
  {
    reset();
  }

  DISABLE_COPY_ASSIGN(ObPackageInfo);

  virtual ~ObPackageInfo() {}
  int assign(const ObPackageInfo &other);
  bool is_valid() const;
  void reset();
  int64_t get_convert_size() const;
  
  
  uint64_t get_database_id() const { return database_id_; }
  void set_database_id(uint64_t database_id) { database_id_ = database_id; }
  uint64_t get_package_id() const { return package_id_; }
  uint64_t get_object_id() const { return package_id_; }
  ObObjectType get_object_type() const { return is_package() ? ObObjectType::PACKAGE : ObObjectType::PACKAGE_BODY; }
  void set_package_id(uint64_t package_id) { package_id_ = package_id; }
  const common::ObString &get_package_name() const { return package_name_; }
  int set_package_name(const common::ObString &package_name) { return deep_copy_str(package_name, package_name_); }
  void assign_package_name(const common::ObString &package_name) { package_name_ = package_name; }
  void set_schema_version(int64_t schema_version) { schema_version_ = schema_version; }
  int64_t get_schema_version() const { return schema_version_; }
  ObPackageType get_type() const { return type_; }
  void set_type(ObPackageType type) { type_ = type; }
  inline bool is_package() const { return type_ ==  PACKAGE_TYPE; }
  inline bool is_package_body() const { return type_ == PACKAGE_BODY_TYPE; }
  int64_t get_flag() const { return flag_; }
  void set_flag(int64_t flag) { flag_ = flag; }
  uint64_t get_owner_id() const { return owner_id_; }
  void set_owner_id(int64_t owner_id) { owner_id_ = owner_id; }
  const common::ObString &get_source() const { return source_; }
  int set_source(const common::ObString &source) { return deep_copy_str(source, source_); }
  void assign_source(const common::ObString &source) { source_ = source; }
  OB_INLINE int set_exec_env(const common::ObString &exec_env) { return deep_copy_str(exec_env, exec_env_); }
  OB_INLINE int set_comment(const common::ObString &comment) { return deep_copy_str(comment, comment_); }
  OB_INLINE const common::ObString &get_route_sql() const { return route_sql_; }
  OB_INLINE int set_route_sql(const common::ObString &route_sql) { return deep_copy_str(route_sql, route_sql_); }
  OB_INLINE const common::ObString &get_exec_env() const { return exec_env_; }
  OB_INLINE const common::ObString &get_comment() const { return comment_; }
  bool is_for_trigger() const;

  OB_INLINE void set_pkg_invalid() { flag_ |= PKG_FLAG_INVALID; }
  OB_INLINE void set_noneditionable() { flag_ |= PKG_FLAG_NONEDITIONABLE; }
  OB_INLINE void set_invoker_right() { flag_ |= PKG_FLAG_INVOKER_RIGHT; }
  OB_INLINE void set_accessible_by_clause() { flag_ |= PKG_FLAG_ACCESSIBLE_BY; }

  OB_INLINE bool is_pkg_invalid() { return PKG_FLAG_INVALID == (flag_ & PKG_FLAG_INVALID); }
  OB_INLINE bool is_noneditionable() const
  {
    return PKG_FLAG_NONEDITIONABLE == (flag_ & PKG_FLAG_NONEDITIONABLE);
  }
  OB_INLINE bool is_invoker_right() const
  {
    return PKG_FLAG_INVOKER_RIGHT == (flag_ & PKG_FLAG_INVOKER_RIGHT);
  }
  OB_INLINE bool has_accessible_by_clause() const
  {
    return PKG_FLAG_ACCESSIBLE_BY == (flag_ & PKG_FLAG_ACCESSIBLE_BY);
  }

  TO_STRING_KV(K_(database_id),
               K_(owner_id),
               K_(package_id),
               K_(package_name),
               K_(schema_version),
               K_(type),
               K_(flag),
               K_(exec_env),
               K_(source),
               K_(comment),
               K_(route_sql));
private:
  uint64_t database_id_;
  uint64_t owner_id_;
  uint64_t package_id_;
  common::ObString package_name_;
  int64_t schema_version_;
  ObPackageType type_;
  int64_t flag_;
  common::ObString exec_env_;
  common::ObString source_;
  common::ObString comment_;
  common::ObString route_sql_;
};

}  //schema
}  //share
}  //oceanbase
#endif /* OCEANBASE_SHARE_SCHEMA_OB_PACKAGE_INFO_H_ */
