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

#ifndef OCEANBASE_SHARE_CONFIG_OB_CONFIG_H_
#define OCEANBASE_SHARE_CONFIG_OB_CONFIG_H_

#include <pthread.h>
#include "lib/compress/ob_compressor_pool.h"
#include "lib/container/ob_array_serialization.h"
#include "common/json_type/ob_json_tree.h"
#include "share/config/ob_config_helper.h"
#include "share/ob_encryption_util.h"
#include "share/parameter/ob_parameter_attr.h"

namespace oceanbase
{

namespace rootserver
{
  class ObAdminSetConfig;
}

namespace common
{

enum ObConfigItemType{
  OB_CONF_ITEM_TYPE_UNKNOWN = -1,
  OB_CONF_ITEM_TYPE_BOOL = 0,
  OB_CONF_ITEM_TYPE_INT = 1,
  OB_CONF_ITEM_TYPE_DOUBLE = 2,
  OB_CONF_ITEM_TYPE_STRING = 3,
  OB_CONF_ITEM_TYPE_INTEGRAL = 4,
  OB_CONF_ITEM_TYPE_STRLIST = 5,
  OB_CONF_ITEM_TYPE_INTLIST = 6,
  OB_CONF_ITEM_TYPE_TIME = 7,
  OB_CONF_ITEM_TYPE_MOMENT = 8,
  OB_CONF_ITEM_TYPE_CAPACITY = 9,
  OB_CONF_ITEM_TYPE_VERSION = 11,
  OB_CONF_ITEM_TYPE_MODE = 12,
};

static const char *const DATA_TYPE_UNKNOWN = "UNKNOWN";
static const char *const DATA_TYPE_BOOL = "BOOL";
static const char *const DATA_TYPE_INT = "INT";
static const char *const DATA_TYPE_DOUBLE = "DOUBLE";
static const char *const DATA_TYPE_STRING = "STRING";
static const char *const DATA_TYPE_INTEGRAL = "INTEGRAL";
static const char *const DATA_TYPE_STRLIST = "STR_LIST";
static const char *const DATA_TYPE_INTLIST = "INT_LIST";
static const char *const DATA_TYPE_TIME = "TIME";
static const char *const DATA_TYPE_MOMENT = "MOMENT";
static const char *const DATA_TYPE_CAPACITY = "CAPACITY";
static const char *const DATA_TYPE_VERSION = "VERSION";
static const char *const DATA_TYPE_MODE = "MODE";

enum class ObConfigRangeOpts {
  OB_CONF_RANGE_NONE,
  OB_CONF_RANGE_GREATER_THAN,
  OB_CONF_RANGE_GREATER_EQUAL,
  OB_CONF_RANGE_LESS_THAN,
  OB_CONF_RANGE_LESS_EQUAL,
};

extern ObMemAttr g_config_mem_attr;
class ObBaseConfig;
class ObCommonConfig;
class ObSystemConfig;
class ObConfigItem
{
  friend class oceanbase::rootserver::ObAdminSetConfig;
  friend class ObBaseConfig;
  friend class ObCommonConfig;
  friend class ObSystemConfig;
  friend class ObServerConfig;
public:
  ObConfigItem();
  virtual ~ObConfigItem();

  void init(Scope::ScopeInfo scope_info,
            const char *name,
            const char *def,
            const char *info,
            const ObParameterAttr attr = ObParameterAttr());
  void add_checker(const ObConfigChecker *new_ck)
  {
    ck_ = OB_NEW(ObConfigConsChecker, g_config_mem_attr, ck_, new_ck);
  }
  virtual bool check() const
  {
    return NULL == ck_ ? value_valid_ : value_valid_ && ck_->check(*this);
  }
  virtual bool check_unit(const char *str) const
  {
    UNUSED(str);
    return true;
  }
  bool set_value(const common::ObString &string)
  {
#ifdef CONFIG_LOCK_EXEMPTION
    return set_value_unsafe(string);
#else
    return set_value_with_lock(string);
#endif
  }
  void set_name(const char *name)
  {
    name_str_ = name;
  }
  void set_info(const char *info)
  {
    info_str_ = info;;
  }
  void set_range(const char* range)
  {
    range_str_ = range;
  }
  const char *str() const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return value_ptr();
  }
  int case_compare(const char* str) const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return ObString::make_string(value_ptr()).case_compare(str);
  }
  const char *default_str() const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return value_default_ptr();
  }
  const char *name() const { return name_str_; }
  const char *info() const { return info_str_; }
  const char *range() const { return range_str_; }

  const char *section() const { return attr_.get_section(); }
  const char *scope() const { return attr_.get_scope(); }
  const char *source() const { return attr_.get_source(); }
  const char *edit_level() const { return attr_.get_edit_level(); }
  const char *data_type() const;

  bool invisible() const
  {
    return attr_.is_invisible();
  }

  bool is_not_editable() const
  {
    return attr_.is_readonly();
  }
  bool reboot_effective() const
  {
    return attr_.is_static();
  }
  virtual bool is_default(const char *value_str_,
                          const char *value_default_str_,
                          int64_t size) const;
  virtual bool operator >(const char *) const { return false; }
  virtual bool operator >=(const char *) const { return false; }
  virtual bool operator <(const char *) const { return false; }
  virtual bool operator <=(const char *) const { return false; }

  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_UNKNOWN;
  }
  virtual const char *optional_configuration_values() const { return nullptr; }
  int64_t version() const { return version_; }
protected:
  //use current value to do input operation
  virtual bool set(const char *str) = 0;
  virtual const char *value_ptr() const = 0;
  virtual const char *value_default_ptr() const = 0;
  virtual uint64_t value_len() const = 0;
  const ObConfigChecker *ck_;
  ObConfigUpdateCb *update_cb_;
  int64_t version_;
  bool inited_;
  bool value_valid_;
  const char* name_str_;
  const char* info_str_;
  const char* range_str_;
  common::ObLatch lock_;
private:
  // without lock, only used inner
  bool set_value_unsafe(const common::ObString &string);
  // without lock, only used inner
  bool set_value_with_lock(const common::ObString &string);
private:
  ObParameterAttr attr_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigItem);
};

class ObConfigIntListItem
  : public ObConfigItem
{
public:
  ObConfigIntListItem(ObConfigContainer *container,
                      Scope::ScopeInfo scope_info,
                      const char *name,
                      const char *def,
                      const char *info,
                      const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigIntListItem() {}

  //need reboot value need set it once startup, otherwise it will output current value
  const int64_t &operator[](int idx) const { return value_.int_list_[idx]; }
  int64_t &operator[](int idx) { return value_.int_list_[idx]; }
  ObConfigIntListItem &operator=(const char *str)
  {
    if (!set_value(str)) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig int list item set value failed");
    }
    return *this;
  }
  int size() const { return value_.size_; }
  bool valid() const { return value_.valid_; }
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_INTLIST;
  }

protected:
  //use current value to do input operation
  bool set(const char *str);
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }
  static const int64_t MAX_INDEX_SIZE = 64;
  struct ObInnerConfigIntListItem
  {
    ObInnerConfigIntListItem()
      : size_(0), valid_(false)
    {
      MEMSET(int_list_, 0, sizeof(int_list_));
    }
    ~ObInnerConfigIntListItem() {}

    int64_t int_list_[MAX_INDEX_SIZE];
    int size_;
    bool valid_;
  };

  struct ObInnerConfigIntListItem value_;
  static const uint64_t VALUE_BUF_SIZE = 32 * MAX_INDEX_SIZE;
  char value_str_[VALUE_BUF_SIZE];
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigIntListItem);
};

class ObConfigStrListItem
  : public ObConfigItem
{
public:
  ObConfigStrListItem();
  ObConfigStrListItem(ObConfigContainer *container,
                      Scope::ScopeInfo scope_info,
                      const char *name,
                      const char *def,
                      const char *info,
                      const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigStrListItem() {}

  int get(const int64_t idx, char *buf, const int64_t buf_len) const;
  int get_str_item_length(const int64_t idx, int64_t &length) const
  {
    int ret = OB_SUCCESS;
    if (0 <= idx && idx < size() - 1) {
      if (idx < size() - 2) {
        length = value_.idx_list_[idx + 1] - value_.idx_list_[idx];
      } else {
        length = STRLEN(value_.value_str_bk_) - value_.idx_list_[idx];
      }
    } else {
      ret = OB_ARRAY_OUT_OF_RANGE;
    }
    return ret;
  }

  ObConfigStrListItem &operator=(const char *str)
  {
    if (!set_value(str)) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig str list item set value failed");
    }
    return *this;
  }

  //need reboot value need set it once startup, otherwise it will output current value
  int64_t size() const { return value_.size_; }
  bool valid() const { return value_.valid_; }
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_STRLIST;
  }
public:
  static const int64_t MAX_INDEX_SIZE = 64;
  static const uint64_t VALUE_BUF_SIZE = 8192UL;
  struct ObInnerConfigStrListItem
  {
    ObInnerConfigStrListItem()
      : valid_(false), size_(0), rwlock_()
    {
      MEMSET(idx_list_, 0, sizeof(idx_list_));
      MEMSET(value_str_bk_, 0, sizeof(value_str_bk_));
    }
    ~ObInnerConfigStrListItem() {}

    ObInnerConfigStrListItem &operator = (const ObInnerConfigStrListItem &value)
    {
      if (this == &value) {
        //do nothing
      } else {
        ObLatchRGuard rd_guard(const_cast<ObLatch&>(value.rwlock_), ObLatchIds::CONFIG_LOCK);
        ObLatchWGuard wr_guard(rwlock_, ObLatchIds::CONFIG_LOCK);

        valid_ = value.valid_;
        size_ = value.size_;
        MEMCPY(idx_list_, value.idx_list_, sizeof(idx_list_));
        MEMCPY(value_str_bk_, value.value_str_bk_, sizeof(value_str_bk_));
      }
      return *this;
    }

    ObInnerConfigStrListItem(const ObInnerConfigStrListItem &value)
    {
      if (this == &value) {
        //do nothing
      } else {
        ObLatchRGuard rd_guard(const_cast<ObLatch&>(value.rwlock_), ObLatchIds::CONFIG_LOCK);
        ObLatchWGuard wr_guard(rwlock_, ObLatchIds::CONFIG_LOCK);

        valid_ = value.valid_;
        size_ = value.size_;
        MEMCPY(idx_list_, value.idx_list_, sizeof(idx_list_));
        MEMCPY(value_str_bk_, value.value_str_bk_, sizeof(value_str_bk_));
      }
    }

    bool valid_;
    int64_t size_;
    int64_t idx_list_[MAX_INDEX_SIZE];
    char value_str_bk_[VALUE_BUF_SIZE];
    ObLatch rwlock_;
  };

  struct ObInnerConfigStrListItem value_;

protected:
  //use current value to do input operation
  bool set(const char *str);
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }
  char value_str_[VALUE_BUF_SIZE];
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigStrListItem);
};

class ObConfigIntegralItem
  : public ObConfigItem
{
public:
  ObConfigIntegralItem()
    : value_(0), min_value_(0), max_value_(0),
      left_interval_opt_(ObConfigRangeOpts::OB_CONF_RANGE_NONE),
      right_interval_opt_(ObConfigRangeOpts::OB_CONF_RANGE_NONE)
  {
  }
  virtual ~ObConfigIntegralItem() {}

  bool operator >(const char *str) const
  { bool valid = true; return get_value() > parse(str, valid) && valid; }
  bool operator >=(const char *str) const
  { bool valid = true; return get_value() >= parse(str, valid) && valid; }
  bool operator <(const char *str) const
  { bool valid = true; return get_value() < parse(str, valid) && valid; }
  bool operator <=(const char *str) const
  { bool valid = true; return get_value() <= parse(str, valid) && valid; }

  // get_value() return the real-time value
  int64_t get_value() const { return value_; }
  // get() return the real-time value if it does not need reboot, otherwise it return initial_value
  int64_t get() const { return value_; }
  operator const int64_t &() const { return value_; }

  bool parse_range(const char *range);
  void init(Scope::ScopeInfo scope_info,
            const char *name,
            const char *def,
            const char *range,
            const char *info,
            const ObParameterAttr attr = ObParameterAttr());
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_INTEGRAL;
  }
  virtual bool check() const override;
protected:
  //use current value to do input operation
  virtual bool set(const char *str);
  virtual int64_t parse(const char *str, bool &valid) const = 0;

private:
  int64_t value_;
  int64_t min_value_;
  int64_t max_value_;
  ObConfigRangeOpts left_interval_opt_;
  ObConfigRangeOpts right_interval_opt_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigIntegralItem);
};
inline bool ObConfigIntegralItem::set(const char *str)
{
  bool valid = true;
  const int64_t value = parse(str, valid);
  if (valid) {
    value_ = value;
  }
  return valid;
}

class ObConfigDoubleItem
  : public ObConfigItem
{
public:
  ObConfigDoubleItem(ObConfigContainer *container,
                     Scope::ScopeInfo scope_info,
                     const char *name,
                     const char *def,
                     const char *range,
                     const char *info,
                     const ObParameterAttr attr = ObParameterAttr());
  ObConfigDoubleItem(ObConfigContainer *container,
                     Scope::ScopeInfo scope_info,
                     const char *name,
                     const char *def,
                     const char *info,
                     const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigDoubleItem() {}

  bool operator >(const char *str) const
  { bool valid = true; return get_value() > parse(str, valid) && valid; }
  bool operator >=(const char *str) const
  { bool valid = true; return get_value() >= parse(str, valid) && valid; }
  bool operator <(const char *str) const
  { bool valid = true; return get_value() < parse(str, valid) && valid; }
  bool operator <=(const char *str) const
  { bool valid = true; return get_value() <= parse(str, valid) && valid; }

  double get_value() const { return value_; }

  //need reboot value need set it once startup, otherwise it will output current value
  double get() const { return value_; }
  operator const double &() const { return value_; }

  ObConfigDoubleItem &operator = (double value);
  void init(Scope::ScopeInfo scope_info,
            const char *name,
            const char *def,
            const char *range,
            const char *info,
            const ObParameterAttr attr = ObParameterAttr());
  bool parse_range(const char *range);

  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_DOUBLE;
  }
  virtual bool check() const override;
protected:
  //use current value to do input operation
  bool set(const char *str);
  double parse(const char *str, bool &valid) const;
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 64UL;
  char value_str_[VALUE_BUF_SIZE];
private:
  double value_;
  double min_value_;
  double max_value_;
  ObConfigRangeOpts left_interval_opt_;
  ObConfigRangeOpts right_interval_opt_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigDoubleItem);
};
inline ObConfigDoubleItem &ObConfigDoubleItem::operator = (double value)
{
  char buf[2L<<10];
  (void) snprintf(buf, sizeof(buf), "%f", value);
  if (!set_value(buf)) {
    OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig double item set value failed");
  }
  return *this;
}
inline bool ObConfigDoubleItem::set(const char *str)
{
  bool valid = true;
  const double value = parse(str, valid);
  if (valid) {
    value_ = value;
  }
  return valid;
}


class ObConfigCapacityItem
  : public ObConfigIntegralItem
{
public:
  ObConfigCapacityItem(ObConfigContainer *container,
                       Scope::ScopeInfo scope_info,
                       const char *name,
                       const char *def,
                       const char *range,
                       const char *info,
                       const ObParameterAttr attr = ObParameterAttr());
  ObConfigCapacityItem(ObConfigContainer *container,
                       Scope::ScopeInfo scope_info,
                       const char *name,
                       const char *def,
                       const char *info,
                       const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigCapacityItem() {}

  ObConfigCapacityItem &operator = (int64_t value);
  virtual bool check_unit(const char *str) const
  {
    bool is_valid;
    IGNORE_RETURN ObConfigCapacityParser::get(str, is_valid);
    return is_valid;
  }

  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_CAPACITY;
  }
protected:
  int64_t parse(const char *str, bool &valid) const;
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 32UL;
  char value_str_[VALUE_BUF_SIZE];

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigCapacityItem);
};
inline ObConfigCapacityItem &ObConfigCapacityItem::operator = (int64_t value)
{
  char buf[2L<<10];
  (void) snprintf(buf, sizeof(buf), "%lldB", (long long)value);
  if (!set_value(buf)) {
    OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig capacity item set value failed");
  }
  return *this;
}

class ObConfigTimeItem
  : public ObConfigIntegralItem
{
public:
  ObConfigTimeItem(ObConfigContainer *container,
                   Scope::ScopeInfo scope_info,
                   const char *name,
                   const char *def,
                   const char *range,
                   const char *info,
                   const ObParameterAttr attr = ObParameterAttr());
  ObConfigTimeItem(ObConfigContainer *container,
                   Scope::ScopeInfo scope_info,
                   const char *name,
                   const char *def,
                   const char *info,
                   const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigTimeItem() {}
  ObConfigTimeItem &operator = (int64_t value);
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_TIME;
  }
protected:
  int64_t parse(const char *str, bool &valid) const;
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 32UL;
  char value_str_[VALUE_BUF_SIZE];

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigTimeItem);
};
inline ObConfigTimeItem &ObConfigTimeItem::operator = (int64_t value){
  char buf[2L<<10];
  (void) snprintf(buf, sizeof(buf), "%lldus", (long long)value);
  if (!set_value(buf)) {
    OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig time item set value failed");
  }
  return *this;
}

class ObConfigIntItem
  : public ObConfigIntegralItem
{
public:
  ObConfigIntItem(ObConfigContainer *container,
                  Scope::ScopeInfo scope_info,
                  const char *name,
                  const char *def,
                  const char *range,
                  const char *info,
                  const ObParameterAttr attr = ObParameterAttr());
  ObConfigIntItem(ObConfigContainer *container,
                  Scope::ScopeInfo scope_info,
                  const char *name,
                  const char *def,
                  const char *info,
                  const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigIntItem() {}
  ObConfigIntItem &operator = (int64_t value);
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_INT;
  }
protected:
  int64_t parse(const char *str, bool &valid) const;
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 32UL;
  char value_str_[VALUE_BUF_SIZE];

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigIntItem);
};
inline ObConfigIntItem &ObConfigIntItem::operator = (int64_t value)
{
  char buf[64];
  (void) snprintf(buf, sizeof(buf), "%lld", (long long)value);
  if (!set_value(buf)) {
    OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig int item set value failed");
  }
  return *this;
}

class ObConfigMomentItem
  : public ObConfigItem
{
public:
  ObConfigMomentItem(ObConfigContainer *container,
                     Scope::ScopeInfo scope_info,
                     const char *name,
                     const char *def,
                     const char *info,
                     const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigMomentItem() {}
  //use current value to do input operation
  bool set(const char *str);

  //need reboot value need set it once startup, otherwise it will output current value
  bool disable() const { return value_.disable_; }
  int hour() const { return value_.hour_; }
  int minute() const { return value_.minute_; }
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_MOMENT;
  }
  ObConfigMomentItem &operator=(const char *str)
  {
    if (!set_value(str)) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig moment item set value failed");
    }
    return *this;
  }
public:
  struct ObInnerConfigMomentItem
  {
    ObInnerConfigMomentItem() : disable_(true), hour_(-1), minute_(-1) {}
    ~ObInnerConfigMomentItem() {}

    bool disable_;
    int hour_;
    int minute_;
  };

protected:
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }
  static const uint64_t VALUE_BUF_SIZE = 64UL;
  char value_str_[VALUE_BUF_SIZE];

private:
  struct ObInnerConfigMomentItem value_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigMomentItem);
};

class ObConfigBoolItem
  : public ObConfigItem
{
public:
  ObConfigBoolItem(ObConfigContainer *container,
                   Scope::ScopeInfo scope_info,
                   const char *name,
                   const char *def,
                   const char *info,
                   const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigBoolItem() {}

  //need reboot value need set it once startup, otherwise it will output current value
  operator const bool &() const { return value_; }
  ObConfigBoolItem &operator = (const bool value) { set_value(value ? "True" : "False"); return *this; }
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_BOOL;
  }
protected:
  //use current value to do input operation
  bool set(const char *str);
  bool parse(const char *str, bool &valid) const;
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 8UL;
  char value_str_[VALUE_BUF_SIZE];
private:
  bool value_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigBoolItem);
};

class ObConfigStringItem : public ObConfigItem
{
public:
  ObConfigStringItem(ObConfigContainer *container,
                     Scope::ScopeInfo scope_info,
                     const char *name,
                     const char *def,
                     const char *info,
                     const ObParameterAttr attr = ObParameterAttr(),
                     const char *optional_values = nullptr);
  virtual ~ObConfigStringItem() {}

  //need reboot value need set it once startup, otherwise it will output current value
  operator const char *() const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return value_str_;
  } // not safe, value maybe changed
  const char *get_value() const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return value_str_;
  }
  ObString get_value_string() const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return ObString::make_string(value_str_);
  }
  int case_compare(const char *str) const
  {
    ObLatchRGuard rd_guard(const_cast<ObLatch&>(lock_), ObLatchIds::CONFIG_LOCK);
    return ObString::make_string(value_str_).case_compare(str);
  }
  int copy(char *buf, const int64_t buf_len); // '\0' will be added
  int deep_copy_value_string(ObIAllocator &allocator, ObString &dst);
  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_STRING;
  }
  ObConfigStringItem &operator=(const char *str)
  {
    if (!set_value(str)) {
      OB_LOG_RET(WARN, common::OB_ERR_UNEXPECTED, "obconfig string item set value failed");
    }
    return *this;
  }
  virtual const char *optional_configuration_values() const { return optional_values_; }
protected:
  //use current value to do input operation
  bool set(const char *str) { UNUSED(str); return true; }
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 8192UL;
  char value_str_[VALUE_BUF_SIZE];

private:
  const char *optional_values_;
  DISALLOW_COPY_AND_ASSIGN(ObConfigStringItem);
};

class ObConfigVersionItem
  : public ObConfigIntegralItem
{
public:
  ObConfigVersionItem(ObConfigContainer *container,
                       Scope::ScopeInfo scope_info,
                       const char *name,
                       const char *def,
                       const char *range,
                       const char *info,
                       const ObParameterAttr attr = ObParameterAttr());
  ObConfigVersionItem(ObConfigContainer *container,
                      Scope::ScopeInfo scope_info,
                      const char *name,
                      const char *def,
                      const char *info,
                      const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigVersionItem() {}

  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_VERSION;
  }
  ObConfigVersionItem &operator = (int64_t value);
protected:
  virtual bool set(const char *str) override;
  virtual int64_t parse(const char *str, bool &valid) const override;
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }

  static const uint64_t VALUE_BUF_SIZE = 32UL; // 32 is enough for version like 4.2.0.0
  char value_str_[VALUE_BUF_SIZE];

private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigVersionItem);
};


class ObConfigPairs
{
public:

struct ObConfigPair {
public:
  ObConfigPair()
    : key_(), value_()
  {}
  ~ObConfigPair() {}
  TO_STRING_KV(K_(key), K_(value));
public:
  ObString key_;
  ObString value_;
};

public:
  ObConfigPairs()
    : allocator_(),
      config_array_()
  {}
  ~ObConfigPairs() {}
  void init() {}
  bool is_valid() const
  {
    return true
           && config_array_.count() > 0;
  }
  void reset();
  int assign(const ObConfigPairs &other);

  
  const common::ObSArray<ObConfigPair> &get_configs() const { return config_array_; }
  int get_config_str(char *buf, const int64_t length) const;
  int64_t get_config_str_length() const;
  int add_config(const ObString &key, const ObString &value);

  TO_STRING_KV(K_(config_array));
private:
  ObArenaAllocator allocator_;
  common::ObSArray<ObConfigPair> config_array_;
};

class ObIConfigMode;
class ObConfigModeItem: public ObConfigItem
{
public:
  ObConfigModeItem(ObConfigContainer *container,
          Scope::ScopeInfo scope_info,
          const char *name,
          const char *def,
          ObConfigParser* parser,
          const char *info,
          const ObParameterAttr attr = ObParameterAttr());
  virtual ~ObConfigModeItem();
  // get_value() return the real-time value
  const uint8_t* get_value() const { return value_; }
  // get() return the real-time value if it does not need reboot, otherwise it return initial_value
  const uint8_t* get() const { return value_; }
  operator const uint8_t* () const { return value_; }

  virtual ObConfigItemType get_config_item_type() const {
    return ObConfigItemType::OB_CONF_ITEM_TYPE_MODE;
  }
  int init_mode(ObIConfigMode &mode);
  static const int64_t MAX_MODE_BYTES = 32;
protected:
  //use current value to do input operation
  bool set(const char *str);
  const char *value_ptr() const override
  {
    return value_str_;
  }
  uint64_t value_len() const override
  {
    return sizeof(value_str_);
  }
protected:
  static const uint64_t VALUE_BUF_SIZE = 8192UL;
  ObConfigParser *parser_;
  char value_str_[VALUE_BUF_SIZE];
  // max bits size: 8 * 32 = 256
  uint8_t value_[MAX_MODE_BYTES];
private:
  DISALLOW_COPY_AND_ASSIGN(ObConfigModeItem);
};

class ObIConfigMode
{
public:
  ObIConfigMode() {}
  ~ObIConfigMode() {}
  virtual int set_value(const ObConfigModeItem &mode_item) = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObIConfigMode);
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_SHARE_CONFIG_OB_CONFIG_H_
