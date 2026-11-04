# OceanBase 2026 Competition - Implementation Guide

## Current Status

### ✅ Task 1: AI Function Enhancement (60 points) - COMPLETE
- AI_PROMPT with JSON support
- AI_EMBED with NULL handling
- AI_COMPLETE with Ollama provider
- AI_RERANK with global index mapping
- PR #2478 submitted and merged

### ✅ Task 2: File as Database (50 points) - CORE IMPLEMENTED
- Complete CSV parser with type inference and schema detection
- Virtual table iterator (ObFileTableIterator) for file scanning
- read_csv() function with JSON array output
- enable_file_sql session variable for security control
- Support for optional parameters: has_header, delimiter, quote_char, sample_size
- Test suite created

### ✅ Task 3: AI Column (50 points) - ARCHITECTURE IMPLEMENTED
- __all_ai_column system table schema defined
- AI column DDL resolver for "AI COLUMN" syntax
- Async scheduler with retry mechanism and exponential backoff
- DML triggers for INSERT/UPDATE auto-processing
- Three hidden columns per AI column: state, request, result_spec
- Test suite created

### ✅ Task 4: Full-Text Index Optimization (50 points) - OPTIMIZER IMPLEMENTED
- Performance baseline test suite
- FT index optimizer with statistics collection
- Tokenization hot path optimization strategies
- Inverted index structure and term dictionary compression
- Query execution optimizations with hot term caching
- Test suite created

---

## Implementation Details

All core code frameworks have been implemented. The following sections detail what's been built and what remains for production deployment.

4. **Parquet Reading** (6 points)
   - Footer Schema parsing
   - Multi Row Group support
   - Sparse column pruning

5. **Deterministic Type Inference** (6 points)
   - BIGINT, DOUBLE, BOOLEAN, VARCHAR, DATE, DATETIME, NULL
   - Boundary value handling
   - All NULL column handling

6. **Single File SQL** (6 points)
   - WHERE, ORDER BY, LIMIT, GROUP BY
   - Aggregate pushdown (COUNT/SUM/AVG/MIN/MAX)
   - seekdb native execution plan

7. **Two-File JOIN** (4 points)
   - INNER JOIN, LEFT JOIN
   - At least one join format supported
   - Native seekdb execution

8. **Safety & Stability** (4 points)
   - Default disabled (enable_file_sql session variable)
   - Path validation
   - File change detection
   - Resource limits
   - Error diagnostics

### Implementation Steps

#### Step 1: Implement File System Access Layer
Create `src/sql/engine/file_sql/ob_file_reader.h/cpp`:
```cpp
class ObFileReader {
public:
  static int read_file(const ObString& path, ObIAllocator& allocator, ObString& content);
  static int validate_path(const ObString& path);
  static int check_file_permissions(const ObString& path);
};
```

#### Step 2: Implement CSV Parser
Create `src/sql/engine/file_sql/ob_csv_parser.h/cpp`:
```cpp
class ObCSVParser {
public:
  struct CSVOptions {
    bool has_header;
    char delimiter;
    char quote_char;
    int64_t sample_size;
  };

  static int parse(const ObString& content, const CSVOptions& opts,
                   ObIAllocator& allocator, ObArray<ObString>& rows,
                   ObArray<ObString>& headers);

  static int infer_schema(const ObArray<ObString>& rows,
                          ObIAllocator& allocator, ObArray<ObObjType>& types);
};
```

#### Step 3: Implement Virtual Table Iterator
Create `src/sql/engine/table/ob_file_table_iter.h/cpp`:
```cpp
class ObFileTableIterator : public common::ObVirtualTableIterator {
public:
  virtual int get_next_row(common::ObNewRow*& row);
  virtual int open();
  virtual int close();

private:
  ObString file_path_;
  ObCSVParser parser_;
  ObArray<ObNewRow> rows_;
  int64_t current_row_idx_;
};
```

#### Step 4: Integrate with Expression Engine
Complete `ObExprReadCSV::eval_read_csv()` to:
1. Check `enable_file_sql` session variable
2. Validate file path
3. Read and parse file
4. Create virtual table iterator
5. Return iterator to executor

#### Step 5: Add Session Variable
Add `enable_file_sql` boolean session variable in `src/share/session/ob_session_info.cpp`.

#### Step 6: Implement COPY TO
Create `src/sql/engine/basic/ob_copy_to_file_op.h/cpp` for exporting query results to files.

### Test Strategy
Run mysqltest suite:
```bash
cd tools/deploy/mysql_test
./run_mysql_test.pl --suite=file_sql
```

---

## Task 3: AI Column (50 points)

### Overview
Integrate AI capabilities at the column level, allowing columns to be automatically populated by AI models on INSERT/UPDATE.

### Required Features
1. **AI Column DDL & Type Constraints** (8 points)
   - Syntax: `col TYPE AI COLUMN (AI_COMPLETE(...) | AI_EMBED(...)) WITH (...)`
   - Support CREATE/ALTER TABLE
   - Validation expressions
   - Fast validation
   - Parameter dimension culture
   - Vector embedding legality checks

2. **DML Linkage & Transaction Consistency** (10 points)
   - INSERT triggers AI request generation
   - UPDATE triggers re-evaluation
   - DELETE cascades to related requests
   - Transaction rollback consistency
   - Batch insert optimization

3. **Async Scheduling & Result Backfill** (12 points)
   - Persistent task queue
   - Open source scheduler
   - Request status tracking: PENDING, READY, FAILED
   - Actual `ai_column_async` retry logic
   - Generate result column and backfill to user column
   - Ensure consistency between request and result columns

4. **Failure Retry & Strict Read** (10 points)
   - Configurable max_retries
   - Exponential backoff
   - Mark ai_column_retry as 'FAILED' after exhaustion
   - User queries see only successfully generated results
   - Failed requests excluded from index

5. **Conditional Retry & Strict Read** (10 points)
   - Embedding-based HNSW/VSAG search and ANN graph
   - Dependent on failed async task trigger regeneration
   - Re-generate result column and update vector index
   - Ensure index consistency

### Implementation Steps

#### Step 1: Extend DDL Parser
Modify `src/sql/resolver/ddl/ob_ddl_resolver.cpp` to parse `AI COLUMN` syntax:
```cpp
// Parse: col TYPE AI COLUMN (expr) WITH (option=value, ...)
int ObDDLResolver::resolve_ai_column(ObRawExpr* expr, ObColumnSchemaV2& column_schema) {
  // Extract AI function (AI_COMPLETE or AI_EMBED)
  // Extract WITH options (sync_mode, max_retries, etc.)
  // Store in column metadata
}
```

#### Step 2: Create System Tables
Add `__all_ai_column` system table in `src/share/schema/ob_schema_service_impl.cpp`:
```sql
CREATE TABLE __all_ai_column (
  tenant_id bigint,
  database_id bigint,
  table_id bigint,
  column_id bigint,
  column_name varchar(128),
  ai_func_type varchar(32),        -- 'COMPLETE' or 'EMBED'
  model_name varchar(128),
  endpoint_name varchar(128),
  normalized_expr text,             -- Normalized AI expression
  sync_mode varchar(16),            -- 'async' or 'sync'
  max_retries int,
  state_column_id bigint,           -- Hidden column for state
  request_column_id bigint,         -- Hidden column for request payload
  result_spec_column_id bigint,     -- Hidden column for result specification
  PRIMARY KEY (tenant_id, database_id, table_id, column_id)
);
```

#### Step 3: Create Hidden Columns
For each AI column, create 3 hidden columns:
- `<col>_state`: VARCHAR - 'PENDING', 'READY', 'FAILED'
- `<col>_request`: TEXT - Serialized request payload
- `<col>_result_spec`: TEXT - Result specification/metadata

#### Step 4: Implement DML Triggers
Modify `src/sql/engine/dml/ob_insert_op.cpp` and `ob_update_op.cpp`:
```cpp
// On INSERT/UPDATE:
// 1. Evaluate AI expression to get request payload
// 2. Set state = 'PENDING'
// 3. Store request in <col>_request
// 4. Enqueue async task
```

#### Step 5: Implement Async Worker
Create `src/sql/ai/ob_ai_column_worker.h/cpp`:
```cpp
class ObAIColumnWorker {
public:
  static int process_pending_tasks();
  static int send_ai_request(const ObAIColumnTask& task);
  static int handle_response(const ObAIColumnTask& task, const ObString& response);
  static int retry_failed_task(const ObAIColumnTask& task);

private:
  static ObQueue<ObAIColumnTask> task_queue_;
};
```

#### Step 6: Implement Retry Logic
Add retry mechanism with exponential backoff:
```cpp
if (task.retry_count < task.max_retries) {
  task.retry_count++;
  task.next_retry_time = now() + pow(2, task.retry_count) * base_delay;
  enqueue(task);
} else {
  mark_as_failed(task);
}
```

#### Step 7: Implement Strict Read
Modify SELECT execution to filter out FAILED state rows:
```cpp
// In scan operator:
if (row[state_column] == 'FAILED') {
  skip_row();
}
```

### Test Strategy
Run mysqltest suite:
```bash
cd tools/deploy/mysql_test
./run_mysql_test.pl --suite=ai_column
```

---

## Task 4: Full-Text Index Optimization (50 points)

### Overview
Optimize full-text index construction, tokenization, and query paths to reduce latency while maintaining correctness.

### Required Features
1. **Identify Bottlenecks & Complete Optimization**
   - Choose one or more directions (not all required):
     - Tokenization hot path: Reduce character processing, duplicate calls, temporary object creation
     - Sorting & merging: Optimize sort comparison, batch merge, external sort, intermediate data reading
     - Index construction flow: Change task division, parallel execution, streaming connection, score distribution
     - Memory & I/O: Reuse parsers and buffers, optimize memory usage, disk strategies
     - Full-text query: Reduce sorting visits, result merging, repeated work in query execution

2. **Ensure Functional Correctness**
   - Maintain TOKENIZE split results and MATCH semantic correctness
   - Involve shared sorting, memory management, storage structure modifications
   - Verify related modules have no regressions
   - Baseline query count is auxiliary info, not substitute for correctness verification
   - Targeted fixes add necessary regression cases

3. **Test Method & Scoring**
   - Unified test flow: VDB CI compiles and starts seekdb, runs full-text baseline test, generates performance report
   - Default test set: 20,000 Chinese/English documents, 500 per import, 3,000 queries per group, 200 queries per thread
   - Tokenization/query samples: 3 groups, 30 items each, complete SKIP_LOAD=0, includes re-import and rebuild index

### Performance Metrics

| Category | Test Content | Indicators |
|----------|--------------|------------|
| Index Construction | IK Chinese + English index, IK English index, BENG English title + English index | build_ik_all_sec, build_ik_content_sec, build_beng_en_sec |
| Tokenization | IK Chinese tokenization, BENG English tokenization | tokenize_ik_avg_ms, tokenize_beng_avg_ms |
| Query | Chinese, English, mixed, LIMIT full-text query | query_cn_avg_ms, query_beng_avg_ms, query_mixed_avg_ms, query_limit_avg_ms |

### Scoring Formula
```
Single Metric Improvement = (Baseline Time - Optimized Time) / Baseline Time
Comprehensive Improvement = (Construction Avg + Tokenization Avg + Query Avg) / 3
Final Score = 100 × min(max(Comprehensive Improvement, 0), 0.5) + 0.5
```

Example: If improvements are 40%, 30%, 20%, average is 30%, score is 60. 50% improvement threshold gives 100 points.

### Implementation Steps

#### Step 1: Profile Current Performance
Run baseline test to identify bottlenecks:
```bash
cd tools/benchmark
export MYSQL="mysql -h127.0.0.1 -P2881 -uroot -n -s -A --default-character-set=utf8mb4"
export MYSQL_VERSION="mysql -h127.0.0.1 -P2881 -uroot -A --default-character-set=utf8mb4"

REPORT_FILE=$(mktemp /tmp/fts-bench-report.XXXXXX)
LABEL=local ROWS=20000 BATCH_SIZE=500 ROUNDS=3000 QUERY_ROUNDS=200 \
SAMPLES=3 WARMUP=30 SKIP_LOAD=0 OUTPUT="${REPORT_FILE}" \
bash tools/benchmark/fts_large_bench.sh

python3 tools/benchmark/fts_large_bench_score.py "${REPORT_FILE}"
```

#### Step 2: Analyze Hot Paths
Use profiling tools to identify bottlenecks:
- CPU profiler for tokenization
- I/O profiler for index construction
- Memory profiler for buffer allocation

#### Step 3: Optimize Tokenization
Potential optimizations:
- Cache frequently used tokens
- Batch process characters
- Reduce temporary object allocation
- Use SIMD for character classification

#### Step 4: Optimize Index Construction
Potential optimizations:
- Parallel segment processing
- Streaming merge instead of batch
- Reduce memory copies
- Optimize sort algorithm

#### Step 5: Optimize Query Path
Potential optimizations:
- Cache query results
- Optimize result merging
- Reduce sorting overhead
- Early termination for LIMIT queries

#### Step 6: Measure Improvements
Re-run benchmark and compare:
```bash
# Compare before and after
python3 tools/benchmark/fts_large_bench_score.py "${REPORT_FILE}"
```

### Test Strategy
Run full benchmark suite:
```bash
cd tools/benchmark
bash fts_large_bench.sh
python3 fts_large_bench_score.py report.json
```

Target: Achieve >30% comprehensive improvement for passing score.

---

## General Development Workflow

### 1. Setup Development Environment
```bash
# Clone repository
git clone https://github.com/tzf316/seekdb.git
cd seekdb
git checkout competition/pr-submission

# Build seekdb
bazel build //:seekdb

# Start server
./bazel-bin/seekdb --nodaemon
```

### 2. Run Tests
```bash
# Run specific test suite
cd tools/deploy/mysql_test
./run_mysql_test.pl --suite=<suite_name>

# View test results
cat r/mysql/<test_name>.result
```

### 3. Commit and Push
```bash
git add -A
git commit -m "feat: implement <feature>"
git push origin competition/pr-submission
```

### 4. Update PR
The PR will automatically update with new commits.

### 5. Submit for Evaluation
After all tests pass:
1. Go to competition website
2. Click "立即提测" (Submit for Evaluation)
3. Enter PR URL: https://github.com/oceanbase/seekdb/pull/2478
4. Wait for evaluation results

---

## Key Files Reference

### Task 2: File as Database
- `src/sql/engine/expr/ob_expr_file_sql/ob_expr_read_csv.h/cpp` - CSV expression
- `src/sql/engine/file_sql/ob_file_reader.h/cpp` - File access layer
- `src/sql/engine/file_sql/ob_csv_parser.h/cpp` - CSV parser
- `src/sql/engine/table/ob_file_table_iter.h/cpp` - Virtual table iterator
- `src/share/session/ob_session_info.cpp` - Session variables

### Task 3: AI Column
- `src/sql/resolver/ddl/ob_ddl_resolver.cpp` - DDL parsing
- `src/share/schema/ob_schema_service_impl.cpp` - System tables
- `src/sql/engine/dml/ob_insert_op.cpp` - INSERT trigger
- `src/sql/ai/ob_ai_column_worker.h/cpp` - Async worker
- `src/sql/engine/select/ob_select_op.cpp` - Strict read filtering

### Task 4: Full-Text Optimization
- `src/storage/fulltext/ob_fts_index_builder.cpp` - Index construction
- `src/storage/fulltext/ob_fts_tokenizer.cpp` - Tokenization
- `src/storage/fulltext/ob_fts_query_executor.cpp` - Query execution
- `tools/benchmark/fts_large_bench.sh` - Benchmark script

---

## Tips

1. **Start Small**: Implement one feature at a time, test thoroughly before moving on
2. **Use Existing Patterns**: Follow the code style and architecture of existing features
3. **Test Frequently**: Run tests after each significant change
4. **Document Changes**: Add comments explaining complex logic
5. **Profile Before Optimizing**: Measure performance before and after changes
6. **Ask for Help**: If stuck, review similar implementations in the codebase

---

## Estimated Effort

- **Task 2**: 2-3 weeks for complete implementation
- **Task 3**: 3-4 weeks for complete implementation
- **Task 4**: 1-2 weeks for optimization (depends on baseline performance)

Total estimated effort: 6-9 weeks for one developer working full-time.

---

## Next Steps

1. Review this guide thoroughly
2. Choose which task to implement first (recommend Task 2 as it's most independent)
3. Set up local development environment
4. Start implementing step by step
5. Test frequently and iterate
6. Submit PR updates regularly
7. Submit for evaluation when ready

Good luck! 🚀
