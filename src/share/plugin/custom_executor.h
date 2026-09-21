// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_SHARE_PLUGIN_CUSTOM_EXECUTOR_H_
#define SEEKDB_SHARE_PLUGIN_CUSTOM_EXECUTOR_H_
#include <string>
#include "seekdb/plugin/server_dev_executor.h"
namespace oceanbase { namespace share { namespace plugin {
// Owned, pointer-free logical binding. Cached plans do not pin executable
// code; open reacquires this EXACT generation/version, never a replacement.
struct CustomExecutorBinding {
  std::string service_id;
  std::string owner_id;
  std::string runtime_incarnation;
  uint64_t generation = 0;
  uint32_t major = 0, minor = 0, patch = 0;
};
class ICustomExecutor {
public:
  virtual ~ICustomExecutor() = default;
  virtual int next(const seekdb_plugin_custom_context_v1_t &context) = 0;
  virtual int rescan() = 0;
  virtual int close() = 0;
};
} } }
#endif
