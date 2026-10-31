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

#include "common/object/ob_type_metadata.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace common
{

class SqlType;

enum class SqlTypeForm : uint8_t { Scalar, Array, Record, Enum, Set, UserDefined };
enum class SqlTimeSemantics : uint8_t {
  Unspecified, Date, TimeOfDay, Year, WallClock, Instant, SessionLocal, WithTimeZone
};
enum class DatumRepresentation : uint8_t { NoPayload, Inline64, Reference };

struct SqlNominalType
{
  SqlNominalType() : specified(false), tenant_id(0), object_id(0), schema_version(0) {}
  bool specified;
  uint64_t tenant_id;
  uint64_t object_id;
  int64_t schema_version;
};

struct SqlTypeMember
{
  SqlTypeMember() : name(), type(nullptr), nullable(true) {}
  ObString name;
  const SqlType *type;
  bool nullable;
};

// Mutable construction input. Precision and length semantics share storage in
// ObAccuracy, so only the attributes meaningful for the SQL kind may be marked.
// An explicit mask can express a specified negative numeric scale, including -1.
struct SqlTypeSpec
{
  enum Attribute : uint8_t { Precision = 1, Scale = 2, Length = 4, LengthSemantics = 8 };
  SqlTypeSpec()
    : attributes(), form(SqlTypeForm::Scalar), specified(0), time(SqlTimeSemantics::Unspecified),
      nominal(), element(nullptr), array_limit(-1), heterogeneous(false), members(nullptr),
      member_count(0), labels(nullptr), label_count(0) {}
  ObDataType attributes;
  SqlTypeForm form;
  uint8_t specified;
  SqlTimeSemantics time;
  SqlNominalType nominal;
  const SqlType *element;
  int64_t array_limit; // -1 means no declared bound.
  bool heterogeneous;
  const SqlTypeMember *members;
  int64_t member_count;
  const ObString *labels;
  int64_t label_count;
};

// Complete immutable type descriptors live in a schema/plan/explicit owner,
// never in a global TypeRef registry. create() copies names, labels and child
// descriptors into that owner. The owner must outlive all published pointers;
// resetting it invalidates the entire descriptor tree. Recursive UDTs use a
// nominal identity rather than a cycle of in-memory descriptor pointers.
class SqlType final
{
public:
  typedef int (*CastResolver)(const SqlType &, const SqlType &, void *, bool &);

  // A stack scalar descriptor needs no allocation. Treat it as immutable while
  // borrowed by a plan; from_legacy() may replace it only before publication.
  SqlType() : SqlType(SqlTypeSpec()) {}

  // owner must provide bulk lifetime management (for example, a plan/schema
  // arena). On failure, partial allocations remain owned by that arena and are
  // reclaimed at its reset/destruction; this is not a transactional factory for
  // an arbitrary malloc-style allocator.
  static int create(const SqlTypeSpec &spec, ObIAllocator &owner, const SqlType *&output)
  { return create(spec, owner, output, 0); }

  // Four-byte legacy metadata cannot recover enum labels, collection children
  // or a nominal schema version. Such types require an explicit complete spec.
  // -1 accuracy fields retain their legacy "unspecified" interpretation here;
  // callers with a declared scale of -1 must use create() and its explicit mask.
  static int from_legacy(const SqlTypeMeta &meta, const ObAccuracy &accuracy,
                         ObIAllocator &owner, const SqlType *&output)
  {
    ObDataType attributes;
    attributes.set_meta_type(meta);
    attributes.set_accuracy(accuracy);
    if (has_collation(meta.get_type())) { attributes.set_charset_type(meta.get_charset_type()); }
    return from_legacy(attributes, owner, output);
  }

  static int from_legacy(const ObDataType &attributes, ObIAllocator &owner,
                         const SqlType *&output)
  {
    SqlTypeSpec spec;
    const int ret = legacy_spec(attributes, spec);
    return ret == OB_SUCCESS ? create(spec, owner, output) : ret;
  }

  static int from_legacy(const SqlTypeMeta &meta, const ObAccuracy &accuracy,
                         SqlType &output)
  {
    ObDataType attributes;
    attributes.set_meta_type(meta); attributes.set_accuracy(accuracy);
    if (has_collation(meta.get_type())) { attributes.set_charset_type(meta.get_charset_type()); }
    return from_legacy(attributes, output);
  }

  static int from_legacy(const ObDataType &attributes, SqlType &output)
  {
    SqlTypeSpec spec;
    const int ret = legacy_spec(attributes, spec);
    if (ret == OB_SUCCESS) {
      output.attributes_ = spec.attributes;
      output.form_ = spec.form; output.specified_ = spec.specified; output.time_ = spec.time;
      output.nominal_ = spec.nominal; output.element_ = nullptr; output.array_limit_ = -1;
      output.heterogeneous_ = false; output.members_ = nullptr; output.member_count_ = 0;
      output.labels_ = nullptr; output.label_count_ = 0;
    }
    return ret;
  }

private:
  static int legacy_spec(const ObDataType &attributes, SqlTypeSpec &spec)
  {
    const SqlValueType kind = attributes.get_obj_type();
    if (!valid_kind(kind)) { return OB_INVALID_ARGUMENT; }
    if (kind == ObEnumType || kind == ObSetType || kind == ObEnumInnerType
        || kind == ObSetInnerType || kind == ObCollectionSQLType
        || kind == ObUserDefinedSQLType || attributes.meta_.is_ext_sql_array()
        || attributes.meta_.is_pl_extend_type()) { return OB_NOT_SUPPORTED; }
    spec.attributes = attributes;
    if (has_length(kind)) {
      if (attributes.get_length() != -1) { spec.specified |= SqlTypeSpec::Length; }
      if (attributes.get_length_semantics() != LS_INVALIED) { spec.specified |= SqlTypeSpec::LengthSemantics; }
    } else {
      if (attributes.get_precision() != -1) { spec.specified |= SqlTypeSpec::Precision; }
      if (attributes.get_scale() != -1) { spec.specified |= SqlTypeSpec::Scale; }
      else if (kind == ObDecimalIntType && attributes.meta_.get_scale() != -1) {
        spec.attributes.set_scale(attributes.meta_.get_scale());
        spec.specified |= SqlTypeSpec::Scale;
      }
    }
    spec.time = default_time(kind);
    return validate(spec);
  }

public:
  SqlValueType kind() const { return attributes_.get_obj_type(); }
  SqlTypeForm form() const { return form_; }
  const SqlTypeMeta &legacy_meta() const { return attributes_.meta_; }
  const ObAccuracy &accuracy() const { return attributes_.accuracy_; }
  const ObDataType &attributes() const { return attributes_; }
  bool has_precision() const { return (specified_ & SqlTypeSpec::Precision) != 0; }
  bool has_scale() const { return (specified_ & SqlTypeSpec::Scale) != 0; }
  bool has_length() const { return (specified_ & SqlTypeSpec::Length) != 0; }
  bool has_length_semantics() const { return (specified_ & SqlTypeSpec::LengthSemantics) != 0; }
  ObPrecision precision() const { return attributes_.get_precision(); }
  ObScale scale() const { return attributes_.get_scale(); }
  ObLength length() const { return attributes_.get_length(); }
  ObLengthSemantics length_semantics() const { return attributes_.get_length_semantics(); }
  ObCollationType collation() const { return attributes_.get_collation_type(); }
  SqlTimeSemantics time_semantics() const { return time_; }
  const SqlNominalType &nominal() const { return nominal_; }
  const SqlType *element() const { return element_; }
  int64_t array_limit() const { return array_limit_; }
  bool heterogeneous_array() const { return heterogeneous_; }
  int64_t member_count() const { return member_count_; }
  const SqlTypeMember &member(int64_t index) const { OB_ASSERT(index >= 0 && index < member_count_); return members_[index]; }
  int64_t label_count() const { return label_count_; }
  const ObString &label(int64_t index) const { OB_ASSERT(index >= 0 && index < label_count_); return labels_[index]; }

  // This is a future canonical native carrier choice, not the current pointer
  // ObDatum ABI. Every ordinary integer <=64 bits has exactly one representation.
  // Unknown Decimal precision always stays referenced, regardless of its value.
  DatumRepresentation canonical_representation() const
  {
    if ((form_ == SqlTypeForm::Enum && kind() == ObEnumType)
        || (form_ == SqlTypeForm::Set && kind() == ObSetType)) {
      return DatumRepresentation::Inline64;
    }
    if (form_ != SqlTypeForm::Scalar) { return DatumRepresentation::Reference; }
    if (kind() == ObNullType) { return DatumRepresentation::NoPayload; }
    // A nondefault interpretation can require an additional zone/context
    // payload. Until its codec is prepared, never assume it is a native word.
    if (time_ != default_time(kind())) { return DatumRepresentation::Reference; }
    if ((kind() >= ObTinyIntType && kind() <= ObUDoubleType)
        || (kind() >= ObDateTimeType && kind() <= ObYearType)
        || kind() == ObBitType || kind() == ObMySQLDateType || kind() == ObMySQLDateTimeType) {
      return DatumRepresentation::Inline64;
    }
    if (kind() == ObDecimalIntType && has_precision()
        && precision() > 0 && precision() <= MAX_PRECISION_DECIMAL_INT_64) {
      return DatumRepresentation::Inline64;
    }
    return DatumRepresentation::Reference;
  }

  bool equals(const SqlType &other) const
  {
    if (this == &other) { return true; }
    if (kind() != other.kind() || form_ != other.form_ || specified_ != other.specified_
        || time_ != other.time_ || !same_nominal(other)
        || (has_precision() && precision() != other.precision())
        || (has_scale() && scale() != other.scale())
        || (has_length() && length() != other.length())
        || (has_length_semantics() && length_semantics() != other.length_semantics())
        || !same_payload_metadata(other)) { return false; }
    // Coercibility belongs to expression binding; ZEROFILL belongs to display
    // formatting. Preserve these legacy attributes, but do not make them type
    // identity. Charset/binary attributes have meaning only for string kinds.
    if (has_collation(kind()) && (collation() != other.collation()
        || attributes_.charset_ != other.attributes_.charset_
        || attributes_.is_binary_collation_ != other.attributes_.is_binary_collation_)) { return false; }
    if (array_limit_ != other.array_limit_ || heterogeneous_ != other.heterogeneous_
        || member_count_ != other.member_count_ || label_count_ != other.label_count_
        || (element_ == nullptr) != (other.element_ == nullptr)) { return false; }
    if (element_ != nullptr && !element_->equals(*other.element_)) { return false; }
    for (int64_t i = 0; i < member_count_; ++i) {
      if (members_[i].name != other.members_[i].name || members_[i].nullable != other.members_[i].nullable
          || !members_[i].type->equals(*other.members_[i].type)) { return false; }
    }
    for (int64_t i = 0; i < label_count_; ++i) {
      if (labels_[i] != other.labels_[i]) { return false; }
    }
    return true;
  }

  // Decoder compatibility only: never permission to skip target constraints,
  // collation-aware equality/hash preparation, or a SQL cast. For example,
  // DECIMAL(19,2) and DECIMAL(20,2) share a coefficient layout but differ in type.
  bool physically_compatible(const SqlType &other) const
  {
    if (kind() != other.kind() || form_ != other.form_ || time_ != other.time_
        || !same_nominal(other) || !same_payload_metadata(other)) { return false; }
    if (form_ != SqlTypeForm::Scalar) { return equals(other); }
    if (has_collation(kind()) && attributes_.charset_ != other.attributes_.charset_) { return false; }
    if (kind() == ObDecimalIntType) {
      return has_precision() == other.has_precision() && has_scale() == other.has_scale()
          && (!has_scale() || scale() == other.scale())
          && decimal_width() == other.decimal_width();
    }
    return true;
  }

  // Conversion remains the existing SQL cast resolver's decision, including
  // mode/session rules. Failure never publishes a partially computed answer.
  int can_cast_to(const SqlType &target, CastResolver resolver, void *context, bool &possible) const
  {
    int ret = OB_SUCCESS;
    bool candidate = false;
    if (equals(target)) { candidate = true; }
    else if (resolver == nullptr) { ret = OB_NOT_SUPPORTED; }
    else { ret = resolver(*this, target, context, candidate); }
    if (ret == OB_SUCCESS) { possible = candidate; }
    return ret;
  }

private:
  explicit SqlType(const SqlTypeSpec &spec)
    : attributes_(spec.attributes), form_(spec.form), specified_(spec.specified),
      time_(spec.time == SqlTimeSemantics::Unspecified ? default_time(spec.attributes.get_obj_type()) : spec.time),
      nominal_(spec.nominal), element_(nullptr), array_limit_(spec.array_limit), heterogeneous_(spec.heterogeneous),
      members_(nullptr), member_count_(spec.member_count), labels_(nullptr), label_count_(spec.label_count) {}
  SqlType(const SqlType &) = delete;
  SqlType &operator=(const SqlType &) = delete;

  static bool valid_kind(SqlValueType kind)
  { return (kind >= ObNullType && kind < ObMaxType) || kind == ObTimestampLTZType || kind == ObTimestampNanoType; }
  static bool has_length(SqlValueType kind)
  { return has_collation(kind) || kind == ObCollectionSQLType; }
  static bool has_collation(SqlValueType kind)
  {
    return (kind >= ObVarcharType && kind <= ObHexStringType)
        || (kind >= ObTinyTextType && kind <= ObLongTextType)
        || (kind >= ObEnumType && kind <= ObGeometryType);
  }
  static SqlTimeSemantics default_time(SqlValueType kind)
  {
    switch (kind) {
      case ObDateType: case ObMySQLDateType: return SqlTimeSemantics::Date;
      case ObTimeType: return SqlTimeSemantics::TimeOfDay;
      case ObYearType: return SqlTimeSemantics::Year;
      case ObDateTimeType: case ObMySQLDateTimeType: case ObTimestampNanoType: return SqlTimeSemantics::WallClock;
      case ObTimestampType: return SqlTimeSemantics::Instant;
      case ObTimestampLTZType: return SqlTimeSemantics::SessionLocal;
      default: return SqlTimeSemantics::Unspecified;
    }
  }
  bool same_nominal(const SqlType &other) const
  {
    return nominal_.specified == other.nominal_.specified && (!nominal_.specified
        || (nominal_.tenant_id == other.nominal_.tenant_id && nominal_.object_id == other.nominal_.object_id
            && nominal_.schema_version == other.nominal_.schema_version));
  }
  bool same_payload_metadata(const SqlType &other) const
  {
    // The old scale byte is also used for locator flags and extended subtype.
    return kind() == ObExtendType ? legacy_meta().get_extend_type() == other.legacy_meta().get_extend_type()
        : kind() == ObBitType ? legacy_meta().get_scale() == other.legacy_meta().get_scale()
        : ((kind() >= ObTinyTextType && kind() <= ObLongTextType) || kind() == ObJsonType || kind() == ObGeometryType)
            ? legacy_meta().get_scale() == other.legacy_meta().get_scale() : true;
  }
  int decimal_width() const
  {
    if (!has_precision()) { return 0; }
    return precision() <= MAX_PRECISION_DECIMAL_INT_32 ? 4
        : precision() <= MAX_PRECISION_DECIMAL_INT_64 ? 8
        : precision() <= MAX_PRECISION_DECIMAL_INT_128 ? 16
        : precision() <= MAX_PRECISION_DECIMAL_INT_256 ? 32 : 64;
  }
  static int validate(const SqlTypeSpec &spec)
  {
    const SqlValueType kind = spec.attributes.get_obj_type();
    if (!valid_kind(kind) || static_cast<uint8_t>(spec.form) > static_cast<uint8_t>(SqlTypeForm::UserDefined)
        || static_cast<uint8_t>(spec.time) > static_cast<uint8_t>(SqlTimeSemantics::WithTimeZone)
        || (spec.specified & ~uint8_t(15)) != 0 || spec.array_limit < -1
        || spec.member_count < 0 || spec.label_count < 0
        || (spec.member_count > 0 && spec.members == nullptr)
        || (spec.label_count > 0 && spec.labels == nullptr)
        || spec.member_count > INT64_MAX / int64_t(sizeof(SqlTypeMember))
        || spec.label_count > INT64_MAX / int64_t(sizeof(ObString))) { return OB_INVALID_ARGUMENT; }
    // ObAccuracy overlays these fields. A descriptor must never publish a
    // length and numeric precision/scale as if they were independent storage.
    const uint8_t numeric = SqlTypeSpec::Precision | SqlTypeSpec::Scale;
    const uint8_t string = SqlTypeSpec::Length | SqlTypeSpec::LengthSemantics;
    if ((has_length(kind) && (spec.specified & numeric) != 0)
        || (!has_length(kind) && (spec.specified & string) != 0)) { return OB_INVALID_ARGUMENT; }
    if ((spec.specified & SqlTypeSpec::LengthSemantics) && (spec.attributes.get_length_semantics() < LS_DEFAULT
        || spec.attributes.get_length_semantics() > LS_BYTE)) { return OB_INVALID_ARGUMENT; }
    if ((spec.specified & SqlTypeSpec::Length) && spec.attributes.get_length() < 0) { return OB_INVALID_ARGUMENT; }
    if ((spec.specified & SqlTypeSpec::Precision) && ((spec.specified & SqlTypeSpec::LengthSemantics)
        || spec.attributes.get_precision() < 0)) { return OB_INVALID_ARGUMENT; }
    if (kind == ObDecimalIntType && (spec.specified & SqlTypeSpec::Precision)
        && (spec.attributes.get_precision() == 0 || spec.attributes.get_precision() > MAX_PRECISION_DECIMAL_INT_512)) {
      return OB_INVALID_ARGUMENT;
    }
    const bool enum_kind = kind >= ObEnumType && kind <= ObSetInnerType;
    const bool complex_kind = kind == ObUserDefinedSQLType || kind == ObCollectionSQLType
        || spec.attributes.meta_.is_ext_sql_array() || spec.attributes.meta_.is_pl_extend_type();
    if (spec.form == SqlTypeForm::Scalar && (enum_kind || complex_kind)) { return OB_NOT_SUPPORTED; }
    if (spec.form == SqlTypeForm::Array && kind != ObCollectionSQLType
        && !spec.attributes.meta_.is_ext_sql_array()) { return OB_INVALID_ARGUMENT; }
    if ((spec.form == SqlTypeForm::Record || spec.form == SqlTypeForm::UserDefined)
        && kind != ObUserDefinedSQLType && !spec.attributes.meta_.is_pl_extend_type()) { return OB_INVALID_ARGUMENT; }
    if ((spec.form == SqlTypeForm::Enum && kind != ObEnumType && kind != ObEnumInnerType)
        || (spec.form == SqlTypeForm::Set && kind != ObSetType && kind != ObSetInnerType)) { return OB_INVALID_ARGUMENT; }
    if ((spec.form == SqlTypeForm::Enum || spec.form == SqlTypeForm::Set) && spec.label_count == 0) { return OB_INVALID_ARGUMENT; }
    if (spec.form == SqlTypeForm::Array && (spec.element == nullptr && !spec.heterogeneous)) { return OB_INVALID_ARGUMENT; }
    if (spec.form != SqlTypeForm::Array && (spec.element != nullptr || spec.array_limit != -1 || spec.heterogeneous)) {
      return OB_INVALID_ARGUMENT;
    }
    if (spec.form != SqlTypeForm::Record && spec.form != SqlTypeForm::UserDefined && spec.member_count != 0) { return OB_INVALID_ARGUMENT; }
    if (spec.form != SqlTypeForm::Enum && spec.form != SqlTypeForm::Set && spec.label_count != 0) { return OB_INVALID_ARGUMENT; }
    if (spec.form == SqlTypeForm::UserDefined && !spec.nominal.specified) { return OB_NOT_SUPPORTED; }
    if (spec.nominal.specified && (spec.nominal.object_id == 0 || spec.nominal.schema_version < 0)) { return OB_INVALID_ARGUMENT; }
    if (spec.time != SqlTimeSemantics::Unspecified && default_time(kind) == SqlTimeSemantics::Unspecified) {
      return OB_INVALID_ARGUMENT;
    }
    for (int64_t i = 0; i < spec.member_count; ++i) {
      if (spec.members[i].type == nullptr || spec.members[i].name.length() < 0
          || (spec.members[i].name.length() > 0 && spec.members[i].name.ptr() == nullptr)) { return OB_INVALID_ARGUMENT; }
    }
    for (int64_t i = 0; i < spec.label_count; ++i) {
      if (spec.labels[i].length() < 0 || (spec.labels[i].length() > 0 && spec.labels[i].ptr() == nullptr)) { return OB_INVALID_ARGUMENT; }
    }
    return OB_SUCCESS;
  }
  static int copy_string(const ObString &source, ObIAllocator &owner, ObString &output)
  {
    if (source.empty()) { output.reset(); return OB_SUCCESS; }
    char *bytes = static_cast<char *>(owner.alloc(source.length()));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    MEMCPY(bytes, source.ptr(), source.length());
    output.assign_ptr(bytes, source.length());
    return OB_SUCCESS;
  }
  SqlTypeSpec specification() const
  {
    SqlTypeSpec spec;
    spec.attributes = attributes_; spec.form = form_; spec.specified = specified_; spec.time = time_;
    spec.nominal = nominal_; spec.element = element_; spec.array_limit = array_limit_; spec.heterogeneous = heterogeneous_;
    spec.members = members_; spec.member_count = member_count_; spec.labels = labels_; spec.label_count = label_count_;
    return spec;
  }
  static int create(const SqlTypeSpec &spec, ObIAllocator &owner, const SqlType *&output, int depth)
  {
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    int ret = validate(spec);
    if (ret != OB_SUCCESS) { return ret; }
    void *memory = owner.alloc(sizeof(SqlType));
    if (memory == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    SqlType *candidate = new (memory) SqlType(spec);
    if (spec.element != nullptr) { ret = create(spec.element->specification(), owner, candidate->element_, depth + 1); }
    if (ret == OB_SUCCESS && spec.member_count > 0) {
      auto *members = static_cast<SqlTypeMember *>(owner.alloc(spec.member_count * sizeof(SqlTypeMember)));
      if (members == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        candidate->members_ = members;
        for (int64_t i = 0; ret == OB_SUCCESS && i < spec.member_count; ++i) {
          new (&members[i]) SqlTypeMember(); members[i].nullable = spec.members[i].nullable;
          ret = copy_string(spec.members[i].name, owner, members[i].name);
          if (ret == OB_SUCCESS) { ret = create(spec.members[i].type->specification(), owner, members[i].type, depth + 1); }
        }
      }
    }
    if (ret == OB_SUCCESS && spec.label_count > 0) {
      auto *labels = static_cast<ObString *>(owner.alloc(spec.label_count * sizeof(ObString)));
      if (labels == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        candidate->labels_ = labels;
        for (int64_t i = 0; ret == OB_SUCCESS && i < spec.label_count; ++i) {
          new (&labels[i]) ObString(); ret = copy_string(spec.labels[i], owner, labels[i]);
        }
      }
    }
    // Partial construction belongs to the caller's arena; no pointer escapes.
    if (ret == OB_SUCCESS) { output = candidate; }
    return ret;
  }

  ObDataType attributes_;
  SqlTypeForm form_;
  uint8_t specified_;
  SqlTimeSemantics time_;
  SqlNominalType nominal_;
  const SqlType *element_;
  int64_t array_limit_;
  bool heterogeneous_;
  const SqlTypeMember *members_;
  int64_t member_count_;
  const ObString *labels_;
  int64_t label_count_;
};

} // namespace common
} // namespace oceanbase
