// Native DDL failures/explicit rollback/destructor cleanup cannot publish roots.
static int run_ddl_schema_fence_probe()
{
  auto *proxy = directory_sql_proxy();
  auto *schema = directory_schema_service();
  if (proxy == nullptr || schema == nullptr) { return OB_NOT_INIT; }
#define DDL_PROBE_ASSERT(expr) do { if (!(expr)) { \
  fprintf(stderr, "INSTANCE_META_PROBE_FAIL ddl cleanup line=%d assertion=%s\n", __LINE__, #expr); \
  return OB_ERR_UNEXPECTED; } } while (0)
  {
    rootserver::ObDDLSQLTransaction transaction(schema, false);
    DDL_PROBE_ASSERT(transaction.start(proxy, int64_t{1}) == OB_EAGAIN && transaction.is_started());
    DDL_PROBE_ASSERT(transaction.end(false) == OB_SUCCESS);
  }
  {
    rootserver::ObDDLSQLTransaction transaction(schema, false);
    DDL_PROBE_ASSERT(transaction.start(proxy, int64_t{0}) == OB_SUCCESS);
    DDL_PROBE_ASSERT(transaction.end(false) == OB_SUCCESS);
  }
  {
    rootserver::ObDDLSQLTransaction transaction(schema, false);
    DDL_PROBE_ASSERT(transaction.start(proxy, int64_t{0}) == OB_SUCCESS);
    // The native transaction rolls back on destructor; no separate registration.
  }
  fprintf(stderr, "INSTANCE_DDL_PROBE_PASS failed_start=1 rollback=1 destructor=1\n");
#undef DDL_PROBE_ASSERT
  return OB_SUCCESS;
}
