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

#pragma once

namespace oceanbase
{
namespace share
{

// SQL notifies the AI column workers after the task-creating transaction commits.
// Observer owns the worker implementation and binds this process-wide interface.
class IAIColumnTaskNotifier
{
public:
  virtual ~IAIColumnTaskNotifier() = default;
  virtual void wakeup() = 0;
};

} // namespace share
} // namespace oceanbase
