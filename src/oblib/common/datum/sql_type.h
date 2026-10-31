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
#include "lib/utility/ob_print_utils.h"
#include <type_traits>

namespace oceanbase
{
namespace common
{

class SqlType;

enum class SqlTypeForm : uint8_t { Scalar, Array, Record, Enum, Set, UserDefined, Map, Vector, SparseVector };
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

// A declaration ID is not a schema object ID. Package declarations use the
// complete combined ID; local declarations additionally need their original
// compilation scope. Producers bind this provenance to the real declaration,
// and must preserve it when a parent declaration is imported into another unit.
enum class SqlPLDeclarationSource : uint8_t {
  None = 0, Local = 1, TableRow = 2, PackageSpec = 3,
  PackageBody = 4, Trigger = 5, SchemaType = 6
};

struct SqlPLCompilationScope
{
  SqlPLCompilationScope() : high(0), low(0) {}
  bool is_valid() const { return high != 0 || low != 0; }
  bool equals(const SqlPLCompilationScope &other) const
  { return high == other.high && low == other.low; }
  uint64_t high;
  uint64_t low;
};

struct SqlPLDeclarationIdentity
{
  SqlPLDeclarationIdentity()
    : source(SqlPLDeclarationSource::None), tenant_id(0), declaration_id(0), compilation_scope() {}
  bool is_specified() const { return source != SqlPLDeclarationSource::None; }
  bool is_local() const { return source == SqlPLDeclarationSource::Local; }
  bool equals(const SqlPLDeclarationIdentity &other) const
  {
    return source == other.source && tenant_id == other.tenant_id
        && declaration_id == other.declaration_id && compilation_scope.equals(other.compilation_scope);
  }
  // Field consistency only: a producer must still obtain these values from the
  // actual declaration owner. In particular, zero is not a missing-schema-version fallback.
  // None is valid for non-PL types; a PL Record producer also requires is_specified().
  bool valid_with(const SqlNominalType &nominal) const
  {
    if (static_cast<uint8_t>(source) > static_cast<uint8_t>(SqlPLDeclarationSource::SchemaType)) {
      return false;
    } else if (!is_specified()) {
      return tenant_id == 0 && declaration_id == 0 && !compilation_scope.is_valid();
    } else if (tenant_id == 0 || tenant_id == OB_INVALID_ID || declaration_id == OB_INVALID_ID) {
      return false;
    } else if (is_local()) {
      return compilation_scope.is_valid() && !nominal.specified;
    } else {
      return !compilation_scope.is_valid() && nominal.specified && nominal.tenant_id == tenant_id
          && nominal.object_id != 0 && nominal.object_id != OB_INVALID_ID && nominal.schema_version >= 0
          && (source != SqlPLDeclarationSource::TableRow || declaration_id == nominal.object_id);
    }
  }
  SqlPLDeclarationSource source;
  uint64_t tenant_id;
  uint64_t declaration_id;
  SqlPLCompilationScope compilation_scope;
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
      nominal(), pl_declaration(), element(nullptr), key(nullptr), value(nullptr), dimension(0),
      array_limit(-1), heterogeneous(false), members(nullptr),
      member_count(0), labels(nullptr), label_count(0) {}
  ObDataType attributes;
  SqlTypeForm form;
  uint8_t specified;
  SqlTimeSemantics time;
  SqlNominalType nominal;
  SqlPLDeclarationIdentity pl_declaration;
  const SqlType *element;
  const SqlType *key;
  const SqlType *value;
  uint32_t dimension;
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

  int clone_to(ObIAllocator &owner, const SqlType *&output) const
  { return create(specification(), owner, output, 0); }

  // Versioned metadata tree, never an address encoding. Nodes without PL
  // identity retain the historical v1 bytes. Identity-bearing nodes use v2;
  // v1 PL Records are rejected because their declaration origin is missing.
  // This metadata codec does not serialize the PL value/resource itself.
  // As for create(), a
  // failed decode leaves partial allocations in the caller's bulk-lifetime
  // owner, but publishes neither an output pointer nor an advanced cursor.
  int serialize(char *buffer, int64_t capacity, int64_t &position) const
  {
    const int64_t size = get_serialize_size();
    if (position < 0 || capacity < position || buffer == nullptr) { return OB_INVALID_ARGUMENT; }
    if (size < 0) { return OB_SIZE_OVERFLOW; }
    if (size > capacity - position) { return OB_BUF_NOT_ENOUGH; }
    int64_t cursor = position;
    const int ret = encode_node(buffer, capacity, cursor, 0);
    if (ret == OB_SUCCESS) { position = cursor; }
    return ret;
  }

  int64_t get_serialize_size() const
  { int64_t size = 0; return wire_size(size, 0) == OB_SUCCESS ? size : -1; }

  static int deserialize(const char *buffer, int64_t length, int64_t &position,
                         ObIAllocator &owner, const SqlType *&output)
  {
    if (position < 0 || length < position || buffer == nullptr) { return OB_INVALID_ARGUMENT; }
    int64_t cursor = position;
    const SqlType *candidate = nullptr;
    const int ret = decode_node(buffer, length, cursor, owner, candidate, 0);
    if (ret == OB_SUCCESS) { output = candidate; position = cursor; }
    return ret;
  }

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
      output.nominal_ = spec.nominal; output.pl_declaration_ = spec.pl_declaration;
      output.element_ = nullptr; output.array_limit_ = -1;
      output.key_ = nullptr; output.value_ = nullptr; output.dimension_ = 0;
      output.heterogeneous_ = false; output.members_ = nullptr; output.member_count_ = 0;
      output.labels_ = nullptr; output.label_count_ = 0;
    }
    return ret;
  }

private:
  static int add_wire_size(int64_t field, int64_t &size)
  {
    if (field < 0 || size > INT64_MAX - field) { return OB_SIZE_OVERFLOW; }
    size += field;
    return OB_SUCCESS;
  }

  int wire_size(int64_t &size, int depth) const
  {
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    int ret = OB_SUCCESS;
#define TYPE_WIRE_SIZE(FIELD) \
    if (ret == OB_SUCCESS) { ret = add_wire_size(serialization::encoded_length(FIELD), size); }
    TYPE_WIRE_SIZE(int64_t(pl_declaration_.is_specified() ? 2 : 1));
    TYPE_WIRE_SIZE(attributes_.meta_);
    TYPE_WIRE_SIZE(attributes_.accuracy_.get_accuracy());
    TYPE_WIRE_SIZE(attributes_.is_zero_fill_);
    // All attributes are explicit: the historical ObDataType codec omits
    // charset/binary and permits truncated trailing fields for compatibility.
    TYPE_WIRE_SIZE(static_cast<int32_t>(attributes_.charset_));
    TYPE_WIRE_SIZE(attributes_.is_binary_collation_);
    TYPE_WIRE_SIZE(static_cast<uint8_t>(form_));
    TYPE_WIRE_SIZE(specified_);
    TYPE_WIRE_SIZE(static_cast<uint8_t>(time_));
    TYPE_WIRE_SIZE(nominal_.specified);
    TYPE_WIRE_SIZE(nominal_.tenant_id);
    TYPE_WIRE_SIZE(nominal_.object_id);
    TYPE_WIRE_SIZE(nominal_.schema_version);
    if (pl_declaration_.is_specified()) {
      TYPE_WIRE_SIZE(static_cast<uint8_t>(pl_declaration_.source));
      TYPE_WIRE_SIZE(pl_declaration_.tenant_id);
      TYPE_WIRE_SIZE(pl_declaration_.declaration_id);
      TYPE_WIRE_SIZE(pl_declaration_.compilation_scope.high);
      TYPE_WIRE_SIZE(pl_declaration_.compilation_scope.low);
    }
    TYPE_WIRE_SIZE(array_limit_);
    TYPE_WIRE_SIZE(heterogeneous_);
    TYPE_WIRE_SIZE(element_ != nullptr);
    if (ret == OB_SUCCESS && element_ != nullptr) { ret = element_->wire_size(size, depth + 1); }
    TYPE_WIRE_SIZE(key_ != nullptr);
    if (ret == OB_SUCCESS && key_ != nullptr) { ret = key_->wire_size(size, depth + 1); }
    TYPE_WIRE_SIZE(value_ != nullptr);
    if (ret == OB_SUCCESS && value_ != nullptr) { ret = value_->wire_size(size, depth + 1); }
    TYPE_WIRE_SIZE(dimension_);
    TYPE_WIRE_SIZE(member_count_);
    for (int64_t i = 0; ret == OB_SUCCESS && i < member_count_; ++i) {
      TYPE_WIRE_SIZE(members_[i].name);
      TYPE_WIRE_SIZE(members_[i].nullable);
      if (ret == OB_SUCCESS) { ret = members_[i].type->wire_size(size, depth + 1); }
    }
    TYPE_WIRE_SIZE(label_count_);
    for (int64_t i = 0; ret == OB_SUCCESS && i < label_count_; ++i) { TYPE_WIRE_SIZE(labels_[i]); }
#undef TYPE_WIRE_SIZE
    return ret;
  }

  int encode_node(char *buffer, int64_t length, int64_t &position, int depth) const
  {
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    int ret = OB_SUCCESS;
#define TYPE_WIRE_WRITE(FIELD) \
    if (ret == OB_SUCCESS) { ret = serialization::encode(buffer, length, position, FIELD); }
    TYPE_WIRE_WRITE(int64_t(pl_declaration_.is_specified() ? 2 : 1));
    TYPE_WIRE_WRITE(attributes_.meta_);
    TYPE_WIRE_WRITE(attributes_.accuracy_.get_accuracy());
    TYPE_WIRE_WRITE(attributes_.is_zero_fill_);
    TYPE_WIRE_WRITE(static_cast<int32_t>(attributes_.charset_));
    TYPE_WIRE_WRITE(attributes_.is_binary_collation_);
    TYPE_WIRE_WRITE(static_cast<uint8_t>(form_));
    TYPE_WIRE_WRITE(specified_);
    TYPE_WIRE_WRITE(static_cast<uint8_t>(time_));
    TYPE_WIRE_WRITE(nominal_.specified);
    TYPE_WIRE_WRITE(nominal_.tenant_id);
    TYPE_WIRE_WRITE(nominal_.object_id);
    TYPE_WIRE_WRITE(nominal_.schema_version);
    if (pl_declaration_.is_specified()) {
      TYPE_WIRE_WRITE(static_cast<uint8_t>(pl_declaration_.source));
      TYPE_WIRE_WRITE(pl_declaration_.tenant_id);
      TYPE_WIRE_WRITE(pl_declaration_.declaration_id);
      TYPE_WIRE_WRITE(pl_declaration_.compilation_scope.high);
      TYPE_WIRE_WRITE(pl_declaration_.compilation_scope.low);
    }
    TYPE_WIRE_WRITE(array_limit_);
    TYPE_WIRE_WRITE(heterogeneous_);
    TYPE_WIRE_WRITE(element_ != nullptr);
    if (ret == OB_SUCCESS && element_ != nullptr) { ret = element_->encode_node(buffer, length, position, depth + 1); }
    TYPE_WIRE_WRITE(key_ != nullptr);
    if (ret == OB_SUCCESS && key_ != nullptr) { ret = key_->encode_node(buffer, length, position, depth + 1); }
    TYPE_WIRE_WRITE(value_ != nullptr);
    if (ret == OB_SUCCESS && value_ != nullptr) { ret = value_->encode_node(buffer, length, position, depth + 1); }
    TYPE_WIRE_WRITE(dimension_);
    TYPE_WIRE_WRITE(member_count_);
    for (int64_t i = 0; ret == OB_SUCCESS && i < member_count_; ++i) {
      TYPE_WIRE_WRITE(members_[i].name);
      TYPE_WIRE_WRITE(members_[i].nullable);
      if (ret == OB_SUCCESS) { ret = members_[i].type->encode_node(buffer, length, position, depth + 1); }
    }
    TYPE_WIRE_WRITE(label_count_);
    for (int64_t i = 0; ret == OB_SUCCESS && i < label_count_; ++i) { TYPE_WIRE_WRITE(labels_[i]); }
#undef TYPE_WIRE_WRITE
    return ret;
  }

  template <typename T>
  static int read_wire_field(const char *buffer, int64_t length, int64_t &position, T &field)
  {
    static_assert(std::is_integral<T>::value, "Type wire fields require an explicit bounded decoder");
    if (position >= length) { return OB_DESERIALIZE_ERROR; }
    if (sizeof(T) == 1) {
      if (std::is_same<T, bool>::value && static_cast<uint8_t>(buffer[position]) > 1) {
        return OB_DESERIALIZE_ERROR;
      }
    } else {
      // The existing vi decoder assumes the terminating byte is accessible.
      // Check both the input bound and the final partial word before calling it.
      const int bits = sizeof(T) * 8;
      bool terminated = false;
      int64_t cursor = position;
      for (int shift = 0; shift < bits && !terminated; shift += 7) {
        if (cursor >= length) { return OB_DESERIALIZE_ERROR; }
        const uint8_t byte = static_cast<uint8_t>(buffer[cursor++]);
        if (bits - shift < 7 && (byte >> (bits - shift)) != 0) { return OB_DESERIALIZE_ERROR; }
        terminated = (byte & 0x80) == 0;
      }
      if (!terminated) { return OB_DESERIALIZE_ERROR; }
    }
    return serialization::decode(buffer, length, position, field);
  }

  static int read_wire_field(const char *buffer, int64_t length, int64_t &position, SqlTypeMeta &field)
  {
    // The established metadata codec consists of exactly four one-byte fields.
    return length - position < 4 ? OB_DESERIALIZE_ERROR
        : serialization::decode(buffer, length, position, field);
  }

  static int read_wire_field(const char *buffer, int64_t length, int64_t &position, ObString &field)
  {
    int64_t cursor = position;
    int64_t bytes = 0;
    int ret = read_wire_field(buffer, length, cursor, bytes);
    if (ret == OB_SUCCESS && (bytes < 0 || bytes > INT32_MAX || bytes >= length - cursor
        || buffer[cursor + bytes] != '\0')) { ret = OB_DESERIALIZE_ERROR; }
    if (ret == OB_SUCCESS) {
      field.assign_ptr(const_cast<char *>(buffer + cursor), static_cast<int32_t>(bytes));
      position = cursor + bytes + 1;
    }
    return ret;
  }

  static int decode_node(const char *buffer, int64_t length, int64_t &position,
                         ObIAllocator &owner, const SqlType *&output, int depth)
  {
    if (depth > 64) { return OB_SIZE_OVERFLOW; }
    int ret = OB_SUCCESS;
    int64_t version = 0;
    int32_t charset = 0;
    uint8_t form = 0, time = 0;
    bool has_element = false, has_key = false, has_value = false;
    SqlTypeSpec spec;
#define TYPE_WIRE_READ(FIELD) \
    if (ret == OB_SUCCESS) { ret = read_wire_field(buffer, length, position, FIELD); }
    TYPE_WIRE_READ(version);
    if (ret == OB_SUCCESS && version != 1 && version != 2) { ret = OB_NOT_SUPPORTED; }
    TYPE_WIRE_READ(spec.attributes.meta_);
    TYPE_WIRE_READ(spec.attributes.accuracy_.accuracy_);
    TYPE_WIRE_READ(spec.attributes.is_zero_fill_);
    TYPE_WIRE_READ(charset);
    TYPE_WIRE_READ(spec.attributes.is_binary_collation_);
    TYPE_WIRE_READ(form);
    TYPE_WIRE_READ(spec.specified);
    TYPE_WIRE_READ(time);
    TYPE_WIRE_READ(spec.nominal.specified);
    TYPE_WIRE_READ(spec.nominal.tenant_id);
    TYPE_WIRE_READ(spec.nominal.object_id);
    TYPE_WIRE_READ(spec.nominal.schema_version);
    if (version == 2) {
      uint8_t source = 0;
      TYPE_WIRE_READ(source);
      TYPE_WIRE_READ(spec.pl_declaration.tenant_id);
      TYPE_WIRE_READ(spec.pl_declaration.declaration_id);
      TYPE_WIRE_READ(spec.pl_declaration.compilation_scope.high);
      TYPE_WIRE_READ(spec.pl_declaration.compilation_scope.low);
      spec.pl_declaration.source = static_cast<SqlPLDeclarationSource>(source);
      if (ret == OB_SUCCESS && !spec.pl_declaration.is_specified()) { ret = OB_DESERIALIZE_ERROR; }
    }
    TYPE_WIRE_READ(spec.array_limit);
    TYPE_WIRE_READ(spec.heterogeneous);
    TYPE_WIRE_READ(has_element);
    spec.attributes.charset_ = static_cast<ObCharsetType>(charset);
    spec.form = static_cast<SqlTypeForm>(form);
    spec.time = static_cast<SqlTimeSemantics>(time);
    if (ret == OB_SUCCESS && has_element) {
      ret = decode_node(buffer, length, position, owner, spec.element, depth + 1);
    }
    TYPE_WIRE_READ(has_key);
    if (ret == OB_SUCCESS && has_key) { ret = decode_node(buffer, length, position, owner, spec.key, depth + 1); }
    TYPE_WIRE_READ(has_value);
    if (ret == OB_SUCCESS && has_value) { ret = decode_node(buffer, length, position, owner, spec.value, depth + 1); }
    TYPE_WIRE_READ(spec.dimension);
    TYPE_WIRE_READ(spec.member_count);
    // Every member needs at least a string length, nullable byte and sixteen
    // mandatory child-node fields. Check before multiplication/allocation.
    if (ret == OB_SUCCESS && (spec.member_count < 0
        || spec.member_count > (length - position) / 18
        || spec.member_count > INT64_MAX / int64_t(sizeof(SqlTypeMember)))) { ret = OB_DESERIALIZE_ERROR; }
    if (ret == OB_SUCCESS && spec.member_count > 0) {
      auto *members = static_cast<SqlTypeMember *>(owner.alloc(spec.member_count * sizeof(SqlTypeMember)));
      if (members == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        spec.members = members;
        for (int64_t i = 0; ret == OB_SUCCESS && i < spec.member_count; ++i) {
          new (&members[i]) SqlTypeMember();
          ObString name;
          TYPE_WIRE_READ(name);
          TYPE_WIRE_READ(members[i].nullable);
          if (ret == OB_SUCCESS) { ret = copy_string(name, owner, members[i].name); }
          if (ret == OB_SUCCESS) { ret = decode_node(buffer, length, position, owner, members[i].type, depth + 1); }
        }
      }
    }
    TYPE_WIRE_READ(spec.label_count);
    if (ret == OB_SUCCESS && (spec.label_count < 0 || spec.label_count > length - position
        || spec.label_count > INT64_MAX / int64_t(sizeof(ObString)))) { ret = OB_DESERIALIZE_ERROR; }
    if (ret == OB_SUCCESS && spec.label_count > 0) {
      auto *labels = static_cast<ObString *>(owner.alloc(spec.label_count * sizeof(ObString)));
      if (labels == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else {
        spec.labels = labels;
        for (int64_t i = 0; ret == OB_SUCCESS && i < spec.label_count; ++i) {
          new (&labels[i]) ObString();
          ObString value;
          TYPE_WIRE_READ(value);
          if (ret == OB_SUCCESS) { ret = copy_string(value, owner, labels[i]); }
        }
      }
    }
#undef TYPE_WIRE_READ
    if (ret == OB_SUCCESS) { ret = validate(spec); }
    if (ret == OB_SUCCESS) {
      void *memory = owner.alloc(sizeof(SqlType));
      if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { output = new (memory) SqlType(spec); }
    }
    return ret;
  }

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
  const SqlPLDeclarationIdentity &pl_declaration() const { return pl_declaration_; }
  const SqlType *element() const { return element_; }
  const SqlType *key_type() const { return key_; }
  const SqlType *value_type() const { return value_; }
  uint32_t vector_dimension() const { return dimension_; }
  int64_t array_limit() const { return array_limit_; }
  bool heterogeneous_array() const { return heterogeneous_; }
  int64_t member_count() const { return member_count_; }
  const SqlTypeMember &member(int64_t index) const { OB_ASSERT(index >= 0 && index < member_count_); return members_[index]; }
  int64_t label_count() const { return label_count_; }
  const ObString &label(int64_t index) const { OB_ASSERT(index >= 0 && index < label_count_); return labels_[index]; }

  // Bounded metadata summary: do not traverse child types, labels or resources.
  // Read the ObAccuracy overlay only through its explicit attribute mask.
  TO_STRING_KV("kind", kind(), "form", static_cast<int>(form_), "specified", specified_,
      "precision", has_precision() ? precision() : PRECISION_UNKNOWN_YET,
      "scale", has_scale() ? scale() : SCALE_UNKNOWN_YET,
      "length", has_length() ? length() : -1,
      "length_semantics", has_length_semantics() ? length_semantics() : LS_INVALIED,
      "collation", collation(), "time", static_cast<int>(time_),
      "nominal_specified", nominal_.specified, "tenant_id", nominal_.tenant_id,
      "object_id", nominal_.object_id, "schema_version", nominal_.schema_version,
      "pl_source", static_cast<int>(pl_declaration_.source),
      "pl_tenant_id", pl_declaration_.tenant_id, "pl_declaration_id", pl_declaration_.declaration_id,
      "pl_scope_high", pl_declaration_.compilation_scope.high,
      "pl_scope_low", pl_declaration_.compilation_scope.low,
      "child_count", int(element_ != nullptr) + int(key_ != nullptr) + int(value_ != nullptr),
      "dimension", dimension_, "array_limit", array_limit_, "heterogeneous", heterogeneous_,
      "member_count", member_count_, "label_count", label_count_);

  // SQL UDT/collection byte encodings are self-contained spans. In contrast,
  // PL and extended SQL-array values carry an address whose pointee belongs to
  // a separate resource owner and needs that domain's existing clone routine.
  bool requires_resource_clone() const
  { return legacy_meta().is_ext_sql_array() || legacy_meta().is_pl_extend_type(); }

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
        || time_ != other.time_ || !same_nominal(other) || !pl_declaration_.equals(other.pl_declaration_)
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
        || dimension_ != other.dimension_ || (key_ == nullptr) != (other.key_ == nullptr)
        || (value_ == nullptr) != (other.value_ == nullptr)
        || member_count_ != other.member_count_ || label_count_ != other.label_count_
        || (element_ == nullptr) != (other.element_ == nullptr)) { return false; }
    if (element_ != nullptr && !element_->equals(*other.element_)) { return false; }
    if (key_ != nullptr && !key_->equals(*other.key_)) { return false; }
    if (value_ != nullptr && !value_->equals(*other.value_)) { return false; }
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
        || !same_nominal(other) || !pl_declaration_.equals(other.pl_declaration_)
        || !same_payload_metadata(other)) { return false; }
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
      nominal_(spec.nominal), pl_declaration_(spec.pl_declaration),
      element_(spec.element), key_(spec.key), value_(spec.value), dimension_(spec.dimension),
      array_limit_(spec.array_limit), heterogeneous_(spec.heterogeneous),
      members_(spec.members), member_count_(spec.member_count), labels_(spec.labels), label_count_(spec.label_count) {}
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
    if (!valid_kind(kind) || static_cast<uint8_t>(spec.form) > static_cast<uint8_t>(SqlTypeForm::SparseVector)
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
    const bool map = spec.form == SqlTypeForm::Map || spec.form == SqlTypeForm::SparseVector;
    const bool vector = spec.form == SqlTypeForm::Vector;
    if ((map || vector) && kind != ObCollectionSQLType) { return OB_INVALID_ARGUMENT; }
    if (map ? (spec.key == nullptr || spec.value == nullptr) : (spec.key != nullptr || spec.value != nullptr)) {
      return OB_INVALID_ARGUMENT;
    }
    if (vector ? spec.element == nullptr : spec.dimension != 0) { return OB_INVALID_ARGUMENT; }
    if ((spec.form == SqlTypeForm::Record || spec.form == SqlTypeForm::UserDefined)
        && kind != ObUserDefinedSQLType && !spec.attributes.meta_.is_pl_extend_type()) { return OB_INVALID_ARGUMENT; }
    if ((spec.form == SqlTypeForm::Enum && kind != ObEnumType && kind != ObEnumInnerType)
        || (spec.form == SqlTypeForm::Set && kind != ObSetType && kind != ObSetInnerType)) { return OB_INVALID_ARGUMENT; }
    if ((spec.form == SqlTypeForm::Enum || spec.form == SqlTypeForm::Set) && spec.label_count == 0) { return OB_INVALID_ARGUMENT; }
    if (spec.form == SqlTypeForm::Array && (spec.element == nullptr && !spec.heterogeneous)) { return OB_INVALID_ARGUMENT; }
    if (spec.form != SqlTypeForm::Array && ((!vector && spec.element != nullptr)
        || spec.array_limit != -1 || spec.heterogeneous)) {
      return OB_INVALID_ARGUMENT;
    }
    if (spec.form != SqlTypeForm::Record && spec.form != SqlTypeForm::UserDefined && spec.member_count != 0) { return OB_INVALID_ARGUMENT; }
    if (spec.form != SqlTypeForm::Enum && spec.form != SqlTypeForm::Set && spec.label_count != 0) { return OB_INVALID_ARGUMENT; }
    if (spec.form == SqlTypeForm::UserDefined && !spec.nominal.specified) { return OB_NOT_SUPPORTED; }
    if (spec.nominal.specified && (spec.nominal.object_id == 0 || spec.nominal.schema_version < 0)) { return OB_INVALID_ARGUMENT; }
    const bool pl_record = spec.form == SqlTypeForm::Record && spec.attributes.meta_.is_pl_extend_type();
    if (!spec.pl_declaration.valid_with(spec.nominal)) { return OB_INVALID_ARGUMENT; }
    if (pl_record && !spec.pl_declaration.is_specified()) { return OB_NOT_SUPPORTED; }
    if (spec.pl_declaration.is_specified() && (!pl_record || spec.specified != 0
        || static_cast<uint64_t>(spec.attributes.accuracy_.get_accuracy()) != spec.pl_declaration.declaration_id)) {
      return OB_INVALID_ARGUMENT;
    }
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
    if (source.length() < 0 || (source.length() > 0 && source.ptr() == nullptr)) { return OB_INVALID_ARGUMENT; }
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
    spec.nominal = nominal_; spec.pl_declaration = pl_declaration_;
    spec.element = element_; spec.key = key_; spec.value = value_; spec.dimension = dimension_;
    spec.array_limit = array_limit_; spec.heterogeneous = heterogeneous_;
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
    if (ret == OB_SUCCESS && spec.key != nullptr) { ret = create(spec.key->specification(), owner, candidate->key_, depth + 1); }
    if (ret == OB_SUCCESS && spec.value != nullptr) { ret = create(spec.value->specification(), owner, candidate->value_, depth + 1); }
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
  SqlPLDeclarationIdentity pl_declaration_;
  const SqlType *element_;
  const SqlType *key_;
  const SqlType *value_;
  uint32_t dimension_;
  int64_t array_limit_;
  bool heterogeneous_;
  const SqlTypeMember *members_;
  int64_t member_count_;
  const ObString *labels_;
  int64_t label_count_;
};

} // namespace common
} // namespace oceanbase
