/*
 * Copyright (c) 2026 OceanBase.
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

#include <optional>
#include "lib/allocator/ob_allocator.h"
#include "lib/container/ob_se_array.h"
#include "share/vector/ob_bit_vector.h"
#include "rust/cxx.h"
#include "rust/expression_program.h"
#include "rust/expression-program/include/host.h"

namespace oceanbase
{
namespace sql
{
class ObOperator;
struct ObExpr;

class ObOpSpec;
class ObPhysicalPlan;

class CompiledExpressionProgram final : public expression::IExecutionHost
{
public:
  static int create(const ObOpSpec &spec, common::ObIAllocator &allocator,
                    CompiledExpressionProgram *&result);
  ~CompiledExpressionProgram() override = default;
  uint64_t *allocate(size_t words) override;
  void release(uint64_t *data, size_t words) override;
  int32_t check_cancel() override { return common::OB_SUCCESS; }

private:
  friend class ExpressionProgram;
  explicit CompiledExpressionProgram(common::ObIAllocator &allocator);

  common::ObIAllocator &allocator_;
  std::optional<rust::Box<expression::Prepared>> prepared_;
  common::ObSEArray<const ObExpr *, 16, common::ObWrapperAllocator> inputs_;
  common::ObSEArray<uint8_t, 16, common::ObWrapperAllocator> materialized_inputs_;
  common::ObSEArray<const ObExpr *, 16, common::ObWrapperAllocator> nodes_;
  common::ObSEArray<uint32_t, 8, common::ObWrapperAllocator> outputs_;
  bool allocation_failed_ = false;
};

class ExpressionProgramCatalog final
{
public:
  static int create(ObPhysicalPlan &plan, ExpressionProgramCatalog *&result);
  ~ExpressionProgramCatalog();

private:
  ExpressionProgramCatalog(const ExpressionProgramCatalog &) = delete;
  ExpressionProgramCatalog &operator=(const ExpressionProgramCatalog &) = delete;
  struct Entry {
    ObOpSpec *spec_;
    CompiledExpressionProgram *program_;
  };
  explicit ExpressionProgramCatalog(common::ObIAllocator &allocator);
  int prepare(ObOpSpec &root);
  common::ObIAllocator &allocator_;
  common::ObSEArray<Entry, 8, common::ObWrapperAllocator> entries_;
};

class ExpressionProgram final : public expression::IExecutionHost
{
public:
  static int create(ObOperator &owner, int64_t initial_rows, ExpressionProgram *&result);
  ~ExpressionProgram() override = default;
  int execute(ObBitVector *skip, int64_t rows, bool batch,
              bool &all_filtered, bool &all_active);
  uint64_t *allocate(size_t words) override;
  void release(uint64_t *data, size_t words) override;
  int32_t check_cancel() override;

private:
  ExpressionProgram(ObOperator &owner, const CompiledExpressionProgram &compiled);
  int ensure_capacity(int64_t rows);
  int import_inputs(int64_t rows, bool batch, const ObBitVector *skip);
  void export_root(const ObExpr &expr, size_t node, int64_t rows, bool batch);
  int report_error(const expression::RunStatus &status);

  ObOperator &owner_;
  const CompiledExpressionProgram &compiled_;
  std::optional<rust::Box<expression::Runner>> runner_;
  common::ObSEArray<uint8_t, 16, common::ObWrapperAllocator> skip_;
  size_t capacity_;
  bool allocation_failed_ = false;
};
}
}
