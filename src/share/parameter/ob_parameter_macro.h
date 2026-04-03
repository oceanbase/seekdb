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

#ifndef OCEANBASE_SHARE_PARAMETER_OB_PARAMETER_MACRO_H_
#define OCEANBASE_SHARE_PARAMETER_OB_PARAMETER_MACRO_H_


////////////////////////////////////////////////////////////////////////////////
// SCOPE macro to support cluster or tenant parameter
////////////////////////////////////////////////////////////////////////////////
#define _OB_CLUSTER_PARAMETER common::Scope::CLUSTER
#define _OB_TENANT_PARAMETER common::Scope::TENANT

// Updated macros with name as first parameter: name, SCOPE, ...
#define _DEF_PARAMETER_SCOPE_EASY(param, name, SCOPE, ...)                        \
  SCOPE(_DEF_PARAMETER_EASY(param, _ ## SCOPE, name, __VA_ARGS__))
#define _DEF_PARAMETER_SCOPE_RANGE_EASY(param, name, SCOPE, ...)                  \
  SCOPE(_DEF_PARAMETER_RANGE_EASY(param, _ ## SCOPE, name, __VA_ARGS__))
#define _DEF_PARAMETER_SCOPE_CHECKER_EASY(param, name, SCOPE, ...)                \
  SCOPE(_DEF_PARAMETER_CHECKER_EASY(param, _ ## SCOPE, name, __VA_ARGS__))
#define _DEF_PARAMETER_SCOPE_PARSER_EASY(param, name, SCOPE, ...)                 \
  SCOPE(_DEF_PARAMETER_PARSER_EASY(param, _ ## SCOPE, name, __VA_ARGS__))
#define _DEF_PARAMETER_SCOPE_IP_EASY(param, name, SCOPE, def, ...)                \
  SCOPE(_DEF_PARAMETER_CHECKER_EASY(param, _ ## SCOPE, name, def,                     \
                                    common::ObConfigIpChecker, __VA_ARGS__))
#define _DEF_PARAMETER_SCOPE_LOG_LEVEL_EASY(param, name, SCOPE, def, ...)         \
  SCOPE(_DEF_PARAMETER_CHECKER_EASY(param, _ ## SCOPE, name, def,                     \
                                    common::ObConfigLogLevelChecker, __VA_ARGS__))

#define _DEF_PARAMETER_SCOPE_WORK_AREA_POLICY_EASY(param, name, SCOPE, def, ...)  \
  SCOPE(_DEF_PARAMETER_CHECKER_EASY(param, _ ## SCOPE, name, def,                     \
                                    common::ObConfigWorkAreaPolicyChecker, __VA_ARGS__))

// TODO: use parameter instead of config
#define _DEF_PARAMETER_EASY(param, scope, name, def, args...)                 \
public:                                                                          \
  class ObConfig ## param ## Item ## _ ## name                                 \
      : public common::ObConfig ## param ## Item                               \
  {                                                                            \
  public:                                                                      \
    ObConfig ## param ## Item ## _ ## name()                                   \
        : common::ObConfig ## param ## Item(local_container(), scope, #name,   \
          def, args) {}                                                        \
    template <class T>                                                         \
    ObConfig ## param ## Item ## _ ## name& operator=(T value)                 \
    {                                                                          \
      common::ObConfig ## param ## Item::operator=(value);                     \
      return *this;                                                            \
    }                                                                          \
    TO_STRING_KV(K_(value_str))                                                \
  protected:                                                                   \
    const char *value_default_ptr() const override                             \
    {                                                                          \
      return value_default_str_;                                               \
    }                                                                          \
    static constexpr const char* value_default_str_ = def;                     \
  } name;

#define _DEF_PARAMETER_RANGE_EASY(param, scope, name, def, args...)           \
public:                                                                          \
  class ObConfig ## param ## Item ## _ ## name                                 \
      : public common::ObConfig ## param ## Item                               \
  {                                                                            \
  public:                                                                      \
    ObConfig ## param ## Item ## _ ## name()                                   \
        : common::ObConfig ## param ## Item(local_container(), scope,          \
          #name, def, args) {}                                                 \
    template <class T>                                                         \
    ObConfig ## param ## Item ## _ ## name& operator=(T value)                 \
    {                                                                          \
      common::ObConfig ## param ## Item::operator=(value);                     \
      return *this;                                                            \
    }                                                                          \
  protected:                                                                   \
    const char *value_default_ptr() const override                             \
    {                                                                          \
      return value_default_str_;                                               \
    }                                                                          \
    static constexpr const char* value_default_str_ = def;                     \
  } name;

#define _DEF_PARAMETER_CHECKER_EASY(param, scope, name, def, checker, args...) \
public:                                                                          \
  class ObConfig ## param ## Item ## _ ## name                                 \
      : public common::ObConfig ## param ## Item                               \
  {                                                                            \
   public:                                                                     \
    ObConfig ## param ## Item ## _ ## name()                                   \
        : common::ObConfig ## param ## Item(                                   \
            local_container(), scope, #name, def, args)                        \
    {                                                                          \
      add_checker(OB_NEW(checker, g_config_mem_attr));                         \
    }                                                                          \
    template <class T>                                                         \
    ObConfig ## param ## Item ## _ ## name& operator=(T value)                 \
    {                                                                          \
      common::ObConfig ## param ## Item::operator=(value);                     \
      return *this;                                                            \
    }                                                                          \
  protected:                                                                   \
    const char *value_default_ptr() const override                             \
    {                                                                          \
      return value_default_str_;                                               \
    }                                                                          \
    static constexpr const char* value_default_str_ = def;                     \
  } name;
#define _DEF_PARAMETER_PARSER_EASY(param, scope, name, def, parser, args...)   \
public:                                                                          \
  class ObConfig ## param ## Item ## _ ## name                                 \
      : public common::ObConfig ## param ## Item                               \
  {                                                                            \
   public:                                                                     \
    ObConfig ## param ## Item ## _ ## name()                                   \
        : common::ObConfig ## param ## Item(                                   \
            local_container(), scope, #name, def, 							               \
            new (std::nothrow) parser(), args) {}                                    \
  protected:                                                                   \
    const char *value_default_ptr() const override                             \
    {                                                                          \
      return value_default_str_;                                               \
    }                                                                          \
    static constexpr const char* value_default_str_ = def;                     \
  } name;

////////////////////////////////////////////////////////////////////////////////
// Unified parameter definition macro: DEF_PARAM(name, type, SCOPE, ...)
// name: parameter name (first parameter)
// type: parameter type (STR, INT, CAP, TIME, BOOL, etc.)
// SCOPE: OB_CLUSTER_PARAMETER or OB_TENANT_PARAMETER
// ...: remaining arguments (def, range/checker, description, attr, optional_values)
#define DEF_PARAM(name, type, SCOPE, ...)                                      \
  _DEF_PARAM_IMPL(name, type, SCOPE, __VA_ARGS__)

// Internal implementation macro that dispatches based on type
#define _DEF_PARAM_IMPL(name, type, SCOPE, ...)                               \
  _DEF_PARAM_DISPATCH(name, type, SCOPE, __VA_ARGS__)

// Dispatch macro based on type
#define _DEF_PARAM_DISPATCH(name, type, SCOPE, ...)                            \
  _DEF_PARAM_##type(name, SCOPE, __VA_ARGS__)

// Type-specific macros: name, SCOPE, def, range/checker, description, attr, optional_values
#define _DEF_PARAM_STR(name, SCOPE, ...)                                      \
  _DEF_PARAMETER_SCOPE_EASY(String, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_INT(name, SCOPE, ...)                                       \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Int, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_DBL(name, SCOPE, ...)                                       \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Double, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_CAP(name, SCOPE, ...)                                       \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Capacity, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_TIME(name, SCOPE, ...)                                      \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Time, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_BOOL(name, SCOPE, ...)                                      \
  _DEF_PARAMETER_SCOPE_EASY(Bool, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_VERSION(name, SCOPE, ...)                                  \
  _DEF_PARAMETER_SCOPE_EASY(Version, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_MOMENT(name, SCOPE, ...)                                    \
  _DEF_PARAMETER_SCOPE_EASY(Moment, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_INT_LIST(name, SCOPE, ...)                                  \
  _DEF_PARAMETER_SCOPE_EASY(IntList, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_STR_LIST(name, SCOPE, ...)                                   \
  _DEF_PARAMETER_SCOPE_EASY(StrList, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_STR_WITH_CHECKER(name, SCOPE, ...)                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(String, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_INT_WITH_CHECKER(name, SCOPE, ...)                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Int, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_CAP_WITH_CHECKER(name, SCOPE, ...)                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Capacity, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_TIME_WITH_CHECKER(name, SCOPE, ...)                         \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Time, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_BOOL_WITH_CHECKER(name, SCOPE, ...)                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Bool, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_IP(name, SCOPE, def, ...)                                   \
  _DEF_PARAMETER_SCOPE_IP_EASY(String, name, SCOPE, def, __VA_ARGS__)
#define _DEF_PARAM_LOG_LEVEL(name, SCOPE, def, ...)                            \
  _DEF_PARAMETER_SCOPE_LOG_LEVEL_EASY(String, name, SCOPE, def, __VA_ARGS__)
#define _DEF_PARAM_WORK_AREA_POLICY(name, SCOPE, def, ...)                      \
  _DEF_PARAMETER_SCOPE_WORK_AREA_POLICY_EASY(String, name, SCOPE, def, __VA_ARGS__)
#define _DEF_PARAM_MODE_WITH_PARSER(name, SCOPE, ...)                          \
  _DEF_PARAMETER_SCOPE_PARSER_EASY(Mode, name, SCOPE, __VA_ARGS__)
#define _DEF_PARAM_LOG_ARCHIVE_OPTIONS_WITH_CHECKER(name, SCOPE, ...)          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(LogArchiveOptions, name, SCOPE, __VA_ARGS__)

// Legacy macros for backward compatibility (reorder arguments: name first)
#define DEF_INT(name, SCOPE, ...)                                               \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Int, name, SCOPE, __VA_ARGS__)

#define DEF_INT_WITH_CHECKER(name, SCOPE, ...)                                 \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Int, name, SCOPE, __VA_ARGS__)

#define DEF_DBL(name, SCOPE, ...)                                              \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Double, name, SCOPE, __VA_ARGS__)

#define DEF_CAP(name, SCOPE, ...)                                              \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Capacity, name, SCOPE, __VA_ARGS__)

#define DEF_CAP_WITH_CHECKER(name, SCOPE, ...)                                \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Capacity, name, SCOPE, __VA_ARGS__)

#define DEF_TIME(name, SCOPE, ...)                                             \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Time, name, SCOPE, __VA_ARGS__)

#define DEF_TIME_WITH_CHECKER(name, SCOPE, ...)                                \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Time, name, SCOPE, __VA_ARGS__)

#define DEF_BOOL(name, SCOPE, ...)                                             \
  _DEF_PARAMETER_SCOPE_EASY(Bool, name, SCOPE, __VA_ARGS__)

#define DEF_BOOL_WITH_CHECKER(name, SCOPE, ...)                                \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Bool, name, SCOPE, __VA_ARGS__)

#define DEF_STR(name, SCOPE, ...)                                              \
  _DEF_PARAMETER_SCOPE_EASY(String, name, SCOPE, __VA_ARGS__)

#define DEF_VERSION(name, SCOPE, ...)                                          \
  _DEF_PARAMETER_SCOPE_EASY(Version, name, SCOPE, __VA_ARGS__)

#define DEF_STR_WITH_CHECKER(name, SCOPE, ...)                                \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(String, name, SCOPE, __VA_ARGS__)

#define DEF_IP(name, SCOPE, ...)                                               \
  _DEF_PARAMETER_SCOPE_IP_EASY(String, name, SCOPE, __VA_ARGS__)

#define DEF_MOMENT(name, SCOPE, ...)                                           \
  _DEF_PARAMETER_SCOPE_EASY(Moment, name, SCOPE, __VA_ARGS__)

#define DEF_INT_LIST(name, SCOPE, ...)                                         \
  _DEF_PARAMETER_SCOPE_EASY(IntList, name, SCOPE, __VA_ARGS__)

#define DEF_STR_LIST(name, SCOPE, ...)                                         \
  _DEF_PARAMETER_SCOPE_EASY(StrList, name, SCOPE, __VA_ARGS__)

#define DEF_MODE_WITH_PARSER(name, SCOPE, ...)                                 \
  _DEF_PARAMETER_SCOPE_PARSER_EASY(Mode, name, SCOPE, __VA_ARGS__)

#define DEF_LOG_ARCHIVE_OPTIONS_WITH_CHECKER(name, SCOPE, ...)                 \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(LogArchiveOptions, name, SCOPE, __VA_ARGS__)
#define DEF_LOG_LEVEL(name, SCOPE, ...)                                       \
  _DEF_PARAMETER_SCOPE_LOG_LEVEL_EASY(String, name, SCOPE, __VA_ARGS__)

#define DEF_WORK_AREA_POLICY(name, SCOPE, ...)                                 \
  _DEF_PARAMETER_SCOPE_WORK_AREA_POLICY_EASY(String, name, SCOPE, __VA_ARGS__)
// For configuration items that only take effect in ERRSIM mode, the following macro must be used to define.


#define DEPRECATED_DEF_INT(args...)
#define DEPRECATED_DEF_INT_WITH_CHECKER(args...)
#define DEPRECATED_DEF_DBL(args...)
#define DEPRECATED_DEF_CAP(args...)
#define DEPRECATED_DEF_CAP_WITH_CHECKER(args...)
#define DEPRECATED_DEF_TIME(args...)
#define DEPRECATED_DEF_TIME_WITH_CHECKER(args...)
#define DEPRECATED_DEF_BOOL(args...)
#define DEPRECATED_DEF_STR(args...)
#define DEPRECATED_DEF_STR_WITH_CHECKER(args...)
#define DEPRECATED_DEF_IP(args...)
#define DEPRECATED_DEF_MOMENT(args...)
#define DEPRECATED_DEF_INT_LIST(args...)
#define DEPRECATED_DEF_STR_LIST(args...)
#define DEPRECATED_DEF_LOG_ARCHIVE_OPTIONS_WITH_CHECKER(args...)
#define DEPRECATED_DEF_LOG_LEVEL(args...)
#define DEPRECATED_DEF_WORK_AREA_POLICY(args...)
// For configuration items used temporarily (to be deleted before official release), the following macro must be used to define.
// ver Please write as v4.2, v3.2, etc., do not write as master
#define TEMP_DEF_INT(ver, args...)                                                       \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Int, args)

#define TEMP_DEF_INT_WITH_CHECKER(ver, args...)                                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Int, args)

#define TEMP_DEF_DBL(ver, args...)                                                       \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Double, args)

#define TEMP_DEF_CAP(ver, args...)                                                       \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Capacity, args)

#define TEMP_DEF_CAP_WITH_CHECKER(ver, args...)                                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Capacity, args)

#define TEMP_DEF_TIME(ver, args...)                                                      \
  _DEF_PARAMETER_SCOPE_RANGE_EASY(Time, args)

#define TEMP_DEF_TIME_WITH_CHECKER(ver, args...)                                         \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(Time, args)

#define TEMP_DEF_BOOL(ver, args...)                                                      \
  _DEF_PARAMETER_SCOPE_EASY(Bool, args)

#define TEMP_DEF_STR(ver, args...)                                                       \
  _DEF_PARAMETER_SCOPE_EASY(String, args)

#define TEMP_DEF_VERSION(ver, args...)                                                   \
  _DEF_PARAMETER_SCOPE_EASY(Version, args)

#define TEMP_DEF_STR_WITH_CHECKER(ver, args...)                                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(String, args)

#define TEMP_DEF_IP(ver, args...)                                                        \
  _DEF_PARAMETER_SCOPE_IP_EASY(String, args)

#define TEMP_DEF_MOMENT(ver, args...)                                                    \
  _DEF_PARAMETER_SCOPE_EASY(Moment, args)

#define TEMP_DEF_INT_LIST(ver, args...)                                                  \
  _DEF_PARAMETER_SCOPE_EASY(IntList, args)

#define TEMP_DEF_STR_LIST(ver, args...)                                                  \
  _DEF_PARAMETER_SCOPE_EASY(StrList, args)

#define TEMP_DEF_LOG_ARCHIVE_OPTIONS_WITH_CHECKER(ver, args...)                          \
  _DEF_PARAMETER_SCOPE_CHECKER_EASY(LogArchiveOptions, args)
#define TEMP_DEF_LOG_LEVEL(ver, args...)                                                 \
  _DEF_PARAMETER_SCOPE_LOG_LEVEL_EASY(String, args)

#define TEMP_DEF_WORK_AREA_POLICY(ver, args...)                                          \
  _DEF_PARAMETER_SCOPE_WORK_AREA_POLICY_EASY(String, args)

#endif
