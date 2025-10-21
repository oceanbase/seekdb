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

#ifndef __OB_SHARE_CHECK_STOP_PROVIDER_H__
#define __OB_SHARE_CHECK_STOP_PROVIDER_H__
namespace oceanbase
{
namespace share
{
class ObCheckStopProvider
{
public:
  virtual ~ObCheckStopProvider() {}
  // return OB_CANCELED if stop, else return OB_SUCCESS
  virtual int check_stop() const = 0;
};
}
}
#endif /* __OB_SHARE_CHECK_STOP_PROVIDER_H__ */

