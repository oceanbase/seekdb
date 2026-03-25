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
 *
 * Change Stream plugin interface and registry.
 * Plugins implement process (per-subtask) and commit (batch commit, advance scn).
 */

#ifndef OB_CS_PLUGIN_H_
#define OB_CS_PLUGIN_H_

#include "lib/ob_define.h"
#include "lib/container/ob_iarray.h"
#include "share/change_stream/ob_change_stream_dispatcher.h"

namespace oceanbase
{
namespace share
{

// ---------------------------------------------------------------------------
// Plugin type enum and abstract interface (process / commit)
// ---------------------------------------------------------------------------

enum CS_PLUGIN_TYPE
{
  CS_PLUGIN_ASYNC_INDEX = 0,
  CS_PLUGIN_DEBUG = 1,
  CS_PLUGIN_MAX_TYPE           // Must equal CS_MAX_PLUGIN_COUNT in ob_change_stream_dispatcher.h
};

/// Change Stream framework interacts with external logic via plugins: register / process / commit.
class ObCSPlugin
{
public:
  ObCSPlugin() : plugin_type_(CS_PLUGIN_MAX_TYPE) {}
  virtual ~ObCSPlugin() = default;

  virtual int init() { return common::OB_SUCCESS; }
  virtual void destroy() {}

  /// Process incremental rows (called by Worker for each subtask).
  virtual int process(common::ObIArray<ObCSRow> &rows, ObCSExecCtx &ctx) = 0;
  /// Called when all subtasks of the batch are done (output, advance scn, commit).
  virtual int commit() = 0;

  CS_PLUGIN_TYPE get_plugin_type() const { return plugin_type_; }
  void set_plugin_type(CS_PLUGIN_TYPE type) { plugin_type_ = type; }

private:
  CS_PLUGIN_TYPE plugin_type_;
};

typedef ObCSPlugin *(*ObCSPluginFactoryFunc)();

// ---------------------------------------------------------------------------
// Debug plugin: logs every row via LOG_INFO for end-to-end testing.
// ---------------------------------------------------------------------------
class ObCSDebugPlugin : public ObCSPlugin
{
public:
  ObCSDebugPlugin() = default;
  ~ObCSDebugPlugin() override = default;

  int process(common::ObIArray<ObCSRow> &rows, ObCSExecCtx &ctx) override;
  int commit() override;

  static ObCSPlugin *create();
};

/// Plugin factory registration and creation.
class ObCSPluginRegistry
{
public:
  static ObCSPluginRegistry &get_instance();

  int register_factory(CS_PLUGIN_TYPE plugin_type, ObCSPluginFactoryFunc factory_func);
  ObCSPluginFactoryFunc get_factory(CS_PLUGIN_TYPE plugin_type) const;

private:
  ObCSPluginRegistry();
  ~ObCSPluginRegistry() = default;
  DISALLOW_COPY_AND_ASSIGN(ObCSPluginRegistry);

  ObCSPluginFactoryFunc factories_[CS_PLUGIN_MAX_TYPE];
};

}  // namespace share
}  // namespace oceanbase

#endif  // OB_CS_PLUGIN_H_
