/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/resolver/ddl/extension_routine_batch.h"
#include <new>
#include <vector>

namespace oceanbase { namespace sql {
using namespace common;

namespace {
// The legacy routine RPC codec intentionally omits schema_version for both the
// routine and its parameters: ordinary DDL assigns versions on the receiver.
// These in-process snapshots also carry provisional schemas used by subsequent
// script statements. Preserve their scalar versions without changing that RPC
// format or treating a caller-supplied version as a write reservation.
int copy_schema_versions(const share::schema::ObRoutineInfo &source, share::schema::ObRoutineInfo &target)
{
  const auto &from = source.get_routine_params();
  auto &to = target.get_routine_params();
  if (from.count() != to.count()) return OB_ERR_UNEXPECTED;
  for (int64_t i = 0; i < from.count(); ++i) {
    if (from.at(i) == nullptr || to.at(i) == nullptr) return OB_ERR_UNEXPECTED;
    to.at(i)->set_schema_version(from.at(i)->get_schema_version());
  }
  target.set_schema_version(source.get_schema_version());
  return OB_SUCCESS;
}
}

struct ExtensionRoutineBatch::Impl
{
  struct OwnedArg
  {
    // Destroy the decoded schema objects before their wire backing storage.
    std::vector<char> bytes_;
    obcall::ObCreateRoutineArg arg_;
  };
  std::vector<std::unique_ptr<OwnedArg>> items_;
};

ExtensionRoutineBatch::ExtensionRoutineBatch() = default;
ExtensionRoutineBatch::~ExtensionRoutineBatch() = default;
void ExtensionRoutineBatch::reset()
{
  args_.reset();
  impl_.reset();
}

int ExtensionRoutineBatch::assign(const ObIArray<const obcall::ObCreateRoutineArg *> &source)
{
  reset();
  int ret = OB_SUCCESS;
  try {
    if (source.empty() || source.count() > 4096) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      impl_ = std::make_unique<Impl>();
      impl_->items_.reserve(source.count());
      for (int64_t i = 0; OB_SUCC(ret) && i < source.count(); ++i) {
        if (nullptr == source.at(i)) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          auto owned = std::make_unique<Impl::OwnedArg>();
          const int64_t size = source.at(i)->get_serialize_size();
          if (size <= 0 || static_cast<uint64_t>(size) > SIZE_MAX) {
            ret = OB_SIZE_OVERFLOW;
          } else {
            owned->bytes_.resize(static_cast<size_t>(size));
            int64_t written = 0;
            ret = source.at(i)->serialize(owned->bytes_.data(), size, written);
            if (OB_SUCC(ret) && written != size) ret = OB_ERR_UNEXPECTED;
            int64_t read = 0;
            if (OB_SUCC(ret)) ret = owned->arg_.deserialize(owned->bytes_.data(), size, read);
            if (OB_SUCC(ret) && read != size) ret = OB_ERR_UNEXPECTED;
            if (OB_SUCC(ret)) ret = copy_schema_versions(source.at(i)->routine_info_, owned->arg_.routine_info_);
            if (OB_SUCC(ret)) ret = args_.push_back(&owned->arg_);
            if (OB_SUCC(ret)) impl_->items_.push_back(std::move(owned));
          }
        }
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret)) reset();
  return ret;
}

struct ExtensionRoutineUpdateBatch::Impl
{
  struct OwnedOperation
  {
    // Deserialized ObStrings can borrow this buffer. Destroy both payloads first.
    std::vector<char> bytes_;
    std::unique_ptr<obcall::ObCreateRoutineArg> create_;
    std::unique_ptr<obcall::ObDropRoutineArg> drop_;
  };
  std::vector<std::unique_ptr<OwnedOperation>> items_;
};

ExtensionRoutineUpdateBatch::ExtensionRoutineUpdateBatch() = default;
ExtensionRoutineUpdateBatch::~ExtensionRoutineUpdateBatch() = default;
void ExtensionRoutineUpdateBatch::reset()
{
  operations_.reset();
  impl_.reset();
}

int ExtensionRoutineUpdateBatch::assign(const ObIArray<Operation> &source)
{
  int ret = OB_SUCCESS;
  try {
    auto next = std::make_unique<Impl>();
    ObSEArray<Operation, 16> views;
    int64_t total = 0;
    if (source.count() > MAX_OPERATIONS) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      next->items_.reserve(source.count());
      for (int64_t i = 0; OB_SUCC(ret) && i < source.count(); ++i) {
        const Operation &input = source.at(i);
        if (!input.has_valid_shape()) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          auto owned = std::make_unique<Impl::OwnedOperation>();
          Operation output;
          output.kind_ = input.kind_;
          // Keep every base/derived field, including schema-version fences,
          // compiler diagnostics, dependencies, IF EXISTS and DDL audit text.
          auto copy = [&](const auto &arg, auto &destination) {
            int result = OB_SUCCESS;
            const int64_t size = arg.get_serialize_size();
            if (size <= 0 || size > MAX_WIRE_BYTES - total) {
              result = OB_SIZE_OVERFLOW;
            } else {
              owned->bytes_.resize(static_cast<size_t>(size));
              int64_t written = 0;
              result = arg.serialize(owned->bytes_.data(), size, written);
              if (OB_SUCCESS == result && written != size) result = OB_ERR_UNEXPECTED;
              int64_t read = 0;
              if (OB_SUCCESS == result) result = destination.deserialize(owned->bytes_.data(), size, read);
              if (OB_SUCCESS == result && read != size) result = OB_ERR_UNEXPECTED;
              if (OB_SUCCESS == result) total += size;
            }
            return result;
          };
          if (input.kind_ == Operation::Kind::DROP) {
            owned->drop_ = std::make_unique<obcall::ObDropRoutineArg>();
            ret = copy(*input.drop_arg_, *owned->drop_);
            output.drop_arg_ = owned->drop_.get();
          } else {
            owned->create_ = std::make_unique<obcall::ObCreateRoutineArg>();
            ret = copy(*input.create_arg_, *owned->create_);
            if (OB_SUCC(ret)) ret = copy_schema_versions(input.create_arg_->routine_info_, owned->create_->routine_info_);
            output.create_arg_ = owned->create_.get();
          }
          if (OB_SUCC(ret)) ret = views.push_back(output);
          if (OB_SUCC(ret)) next->items_.push_back(std::move(owned));
        }
      }
    }
    if (OB_SUCC(ret)) {
      // Only now invalidate old views: the input can borrow this very batch.
      reset();
      ret = operations_.assign(views);
      if (OB_SUCC(ret)) impl_ = std::move(next);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret)) reset();
  return ret;
}

} }
