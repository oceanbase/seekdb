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

#ifndef __OB_SQL_RESOURCE_EXECUTOR_H__
#define __OB_SQL_RESOURCE_EXECUTOR_H__
namespace oceanbase
{
namespace sql
{
class ObExecContext;
class ObCreateResourcePoolStmt;
class ObDropResourcePoolStmt;
class ObSplitResourcePoolStmt;
class ObMergeResourcePoolStmt;
class ObAlterResourceTenantStmt;
class ObAlterResourcePoolStmt;
class ObCreateResourceUnitStmt;
class ObAlterResourceUnitStmt;
class ObDropResourceUnitStmt;

class ObCreateResourcePoolExecutor
{
public:
  ObCreateResourcePoolExecutor();
  virtual ~ObCreateResourcePoolExecutor();
  int execute(ObExecContext &ctx, ObCreateResourcePoolStmt &stmt);
private:
};

class ObDropResourcePoolExecutor
{
public:
  ObDropResourcePoolExecutor();
  virtual ~ObDropResourcePoolExecutor();
  int execute(ObExecContext &ctx, ObDropResourcePoolStmt &stmt);
private:
};

class ObSplitResourcePoolExecutor
{
public:
  ObSplitResourcePoolExecutor();
  virtual ~ObSplitResourcePoolExecutor();
  int execute(ObExecContext &ctx, ObSplitResourcePoolStmt &stmt);
private:
};

class ObMergeResourcePoolExecutor
{
public:
  ObMergeResourcePoolExecutor();
  virtual ~ObMergeResourcePoolExecutor();
  int execute(ObExecContext &ctx, ObMergeResourcePoolStmt &stmt);
private:
};

class ObAlterResourceTenantExecutor
{
public:
  ObAlterResourceTenantExecutor();
  virtual ~ObAlterResourceTenantExecutor();
  int execute(ObExecContext &ctx, ObAlterResourceTenantStmt &stmt);
private:
};

class ObAlterResourcePoolExecutor
{
public:
  ObAlterResourcePoolExecutor();
  virtual ~ObAlterResourcePoolExecutor();
  int execute(ObExecContext &ctx, ObAlterResourcePoolStmt &stmt);
private:
};

class ObCreateResourceUnitExecutor
{
public:
  ObCreateResourceUnitExecutor();
  virtual ~ObCreateResourceUnitExecutor();
  int execute(ObExecContext &ctx, ObCreateResourceUnitStmt &stmt);
private:
};

class ObAlterResourceUnitExecutor
{
public:
  ObAlterResourceUnitExecutor();
  virtual ~ObAlterResourceUnitExecutor();
  int execute(ObExecContext &ctx, ObAlterResourceUnitStmt &stmt);
private:
};

class ObDropResourceUnitExecutor
{
public:
  ObDropResourceUnitExecutor();
  virtual ~ObDropResourceUnitExecutor();
  int execute(ObExecContext &ctx, ObDropResourceUnitStmt &stmt);
private:
};

}
}
#endif /* __OB_SQL_RESOURCE_EXECUTOR_H__ */
//// end of header file

