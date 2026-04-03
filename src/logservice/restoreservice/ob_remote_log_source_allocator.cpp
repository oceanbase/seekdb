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
#include "ob_remote_log_source_allocator.h"

namespace oceanbase
{
namespace logservice
{
ObRemoteLogParent *ObResSrcAlloctor::alloc(const share::ObLogRestoreSourceType &type,
    const share::ObLSID &ls_id)
{
  ObRemoteLogParent *source = NULL;
  if (! is_valid_log_source_type(type)) {
    // just skip
  } else {
    switch (type) {
      case share::ObLogRestoreSourceType::SERVICE:
        source = OB_NEW(ObRemoteSerivceParent, "SerSource", ls_id);
        break;
      case share::ObLogRestoreSourceType::RAWPATH:
        source = OB_NEW(ObRemoteRawPathParent, "DestSource", ls_id);
        break;
      case share::ObLogRestoreSourceType::LOCATION:
        source = OB_NEW(ObRemoteLocationParent, "LocSource", ls_id);
        break;
      default:
        CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "cannot allocate ObRemoteLogParent for invalid type", K(type), K(ls_id));
        break;
    }
  }
  return source;
}

void ObResSrcAlloctor::free(ObRemoteLogParent *source)
{
  if (NULL != source) {
    const share::ObLogRestoreSourceType type = source->get_source_type();
    switch (type) {
      case share::ObLogRestoreSourceType::SERVICE:
        MTL_DELETE(ObRemoteLogParent, "SerSource", source);
        break;
      case share::ObLogRestoreSourceType::RAWPATH:
        MTL_DELETE(ObRemoteLogParent, "DestSource", source);
        break;
      case share::ObLogRestoreSourceType::LOCATION:
        MTL_DELETE(ObRemoteLogParent, "LocSource", source);
        break;
      default:
        CLOG_LOG_RET(ERROR, OB_ERR_UNEXPECTED, "cannot free ObRemoteLogParent for a invalid type", K(type), KP(source));
        break;
    }
    source = NULL;
  }
}
} // namespace logservice
} // namespace oceanbase
