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

#define USING_LOG_PREFIX SHARE

#include "share/change_stream/ob_change_stream_plugin.h"
#include "share/change_stream/ob_cs_plugin_async_index.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/blocksstable/ob_row_reader.h"
#include "lib/oblog/ob_log_module.h"

namespace oceanbase
{
namespace share
{

// ===========================================================================
// ObCSDebugPlugin — prints every row for testing
// ===========================================================================

int ObCSDebugPlugin::process(common::ObIArray<ObCSRow> &rows, ObCSExecCtx &ctx)
{
  UNUSED(ctx);
  int ret = common::OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < rows.count(); ++i) {
    const ObCSRow &row = rows.at(i);
    blocksstable::ObRowReader row_reader;
    blocksstable::ObDatumRow new_datum_row;
    blocksstable::ObDatumRow old_datum_row;
    bool has_new = false, has_old = false;

    if (OB_NOT_NULL(row.new_row_.data_) && row.new_row_.size_ > 0) {
      if (OB_FAIL(row_reader.read_row(row.new_row_.data_, row.new_row_.size_, nullptr, new_datum_row))) {
        LOG_WARN("CSDebugPlugin: read new_row failed", K(ret), K(i));
      } else {
        has_new = true;
      }
    }
    if (OB_SUCC(ret) && OB_NOT_NULL(row.old_row_.data_) && row.old_row_.size_ > 0) {
      if (OB_FAIL(row_reader.read_row(row.old_row_.data_, row.old_row_.size_, nullptr, old_datum_row))) {
        LOG_WARN("CSDebugPlugin: read old_row failed", K(ret), K(i));
      } else {
        has_old = true;
      }
    }
    if (OB_SUCC(ret)) {
      LOG_DEBUG("CSDebugPlugin: row",
               K(i), K(row.tablet_id_), K(row.heap_pk_),
               K(row.commit_version_), K(row.seq_no_), K(row.column_cnt_),
               "dml", blocksstable::get_dml_str(row.dml_flag_),
               "new_row", has_new ? to_cstring(new_datum_row) : "NULL",
               "old_row", has_old ? to_cstring(old_datum_row) : "NULL");
    }
  }
  return ret;
}

int ObCSDebugPlugin::commit()
{
  LOG_DEBUG("CSDebugPlugin: commit called");
  return common::OB_SUCCESS;
}

ObCSPlugin *ObCSDebugPlugin::create()
{
  return OB_NEW(ObCSDebugPlugin, "CSDbgPlugin");
}

// Auto-register plugins at startup.
static int register_plugins_()
{
  int ret = ObCSPluginRegistry::get_instance().register_factory(
      CS_PLUGIN_ASYNC_INDEX, &create_async_index_plugin);
  if (OB_SUCC(ret)) {
    ret = ObCSPluginRegistry::get_instance().register_factory(
        CS_PLUGIN_DEBUG, &ObCSDebugPlugin::create);
  }
  return ret;
}
static int plugin_reg_ret_ __attribute__((unused)) = register_plugins_();

// ===========================================================================
// ObCSPluginRegistry
// ===========================================================================

ObCSPluginRegistry &ObCSPluginRegistry::get_instance()
{
  static ObCSPluginRegistry instance;
  return instance;
}

ObCSPluginRegistry::ObCSPluginRegistry()
{
  for (int64_t i = 0; i < CS_PLUGIN_MAX_TYPE; i++) {
    factories_[i] = nullptr;
  }
}

int ObCSPluginRegistry::register_factory(CS_PLUGIN_TYPE plugin_type,
                                        ObCSPluginFactoryFunc factory_func)
{
  int ret = common::OB_SUCCESS;
  if (plugin_type < 0 || plugin_type >= CS_PLUGIN_MAX_TYPE || nullptr == factory_func) {
    ret = common::OB_INVALID_ARGUMENT;
  } else {
    factories_[plugin_type] = factory_func;
  }
  return ret;
}

ObCSPluginFactoryFunc ObCSPluginRegistry::get_factory(CS_PLUGIN_TYPE plugin_type) const
{
  if (plugin_type < 0 || plugin_type >= CS_PLUGIN_MAX_TYPE) {
    return nullptr;
  }
  return factories_[plugin_type];
}

}  // namespace share
}  // namespace oceanbase
