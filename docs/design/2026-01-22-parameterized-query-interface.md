# Parameterized Query Interface for Embedded SQL Execution

## 1. Background and Motivation

### 1.1 Current Problem

The current embedded SQL execution interface (`ObSPIService`) only supports SQL string parameters:

```cpp
// Current interfaces
static int spi_query(pl::ObPLExecCtx *ctx, const char* sql, ...);
static int spi_execute(pl::ObPLExecCtx *ctx, const char* ps_sql, ...);
```

This approach has several issues:
- **SQL Injection Risk**: Direct string concatenation is vulnerable to injection attacks
- **String Escaping Difficulty**: Handling strings with special characters (like `'`) is error-prone
- **Type Safety**: No compile-time type checking for parameters

### 1.2 Solution Overview

Implement a parameterized query interface similar to pymysql:

```python
# pymysql example
cursor.execute("SELECT * FROM users WHERE id = %s AND name = %s", (user_id, user_name))
```

Proposed C++ interface:

```cpp
// Proposed interface
ObSPIParamList params(allocator);
params.add_int(user_id).add_string(user_name);
ObSPIService::spi_query_with_params(ctx,
    "SELECT * FROM users WHERE id = ? AND name = ?", params, ...);
```

## 2. API Design

### 2.1 ObSPIParam Class

Single parameter wrapper with type-safe factory methods:

```cpp
class ObSPIParam {
public:
  enum ParamMode { SPI_PARAM_IN = 0, SPI_PARAM_OUT = 1, SPI_PARAM_INOUT = 2 };

  // Factory methods
  static ObSPIParam null();
  static ObSPIParam from_int(int64_t value);
  static ObSPIParam from_uint(uint64_t value);
  static ObSPIParam from_float(float value);
  static ObSPIParam from_double(double value);
  static ObSPIParam from_string(const char* value);
  static ObSPIParam from_string(const ObString& value);
  static ObSPIParam from_datetime(int64_t usec);
  static ObSPIParam from_number(const number::ObNumber& num);
  static ObSPIParam from_blob(const void* data, int64_t len);

  // Accessors
  const ObObjParam& get_obj_param() const;
  ParamMode get_mode() const;
  bool is_null() const;
  ObObjType get_type() const;
};
```

### 2.2 ObSPIParamList Class

Parameter list with fluent API:

```cpp
class ObSPIParamList {
public:
  explicit ObSPIParamList(ObIAllocator& allocator);

  // Fluent API for adding parameters
  ObSPIParamList& add_null();
  ObSPIParamList& add_int(int64_t value);
  ObSPIParamList& add_uint(uint64_t value);
  ObSPIParamList& add_float(float value);
  ObSPIParamList& add_double(double value);
  ObSPIParamList& add_string(const char* value);
  ObSPIParamList& add_string(const ObString& value);
  ObSPIParamList& add_datetime(int64_t usec);
  ObSPIParamList& add_number(const number::ObNumber& num);
  ObSPIParamList& add_param(const ObSPIParam& param);

  // Access
  int64_t count() const;
  const ObSPIParam& at(int64_t idx) const;

  // Convert to internal format
  int to_param_store(ParamStore& param_store) const;

  void reset();
};
```

### 2.3 New SPI Interfaces

```cpp
class ObSPIService {
public:
  // Parameterized query (SELECT)
  static int spi_query_with_params(
      pl::ObPLExecCtx *ctx,
      const char* sql,
      const ObSPIParamList& params,
      int64_t type,
      const ObSqlExpression **into_exprs = NULL,
      int64_t into_count = 0,
      const ObDataType *column_types = NULL,
      int64_t type_count = 0,
      const bool *exprs_not_null_flag = NULL,
      const int64_t *pl_integer_ranges = NULL,
      bool is_bulk = false,
      bool is_type_record = false,
      bool for_update = false);

  // Parameterized execution (INSERT/UPDATE/DELETE)
  static int spi_execute_with_params(
      pl::ObPLExecCtx *ctx,
      const char* sql,
      const ObSPIParamList& params,
      int64_t& affected_rows);

  // Extended interface with OUT parameters
  static int spi_execute_with_params_ex(
      pl::ObPLExecCtx *ctx,
      const char* sql,
      ObSPIParamList& params,
      int64_t& affected_rows,
      const ObSqlExpression **into_exprs = NULL,
      int64_t into_count = 0,
      const ObDataType *column_types = NULL,
      int64_t type_count = 0,
      const bool *exprs_not_null_flag = NULL,
      const int64_t *pl_integer_ranges = NULL,
      bool is_returning = false);

  // Note: OUT/INOUT parameters are reserved for a later phase.

private:
  static int validate_param_security(const ObSPIParamList& params);
};
```

## 3. Supported Data Types

| C++ Type | OB Type | MySQL Type | Factory Method |
|----------|---------|------------|----------------|
| `NULL` | `ObNullType` | `MYSQL_TYPE_NULL` | `ObSPIParam::null()` |
| `int8_t` | `ObTinyIntType` | `MYSQL_TYPE_TINY` | `from_int()` |
| `int32_t` | `ObInt32Type` | `MYSQL_TYPE_LONG` | `from_int()` |
| `int64_t` | `ObIntType` | `MYSQL_TYPE_LONGLONG` | `from_int()` |
| `uint64_t` | `ObUInt64Type` | `MYSQL_TYPE_LONGLONG` | `from_uint()` |
| `float` | `ObFloatType` | `MYSQL_TYPE_FLOAT` | `from_float()` |
| `double` | `ObDoubleType` | `MYSQL_TYPE_DOUBLE` | `from_double()` |
| `ObNumber` | `ObNumberType` | `MYSQL_TYPE_NEWDECIMAL` | `from_number()` |
| `int64_t(usec)` | `ObDateTimeType` | `MYSQL_TYPE_DATETIME` | `from_datetime()` |
| `const char*` | `ObVarcharType` | `MYSQL_TYPE_VAR_STRING` | `from_string()` |
| `ObString` | `ObVarcharType` | `MYSQL_TYPE_VAR_STRING` | `from_string()` |
| `void*,len` | `ObLongTextType` | `MYSQL_TYPE_BLOB` | `from_blob()` |

## 4. Implementation Details

### 4.1 SQL Preparation and Placeholder Validation

```cpp
// Use the existing prepare_dynamic path to let the SQL engine
// validate placeholder count and produce ps_sql.
prepare_dynamic(ctx, allocator, is_returning, false, param_count, sql_str,
                ps_sql, stmt_type, for_update, hidden_rowid, into_cnt, skip_locked, NULL);
```

### 4.2 Parameter Binding Path

- Build an `ObObjParam**` array from `ObSPIParamList` using `deep_copy_objparam`.
- Reuse the existing dynamic SQL parameter preparation path to avoid ParamStore
  type mismatches.

### 4.3 Security Validation

- Reject `ObExtendType` parameters (may contain unsafe pointers)
- Enforce maximum string length (`OB_MAX_VARCHAR_LENGTH`) for non-LOB string types only
- Use `deep_copy_objparam` for any parameter that needs deep copy
- BLOB parameters are stored with binary collation
- OUT/INOUT parameters are rejected in this phase

## 5. Error Codes

| Code | Name | Message |
|------|------|---------|
| -5053 | `OB_ERR_WRONG_DYNAMIC_PARAM` | Incorrect number of parameters |
| -4007 | `OB_NOT_SUPPORTED` | OUT/INOUT parameters are not supported yet |
| -4019 | `OB_SIZE_OVERFLOW` | String parameter exceeds `OB_MAX_VARCHAR_LENGTH` |

## 6. Usage Examples

### 6.1 Basic Query

```cpp
void example_query(pl::ObPLExecCtx *ctx) {
  ObArenaAllocator allocator;
  ObSPIParamList params(allocator);
  params.add_int(1001)
        .add_string("Engineering")
        .add_double(50000.00);

  int ret = ObSPIService::spi_query_with_params(ctx,
      "SELECT name, salary FROM employees "
      "WHERE id = ? AND department = ? AND salary >= ?",
      params, stmt::T_SELECT,
      into_exprs, into_count, column_types, type_count,
      NULL, NULL, false, false, false);
}
```

### 6.2 Safe Insert (SQL Injection Prevention)

```cpp
void example_safe_insert(pl::ObPLExecCtx *ctx) {
  ObArenaAllocator allocator;

  const char* company_name = "John's Company";  // Contains single quote
  ObSPIParamList params(allocator);
  params.add_string(company_name)  // Automatically safe
        .add_int(100)
        .add_null();

  int64_t affected_rows = 0;
  int ret = ObSPIService::spi_execute_with_params(ctx,
      "INSERT INTO companies (name, employee_count, notes) VALUES (?, ?, ?)",
      params, affected_rows);
}
```

## 7. Testing Plan

### 7.1 Unit Tests

- Test all parameter types
- Test SQL injection prevention
- Test placeholder count validation
- Test error handling

### 7.2 Integration Tests

- Test with actual database operations
- Test with complex queries
- Test with stored procedures

## 8. Compatibility

- Fully backward compatible with existing interfaces
- No changes to existing APIs
- New interfaces are additions only
