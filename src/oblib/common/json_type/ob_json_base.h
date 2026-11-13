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

#ifndef OCEANBASE_SQL_OB_JSON_BASE
#define OCEANBASE_SQL_OB_JSON_BASE

#include "ob_json_path.h"
#include "common/mysqlclient/ob_mysql_global.h" // DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE
#include "common/number/ob_number_v2.h" // for number::ObNumber
#include "common/timezone/ob_time_convert.h" // for ObTime
#include "common/timezone/ob_timezone_info.h"
#include "lib/charset/ob_dtoa.h" // ob_gcvt

namespace oceanbase {
namespace common {

class ObIJsonBase;
class ObOTimestampData;

enum class ObJsonInType
{
  JSON_TEXT,
  JSON_BIN,
};

enum class ObJsonNodeType
{
  J_NULL, // 0
  J_DECIMAL,
  J_INT,
  J_UINT,
  J_DOUBLE,
  J_STRING, // 5
  J_OBJECT,
  J_ARRAY,
  J_BOOLEAN,
  J_DATE,
  J_TIME, // 10
  J_DATETIME,
  J_TIMESTAMP,
  J_OPAQUE,

  /* SQL/JSON extended types: type(), scalar type */
  J_OFLOAT = 15, //  number float  
  J_ODOUBLE = 16,  //  number double 
  J_ODECIMAL = 17, // number number
  J_OINT = 18, // number number
  J_OLONG = 19,  // number number
  J_OBINARY = 20, // binary string
  J_OOID = 21, // binary string
  J_ORAWHEX = 22, // binary string
  J_ORAWID = 23, // binary string
  J_JSON_DATE_EXT = 24, // date
  J_ODATE = 25,  // timestamp string
  J_OTIMESTAMP = 26, // timestamp string
  J_OTIMESTAMPTZ = 27, // timestamptz string
  J_ODAYSECOND = 28, // daySecondInterval string
  J_OYEARMONTH = 29, // yearMonthInterval string
  J_MYSQL_DATE = 30, // mysql date
  J_MYSQL_DATETIME = 31, // mysql datetime
  J_MAX_TYPE,

  J_ERROR = 200 // 14
};

OB_INLINE ObObjType json_type_to_obj_type(const  ObJsonNodeType json_type)
{
  const static ObObjType JSON_TYPE_TO_OBJ_TYPE[static_cast<int64_t>(ObJsonNodeType::J_MAX_TYPE) + 1] = {
    ObNullType,                          // ObJsonNodeType::J_NULL null type
    ObNumberType,                        // ObJsonNodeType::J_DECIMAL aka decimal/numeric
    ObIntType,                           // ObJsonNodeType::J_INT int64
    ObUInt64Type,                        // ObJsonNodeType::J_UINT uint64
    ObDoubleType,                        // ObJsonNodeType::J_DOUBLE double-precision floating point
    ObVarcharType,                       // ObJsonNodeType::J_STRING = 5,
    ObMaxType,                           // ObJsonNodeType::J_OBJECT = 6,
    ObMaxType,                           // ObJsonNodeType::J_ARRAY = 7,
    ObTinyIntType,                       // ObJsonNodeType::J_BOOLEAN int8, aka mysql boolean type
    ObDateType,                          // ObJsonNodeType::J_DATE = 9,
    ObTimeType,                          // ObJsonNodeType::J_TIME = 10,
    ObDateTimeType,                      // ObJsonNodeType::J_DATETIME = 11,
    ObTimestampType,                     // ObJsonNodeType::J_TIMESTAMP = 12,
    ObVarcharType,                       // ObJsonNodeType::J_OPAQUE = 13,
    ObMaxType,                           // UNKONWN = 14,
    ObFloatType,                         // ObJsonNodeType::J_OFLOAT = 15,
    ObDoubleType,                        // ObJsonNodeType::J_ODOUBLE = 16,
    ObNumberType,                        // ObJsonNodeType::J_ODECIMAL = 17,
    ObIntType,                           // ObJsonNodeType::J_OINT = 18,
    ObUInt64Type,                        // ObJsonNodeType::J_OLONG = 19,
    ObVarcharType,                       // ObJsonNodeType::J_OBINARY = 20,
    ObVarcharType,                       // ObJsonNodeType::J_OOID = 21,
    ObVarcharType,                       // ObJsonNodeType::J_ORAWHEX = 21,
    ObVarcharType,                       // ObJsonNodeType::J_ORAWID = 22,
    ObMaxType,                           // ObJsonNodeType::J_JSON_DATE_EXT = 24,
    ObDateType,                          // ObJsonNodeType::J_ODATE = 25,
    ObTimestampType,                     // ObJsonNodeType::J_OTIMESTAMP = 26,
    ObMaxType,                           // ObJsonNodeType::J_OTIMESTAMPTZ = 27,
    ObMaxType,                           // ObJsonNodeType::J_ODAYSECOND = 28,
    ObMaxType,                           // ObJsonNodeType::J_OYEARMONTH = 29,
    ObMySQLDateType,                     // ObJsonNodeType::J_MYSQL_DATE = 30,
    ObMySQLDateTimeType,                 // ObJsonNodeType::J_MYSQL_DATETIME = 31,
    ObMaxType,                           // ObJsonNodeType::J_MAX_TYPE
  };
  return JSON_TYPE_TO_OBJ_TYPE[static_cast<int64_t>(json_type)];
}

// sub-types of J_OPAQUE.
enum class JsonOpaqueType
{
  J_OPAQUE_BLOB = static_cast<uint32_t>(ObJsonNodeType::J_OPAQUE) + 1,
  J_OPAQUE_BIT,
  J_OPAQUE_GEOMETRY // not support now
};

#define FROM_BLOB_FLAG 1
#define HAS_BLOB_FLAG(flags) ((flags) & 1)

typedef std::pair<ObString, ObIJsonBase*> ObJsonObjPair;

class JsonObjectIterator
{
public:
  /**
    @param[in] wrapper the JSON object wrapper to iterate over
  */
  explicit JsonObjectIterator(const ObIJsonBase *wrapper);
  ~JsonObjectIterator() {}

  bool end() const;
  int get_elem(ObJsonObjPair &elem);
  int get_key(ObString &key);
  int get_value(ObIJsonBase *&value);
  int get_value(ObString &key, ObIJsonBase *&value);
  void next();
private:
  uint64_t curr_element_;
  uint64_t element_count_;
  const ObIJsonBase *json_object_;
};

class ObIJsonBase
{
public:
  explicit ObIJsonBase(ObIAllocator *allocator)
      : allocator_(allocator) 
  {
  }
  virtual ~ObIJsonBase() {}
  int64_t to_string(char *buf, const int64_t buf_len) const
  {
    int64_t pos = 0;
    databuff_printf(buf, buf_len, pos, "is_bin = %d", is_bin());
    return pos;
  }
  OB_INLINE bool is_bin() const { return get_internal_type() == ObJsonInType::JSON_BIN; }
  OB_INLINE ObIAllocator *get_allocator() { return allocator_; }
  OB_INLINE void set_allocator(ObIAllocator *allocator) { allocator_ = allocator; }
  virtual int reset() { return OB_SUCCESS; }
public:
  // Get the stored representation type.
  //
  // @return Returns ObJsonInType.
  virtual ObJsonInType get_internal_type() const = 0;

  // Get element count of json.
  //
  // @return json containner returns the capacity, json scalar return 1.
  virtual uint64_t element_count() const = 0;

  // Get json node type.
  //
  // @return see ObJsonNodeType.
  virtual ObJsonNodeType json_type() const = 0;

  // Get the database scalar field type.
  //
  // @return see ObObjType.
  virtual ObObjType field_type() const = 0;

  // Gey key by index from json node array.
  //
  // @param [in] index    The index of json object node array.
  // @param [out] key_out The result.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_key(uint64_t index, common::ObString &key_out) const = 0;

  // Gey array element by index.
  //
  // @param [in] index    The index of json array.
  // @param [out] value   The result.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_array_element(uint64_t index, ObIJsonBase *&value) const = 0;

  // Gey object value by index.
  //
  // @param [in] index    The index of json object node array.
  // @param [out] value   The result.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_object_value(uint64_t index, ObIJsonBase *&value) const = 0;
  virtual int get_object_value(uint64_t index, ObString &key, ObIJsonBase *&value) const = 0;

  // Gey object value by key.
  //
  // @param [in] key       The key to find.
  // @param [out] key_out  The result.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_object_value(const ObString &key, ObIJsonBase *&value) const = 0;

  virtual bool is_real_json_null(const ObIJsonBase* ptr) const;
  // Change json to string
  //
  // @param [in, out] j_buf   The dest buf.
  // @param [in] is_quoted  Whether append double quotes or not.
  // @param [in] is_pretty  Whether from JSON_PRETTY function or not.
  // @param [in]  depth      The depth of json tree.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int print(ObJsonBuffer &j_buf, bool is_quoted, uint64_t reserve_len = 0,
                    bool is_pretty = false, uint64_t depth = 0) const;
  
  // calculate json hash value
  //
  // @param [in] val        The initialized hash value.
  // @param [in] hash_func  The hash function.
  // @param [in] res        The result after calculate.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int calc_json_hash_value(uint64_t val, hash_algo hash_func, uint64_t &res) const;

  int print_database_scalar(ObJsonBuffer &j_buf, bool is_quoted) const;

  // Compare with other ObIJsonBase.
  //
  // @param [in] other  Another ObIJsonBase.
  // @param [out] res   Less than other returns -1, greater than 1, equal returns 0.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int compare(const ObIJsonBase &other, int &res, bool is_path = false) const;

  // Get depth of current json document.
  //
  // @return uint32_t.
  virtual uint32_t depth() const = 0;

  // Gets the number of bytes used for serialization.
  //
  // @param [in, out] size  The number of bytes taken to serialize.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_used_size(uint64_t &size);

  // Get the json binary free space.
  //
  // @param [in, out] size The json binary free space.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_free_space(uint64_t &size);

  // Get string after serializing the json doc.
  //
  // @param [out] out The string of json binary.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  virtual int get_raw_binary(common::ObString &out, ObIAllocator *allocator = NULL);

  // get object_iterator
  //
  // @return Returns object_iterator
  virtual JsonObjectIterator object_iterator() const;

  // Get member count of json.
  // scalar 1, array, object as child number
  // @return json containner returns the capacity, json scalar return 1.
  virtual uint64_t member_count() const = 0;

  // getter
  virtual bool get_boolean() const = 0;
  virtual double get_double() const = 0;
  virtual float get_float() const = 0;
  virtual int64_t get_int() const = 0;
  virtual uint64_t get_uint() const = 0;
  virtual const char *get_data() const = 0;
  virtual uint64_t get_data_length() const = 0;
  virtual number::ObNumber get_decimal_data() const = 0;
  virtual ObPrecision get_decimal_precision() const = 0;
  virtual ObScale get_decimal_scale() const = 0;
  virtual int get_obtime(ObTime &t) const = 0;
  bool is_json_number(ObJsonNodeType json_type) const;
  bool is_json_scalar(ObJsonNodeType json_type) const;
  bool is_json_string(ObJsonNodeType json_type) const;
  bool is_json_date(ObJsonNodeType json_type) const;

  // for cast
  int to_int(int64_t &value, bool check_range = false, bool force_convert = false) const;
  int to_uint(uint64_t &value, bool fail_on_negative = false, bool check_range = false) const;
  int to_double(double &value) const;
  int to_number(ObIAllocator *allocator, number::ObNumber &number) const;
  int to_datetime(int64_t &value, ObTimeConvertCtx *cvrt_ctx_t = nullptr) const;
  int to_mdatetime(ObMySQLDateTime &value, ObTimeConvertCtx *cvrt_ctx_t = nullptr) const;
  int to_date(int32_t &value) const;
  int to_mdate(ObMySQLDate &value, const ObDateSqlMode date_sql_mode) const;
  int to_time(int64_t &value) const;
  int to_bit(uint64_t &value) const;
protected:
  OB_INLINE void *alloc(const int64_t size)
  {
    return OB_NOT_NULL(allocator_) ? allocator_->alloc(size) : NULL;
  }
  OB_INLINE void free(void *ptr)
  {
    if (OB_NOT_NULL(ptr)) {
      allocator_->free(ptr);
    }
  }
private:
  // Change json time to string.
  //
  // @param [in, out] j_buf  The dest buf.
  // @param [in] is_quoted  Whether append double quotes or not.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  int print_jtime(ObJsonBuffer &j_buf, bool is_quoted) const;

  // Change json decimal to string.
  //
  // @param [in, out] j_buf      The dest buf.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  int print_decimal(ObJsonBuffer &j_buf) const;

  // Change json float to string.
  //
  // @param [in, out] j_buf      The dest buf.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  int print_float(ObJsonBuffer &j_buf) const;

  // Change json opaque to string.
  //
  // @param [in, out] j_buf      The dest buf.
  // @param [in]      depth     The depth of json tree.
  // @param [in]      is_quoted Whether append double quotes or not.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  int print_opaque(ObJsonBuffer &j_buf, uint64_t depth, bool is_quoted) const;

private:
  ObIAllocator *allocator_;
  DISALLOW_COPY_AND_ASSIGN(ObIJsonBase);
};

int compare_json_binary(const ObString &left, const ObString &right, int &result);
int hash_json_binary(const ObString &input, uint64_t seed, hash_algo algorithm, uint64_t &result);

class ObJsonBaseFactory
{
public:
  ObJsonBaseFactory() {};
  virtual ~ObJsonBaseFactory() {};
  static int get_json_base(ObIAllocator *allocator, const ObString &buf,
                           ObJsonInType in_type,
                           ObIJsonBase *&out, uint32_t parse_flag = 0,
                           uint32_t max_depth_config = 100);
  static int get_json_base(ObIAllocator *allocator, const char *ptr, uint64_t length,
                           ObJsonInType in_type,
                           ObIJsonBase *&out, uint32_t parse_flag = 0,
                           uint32_t max_depth_config = 100);
private:
  DISALLOW_COPY_AND_ASSIGN(ObJsonBaseFactory);
};

class ObJsonBaseUtil final
{
public:
  ObJsonBaseUtil() {}
  virtual ~ObJsonBaseUtil() {}

  // get is valid json datetime type.
  //
  // @param [in] jtype The json node type.
  // @return Returns true on time type, false otherwise.
  static ObObjType get_time_type(ObJsonNodeType json_type);

  // get is valid json datetime type.
  //
  // @param [in] jtype The json node type.
  // @return Returns true on time type, false otherwise.
  static bool is_time_type(ObJsonNodeType j_type);  

  // Get ObDTMode by json datetime type.
  //
  // @param [in] jtype The json node type.
  // @param [out] tmode ObDTMode
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int get_dt_mode_by_json_type(ObJsonNodeType j_type, ObDTMode &dt_mode);

  // Add double quotes to the source string, and handle the escape characters, then write them to buf.
  //
  // @param [in, out] j_buf    The dest buf. 
  // @param [in]      cptr   The pointer of source string.
  // @param [in]      length The length of source string.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int add_double_quote(ObJsonBuffer &j_buf, const char *cptr, uint64_t length);

  // Append data to buf, add double quotes if is_quoted is true.
  //
  // @param [in, out] j_buf     The buf that appending data.
  // @param [in] is_quoted    Whether append double quotes or not.
  // @param [in] data         The source string.
  // @param [in] length       The length of source string.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int append_string(ObJsonBuffer &j_buf, bool is_quoted, const char *data, uint64_t length);

  // Compare two numbers.
  //
  // @param [in] a The first value to be compared.
  // @param [in] b The decond value to be compared.
  // @return Less than returns -1, greater than 1, equal returns 0.
  template <class T>
  static inline int compare_numbers(T a, T b) {
    return a < b ? -1 : (a == b ? 0 : 1);
  }

  // Compare int with json.
  //
  // @param [in]  a       The first value with double to be compared.
  // @param [in]  other   The decond value with json type to be compared.
  // @param [out] result  Less than returns -1, greater than 1, equal returns 0.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int compare_int_json(int a, ObIJsonBase* other, int& result);

  // Change double to number
  //
  // @param [in]  d     The double type.
  // @param [out] num   The result of change.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  template<class T>
  static int double_to_number(double d, T &allocator, number::ObNumber &num)
  {
    INIT_SUCC(ret);
    char buf[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE] = {0};
    uint64_t length = ob_gcvt(d, ob_gcvt_arg_type::OB_GCVT_ARG_DOUBLE,
                              sizeof(buf) - 1, buf, NULL);
    ObString str(sizeof(buf), static_cast<int32_t>(length), buf);
    ObPrecision res_precision = PRECISION_UNKNOWN_YET;
    ObScale res_scale = NUMBER_SCALE_UNKNOWN_YET;

    if (OB_FAIL(num.from_sci_opt(str.ptr(), str.length(), allocator,
                                 &res_precision, &res_scale))) {
    }

    return ret;
  }

  // Change number to uint
  //
  // @param [in]  nmb   ObNumber
  // @param [out] value The result of change.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int number_to_uint(number::ObNumber &nmb, uint64_t &value);

  // Change double to uint
  //
  // @param [in]  d     The double type.
  // @param [out] value The result of change.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int double_to_uint(double d, uint64_t &value, bool check_range = false);

  // Get bit length from ObString.
  //
  // @param [in]  str       The ObString.
  // @param [out] bit_len   The bit length of ObString
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int get_bit_len(const ObString &str, int32_t &bit_len);

  // Get bit length from int64.
  //
  // @param [in]  int       The int64_t.
  // @param [out] bit_len   The bit length of int
  static int32_t get_bit_len(uint64_t value);
  
  // ObString to uint64_t
  //
  // @param [in]  str       The ObString.
  // @return The result of ObString to uint64_t.
  static uint64_t hex_to_uint64(const ObString &str);

  // Change string to bit
  //
  // @param [in]  string  The ObString type.
  // @param [out] value   The result of change.
  // @return Returns OB_SUCCESS on success, error code otherwise.
  static int string_to_bit(ObString &str, uint64_t &value);

  static int str_up_to_low(ObIAllocator* allocator, const ObString &src, ObString &dst);
  static int str_low_to_up(ObIAllocator* allocator, const ObString &src, ObString &dst);
  static int check_json_schema_ref_def(ObIAllocator& allocator, ObIJsonBase* json_doc);
private:
  DISALLOW_COPY_AND_ASSIGN(ObJsonBaseUtil);
};

inline bool is_mysql_unsupported_json_column_conversion(ObObjType type)
{
  bool bool_res = false;
  ObObjTypeClass tc = ob_obj_type_class(type);
  if (tc != ObStringTC && tc != ObTextTC && tc != ObJsonTC) {
    bool_res = true;
  }
  return bool_res;
}
} // namespace common
} // namespace oceanbase

#endif  // OCEANBASE_SQL_OB_JSON_BASE
