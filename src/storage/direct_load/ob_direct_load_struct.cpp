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
#define USING_LOG_PREFIX STORAGE

#include "storage/direct_load/ob_direct_load_struct.h"

namespace oceanbase
{
namespace storage
{

/**
 * ObDirectLoadMode
 */

DEFINE_ENUM_FUNC(ObDirectLoadMode::Type, type, OB_DIRECT_LOAD_MODE_DEF, ObDirectLoadMode::);

bool ObDirectLoadMode::is_type_valid(const Type type)
{
  return type > INVALID_MODE && type < MAX_MODE;
}

/**
 * ObDirectLoadMethod
 */

DEFINE_ENUM_FUNC(ObDirectLoadMethod::Type, type, OB_DIRECT_LOAD_METHOD_DEF, ObDirectLoadMethod::);

bool ObDirectLoadMethod::is_type_valid(const Type type)
{
  return type > INVALID_METHOD && type < MAX_METHOD;
}

/**
 * ObDirectLoadInsertMode
 */

DEFINE_ENUM_FUNC(ObDirectLoadInsertMode::Type, type, OB_DIRECT_LOAD_INSERT_MODE_DEF, ObDirectLoadInsertMode::);

bool ObDirectLoadInsertMode::is_type_valid(const Type type)
{
  return type > INVALID_INSERT_MODE && type < MAX_INSERT_MODE;
}

/**
 * ObDirectLoadLevel
 */

DEFINE_ENUM_FUNC(ObDirectLoadLevel::Type, type, OB_DIRECT_LOAD_LEVEL_DEF, ObDirectLoadLevel::);

bool ObDirectLoadLevel::is_type_valid(const Type type)
{
  return type > INVALID_LEVEL && type < MAX_LEVEL;
}

/**
 * ObDirectLoadInsertSSTableType
 */

DEFINE_ENUM_FUNC(ObDirectLoadInsertSSTableType::Type, type, OB_DIRECT_LOAD_INSERT_SSTABLE_TYPE_DEF, ObDirectLoadInsertSSTableType::);

bool ObDirectLoadInsertSSTableType::is_type_valid(const Type type)
{
  return type > INVALID_INSERT_SSTABLE_TYPE && type < MAX_INSERT_SSTABLE_TYPE;
}

} // namespace storage
} // namespace oceanbase
