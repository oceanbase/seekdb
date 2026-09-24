/*
 * Copyright (c) 2026 OceanBase.
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
#include "share/schema/native_routine_signature.h"
#include "share/object/ob_obj_cast.h"
#include <algorithm>
#include <set>
#include <vector>

namespace oceanbase { namespace sql {

// Native standalone overload selection only. SQL expressions have already been
// resolved; this never evaluates defaults, grants privileges or chooses code by
// registration order. SeekDB's cast graph remains authoritative, not pg_cast.
class NativeRoutineOverload final
{
public:
  struct Type {
    common::ObObjType type_ = common::ObNullType;
    common::ObCollationType collation_ = common::CS_TYPE_INVALID;
    common::ObObjType logical() const {
      return common::ob_is_decimal_int(type_) ? common::ObNumberType : type_;
    }
    bool binary() const {
      return common::ob_is_string_or_lob_type(type_) && collation_ == common::CS_TYPE_BINARY;
    }
    bool unknown() const { return type_ == common::ObNullType || type_ == common::ObUnknownType; }
    bool operator==(const Type &other) const { return logical() == other.logical() && binary() == other.binary(); }
    bool operator<(const Type &other) const {
      return logical() != other.logical() ? logical() < other.logical() : binary() < other.binary();
    }
    TO_STRING_KV(K_(type), K_(collation));
  };
  struct Argument {
    Type type_;
    common::ObString name_; // Empty means positional, not an unnamed formal parameter.
    TO_STRING_KV(K_(type), K_(name));
  };

  static int select(const common::ObIArray<Argument> &arguments,
      const common::ObIArray<const share::schema::ObIRoutineInfo *> &routines,
      const share::schema::ObIRoutineInfo *&selected)
  {
    using namespace common;
    using namespace share::schema;
    selected = nullptr;
    if (arguments.count() > NativeRoutineSignature::MAX_EXPANDED_ARGUMENTS) return OB_ERR_SP_WRONG_ARG_NUM;
    bool named = false;
    for (int64_t i = 0; i < arguments.count(); ++i) {
      if (arguments.at(i).name_.empty()) {
        if (named) return OB_ERR_POSITIONAL_FOLLOW_NAME;
      } else named = true;
    }
    try {
      std::vector<Candidate> candidates;
      bool unsupported_named_array = false;
      for (int64_t i = 0; i < routines.count(); ++i) {
        const auto *routine = dynamic_cast<const ObRoutineInfo *>(routines.at(i));
        if (!routine || !routine->is_native() || !routine->is_native_binding_valid()) return OB_INVALID_ARGUMENT;
        const bool variadic = NativeRoutineSignature::variadic(*routine);
        if (variadic && named) { unsupported_named_array = true; continue; }
        int64_t count = 0;
        const int count_ret = NativeRoutineSignature::call_count(*routine, arguments.count(), count);
        if (count_ret == OB_ERR_SP_WRONG_ARG_NUM || arguments.count() > count) continue;
        if (count_ret != OB_SUCCESS) return count_ret;
        if (count < 0 || count > NativeRoutineSignature::MAX_EXPANDED_ARGUMENTS) return OB_INVALID_ARGUMENT;
        Candidate candidate{routine, variadic, {}, 0, 0};
        std::vector<bool> supplied(count, false);
        bool matches = true;
        for (int64_t j = 0; matches && j < arguments.count(); ++j) {
          int64_t position = j;
          const auto &argument = arguments.at(j);
          if (!argument.name_.empty()) {
            const int ret = routine->find_param_by_name(argument.name_, position);
            if (ret == OB_ERR_SP_UNDECLARED_VAR) { matches = false; break; }
            if (ret != OB_SUCCESS) return ret;
          }
          if (position < 0 || position >= count || supplied[position]) { matches = false; break; }
          supplied[position] = true;
          ObRoutineParam *parameter = nullptr;
          const int ret = routine->get_routine_param(NativeRoutineSignature::parameter_index(*routine, position), parameter);
          if (ret != OB_SUCCESS) return ret;
          if (!parameter || !parameter->is_in_param()) return OB_INVALID_ARGUMENT;
          const auto &formal = parameter->get_param_type();
          Type target{formal.get_obj_type(), formal.get_collation_type()};
          if (!can_convert(argument.type_, target)) { matches = false; break; }
          candidate.types_.push_back(target);
          if (!argument.type_.unknown()) {
            if (argument.type_ == target) ++candidate.exact_;
            else if (category(argument.type_) == category(target) && preferred(target)) ++candidate.preferred_;
          }
        }
        for (int64_t j = 0; matches && j < count; ++j) {
          if (!supplied[j]) {
            ObRoutineParam *parameter = nullptr;
            const int ret = routine->get_routine_param(j, parameter);
            if (ret != OB_SUCCESS) return ret;
            if (!parameter || parameter->get_default_value().empty()) matches = false;
          }
        }
        if (matches) candidates.push_back(std::move(candidate));
      }
      if (candidates.empty()) return unsupported_named_array ? OB_NOT_SUPPORTED : OB_ERR_SP_WRONG_ARG_NUM;
      // Same effective inputs: a non-variadic declaration wins over expansion.
      // Never prefer fewer defaults: identical defaulted prefixes are ambiguous.
      std::set<std::vector<Type>> fixed;
      for (const auto &candidate : candidates) if (!candidate.variadic_) fixed.insert(candidate.types_);
      discard(candidates, [&](const Candidate &candidate) { return candidate.variadic_ && fixed.count(candidate.types_); });
      int best = 0;
      for (const auto &candidate : candidates) best = std::max(best, candidate.exact_);
      discard(candidates, [&](const Candidate &candidate) { return candidate.exact_ < best; });
      best = 0;
      for (const auto &candidate : candidates) best = std::max(best, candidate.preferred_);
      discard(candidates, [&](const Candidate &candidate) { return candidate.preferred_ < best; });
      if (candidates.size() == 1) { selected = candidates.front().routine_; return OB_SUCCESS; }
      // Unknown NULLs/parameters never count as exact matches. Infer a category
      // only from surviving signatures, preferring character text (not bytes).
      for (int64_t j = 0; j < arguments.count(); ++j) if (arguments.at(j).type_.unknown()) {
        Category chosen = category(candidates.front().types_[j]);
        bool conflict = false, has_text = false;
        for (const auto &candidate : candidates) {
          const auto current = category(candidate.types_[j]);
          conflict |= current != chosen; has_text |= current == Category::TEXT;
        }
        if (has_text) chosen = Category::TEXT;
        else if (conflict) return OB_ERR_FUNC_DUP;
        bool has_preferred = false;
        for (const auto &candidate : candidates)
          has_preferred |= category(candidate.types_[j]) == chosen && preferred(candidate.types_[j]);
        discard(candidates, [&](const Candidate &candidate) {
          return category(candidate.types_[j]) != chosen || (has_preferred && !preferred(candidate.types_[j]));
        });
      }
      if (candidates.size() == 1) { selected = candidates.front().routine_; return OB_SUCCESS; }
      bool have_known = false, have_unknown = false, uniform = true;
      Type known;
      for (int64_t j = 0; j < arguments.count(); ++j) {
        const auto &type = arguments.at(j).type_;
        if (type.unknown()) have_unknown = true;
        else if (!have_known) { known = type; have_known = true; }
        else if (!(known == type)) uniform = false;
      }
      if (have_known && have_unknown && uniform) {
        discard(candidates, [&](const Candidate &candidate) {
          for (int64_t j = 0; j < arguments.count(); ++j)
            if (arguments.at(j).type_.unknown() && !can_convert(known, candidate.types_[j])) return true;
          return false;
        });
      }
      if (candidates.size() != 1) return OB_ERR_FUNC_DUP;
      selected = candidates.front().routine_;
      return OB_SUCCESS;
    } catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED; }
    catch (...) { return OB_ERR_UNEXPECTED; }
  }
private:
  enum class Category { NUMERIC, TEXT, BINARY, GEOMETRY, OTHER };
  struct Candidate {
    const share::schema::ObRoutineInfo *routine_;
    bool variadic_;
    std::vector<Type> types_;
    int exact_;
    int preferred_;
  };
  static Category category(const Type &type) {
    using namespace common;
    if (ob_is_integer_type(type.type_) || ob_is_number_tc(type.type_) || ob_is_decimal_int(type.type_)
        || ob_is_float_type(type.type_) || ob_is_double_type(type.type_)) return Category::NUMERIC;
    if (ob_is_string_or_lob_type(type.type_)) return type.binary() ? Category::BINARY : Category::TEXT;
    return ob_is_geometry(type.type_) ? Category::GEOMETRY : Category::OTHER;
  }
  static bool preferred(const Type &type) {
    return type.logical() == common::ObDoubleType || type.logical() == common::ObLongTextType;
  }
  static bool can_convert(const Type &source, const Type &target) {
    return source.unknown() || source == target || common::cast_supported(
        source.type_, source.collation_, target.type_, target.collation_);
  }
  template<class Predicate> static void discard(std::vector<Candidate> &candidates, Predicate predicate) {
    candidates.erase(std::remove_if(candidates.begin(), candidates.end(), predicate), candidates.end());
  }
};
} }
