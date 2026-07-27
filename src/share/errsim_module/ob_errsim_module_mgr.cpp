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

#define USING_LOG_PREFIX COMMON
#include "ob_errsim_module_mgr.h"
#include "share/rc/ob_server_runtime.h"

namespace oceanbase
{
using namespace lib;

namespace share
{

ObErrsimModuleMgr::ObErrsimModuleMgr()
    : is_inited_(false),
      lock_(),
      config_version_(0),
      is_whole_module_(false),
      module_set_(),
      percentage_(0)
{
}

ObErrsimModuleMgr::~ObErrsimModuleMgr()
{
}

int ObErrsimModuleMgr::server_module_init(ObErrsimModuleMgr *&errsim_module_mgr)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(errsim_module_mgr->init())) {
    LOG_WARN("failed to init errsim module mgr", K(ret), KP(errsim_module_mgr));
  }
  return ret;
}

void ObErrsimModuleMgr::destroy()
{
  module_set_.destroy();
}

int ObErrsimModuleMgr::init()
{
  int ret = OB_SUCCESS;
  const ObMemAttr bucket_attr("ErrsimModuleSet");

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("errsim module mgr init twice", K(ret));
  } else if (OB_FAIL(module_set_.create(MAX_BUCKET_NUM, bucket_attr))) {
    LOG_WARN("failed to create module set", K(ret));
  } else {
    is_whole_module_ = false;
    is_inited_ = true;
  }
  return ret;
}

bool ObErrsimModuleMgr::is_errsim_module(
    const ObErrsimModuleType::TYPE &type)
{
  bool b_ret = false;
  int ret = OB_SUCCESS;

  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("errsim module mgr is not initialized", K(ret));
  } else if (!ObErrsimModuleTypeHelper::is_valid(type)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("is errsim module get invalid argument", K(ret), K(type));
  } else {
    common::SpinRLockGuard guard(lock_);
    const int64_t percentage = ObRandom::rand(0, 100);
    if (percentage > percentage_) {
      b_ret = false;
    } else if (is_whole_module_) {
      b_ret = true;
    } else {
      ObErrsimModuleType module_type(type);
      const int32_t hash_ret = module_set_.exist_refactored(module_type);
      if (OB_HASH_NOT_EXIST == hash_ret) {
        b_ret = false;
      } else if (OB_HASH_EXIST == hash_ret) {
        b_ret = true;
      } else {
        b_ret = false;
        LOG_ERROR("failed to check module type exist", K(hash_ret));
      }
    }
  }
  return b_ret;
}

int ObErrsimModuleMgr::update_config(const int64_t config_version,
    const ModuleArray &module_array,
    const int64_t percentage)
{
  int ret = OB_SUCCESS;
  char type_buf[ObErrsimModuleTypeHelper::MAX_TYPE_NAME_LENGTH] = "";
  ObErrsimModuleType::TYPE type = ObErrsimModuleType::ERRSIM_MODULE_MAX;
  const int32_t flag = 1;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("errsim module mgr is not initialized", K(ret));
  } else if (config_version < 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid errsim module configuration", K(ret), K(config_version));
  } else {
    common::SpinWLockGuard guard(lock_);
    if (config_version <= config_version_) {
      //do nothing
    } else {
      is_whole_module_ = false;
      module_set_.reuse();
      percentage_ = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < module_array.size(); ++i) {
        const ErrsimModuleString &string = module_array.at(i);
        type = ObErrsimModuleTypeHelper::get_type(string.ptr());
        ObErrsimModuleType module_type(type);
        if (!module_type.is_valid()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("errsim module type is unexpected", K(ret), K(module_type), K(type_buf));
        } else if (ObErrsimModuleType::ERRSIM_MODULE_ALL == module_type.type_) {
          is_whole_module_ = true;
        } else if (OB_FAIL(module_set_.set_refactored(module_type, flag))) {
          LOG_WARN("failed to set module set", K(ret), K(module_type));
        } else {
          LOG_INFO("succeed set module", K(module_type));
        }
      }

      if (OB_SUCC(ret)) {
        percentage_ = percentage;
      }
    }
  }
  return ret;
}


} //share
} //oceanbase
