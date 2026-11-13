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

namespace oceanbase {
namespace common { class ObMySQLTransaction; }
namespace sql { class ObSQLSessionInfo; class ObSQLSessionMgr; }
namespace query {

// A synchronous parallel-DDL call owns this scope and a reference to its
// originating user session. Inner SQL may borrow it only on that call stack.
// No pointer is published to KILL, another thread, or an asynchronous callback.
class CommandCancellationScope final
{
public:
  explicit CommandCancellationScope(const common::ObMySQLTransaction &transaction);
  ~CommandCancellationScope();
  CommandCancellationScope(const CommandCancellationScope &) = delete;
  CommandCancellationScope &operator=(const CommandCancellationScope &) = delete;

  int check_status();
  static int check_current();
  // Serializes the owner's final commit admission with KILL's session-state
  // publication. Cancellation after admission may be too late to undo effects.
  static int admit_commit(const common::ObMySQLTransaction &transaction);

private:
  friend class CommandCancellationMask;
  enum class Phase { ACTIVE, CANCELLED, COMMITTING };
  static CommandCancellationScope *&current();
  static unsigned &mask_depth();
  int check_locked();

  CommandCancellationScope *previous_;
  const common::ObMySQLTransaction *transaction_;
  sql::ObSQLSessionInfo *session_;
  sql::ObSQLSessionMgr *manager_;
  bool owns_reference_;
  Phase phase_;
  int status_;
};

// Mandatory rollback and failed-DDL compensation must remain runnable without
// clearing the originating query's cancellation. Nested scopes stay masked.
class CommandCancellationMask final
{
public:
  CommandCancellationMask();
  ~CommandCancellationMask();
  CommandCancellationMask(const CommandCancellationMask &) = delete;
  CommandCancellationMask &operator=(const CommandCancellationMask &) = delete;
};

} // namespace query
} // namespace oceanbase
