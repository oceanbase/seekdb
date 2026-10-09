// Only included by a native integration test build.
static int run_local_storage_schema_native_probe()
{
  ObArenaAllocator allocator(ObMemAttr("LocalSchemaTest"));
  int ret = OB_SUCCESS;
#define LOCAL_SCHEMA_CALL(expr) do { ret = (expr); if (ret != OB_SUCCESS) { \
  fprintf(stderr, "LOCAL_SCHEMA_FAIL line=%d ret=%d expr=%s\n", __LINE__, ret, #expr); return ret; } } while (0)
#define LOCAL_SCHEMA_CHECK(expr) do { if (!(expr)) { \
  fprintf(stderr, "LOCAL_SCHEMA_FAIL line=%d check=%s\n", __LINE__, #expr); return OB_ERR_UNEXPECTED; } } while (0)
  auto definition = [&](int64_t version, ObStorageSchema &physical) -> int {
    ObTableSchema sql(&allocator);
    int rc = InstanceMetaStore::build_schema(ObTabletID(900000002), sql);
    sql.set_schema_version(version);
    sql.set_block_size(version == 10 ? 16384 : 32768);
    ObColumnSchemaV2 column;
    column.set_table_id(sql.get_table_id());
    column.set_column_id(OB_APP_MIN_COLUMN_ID + 3);
    column.set_schema_version(version);
    column.set_nullable(false);
    column.set_data_type(ObIntType);
    ObObj value;
    value.set_int(version);
    if (rc == OB_SUCCESS) { rc = column.set_column_name("layout_default"); }
    if (rc == OB_SUCCESS) { rc = column.set_orig_default_value(value); }
    if (rc == OB_SUCCESS) { rc = column.set_cur_default_value(value, false); }
    if (rc == OB_SUCCESS) { rc = sql.add_column(column); }
    sql.set_max_used_column_id(column.get_column_id());
    if (rc == OB_SUCCESS) { rc = physical.init(allocator, sql, false); }
    return rc;
  };
  auto same = [&](const ObStorageSchema &left, const ObStorageSchema &right) -> bool {
    std::string a(left.get_serialize_size(), '\0'), b(right.get_serialize_size(), '\0');
    int64_t a_pos = 0, b_pos = 0;
    return left.serialize(&a[0], a.size(), a_pos) == OB_SUCCESS
        && right.serialize(&b[0], b.size(), b_pos) == OB_SUCCESS && a == b && a_pos == b_pos;
  };
  auto install = [&](const char *name, const ObStorageSchema &local,
      const ObStorageSchema &incoming, const ObStorageSchema &expected) -> int {
    ObStorageSchema *result = nullptr;
    int rc = ObStorageSchemaUtil::update_tablet_storage_schema(
        ObTabletID(900000002), allocator, local, incoming, result);
    if (rc == OB_SUCCESS && !same(*result, expected)) {
      fprintf(stderr, "LOCAL_SCHEMA_FAIL case=%s local_v=%ld incoming_v=%ld result_v=%ld result_simplified=%d\n",
          name, local.get_schema_version(), incoming.get_schema_version(), result->get_schema_version(),
          result->is_column_info_simplified());
      rc = OB_ERR_UNEXPECTED;
    }
    if (rc == OB_SUCCESS) {
      std::string bytes(result->get_serialize_size(), '\0');
      int64_t pos = 0;
      rc = result->serialize(&bytes[0], bytes.size(), pos);
      ObStorageSchema restored;
      pos = 0;
      if (rc == OB_SUCCESS) { rc = restored.deserialize(allocator, bytes.data(), bytes.size(), pos); }
      if (rc == OB_SUCCESS && !same(restored, expected)) { rc = OB_ERR_UNEXPECTED; }
    }
    ObStorageSchemaUtil::free_storage_schema(allocator, result);
    if (rc == OB_SUCCESS) { fprintf(stderr, "LOCAL_SCHEMA_CASE_PASS case=%s\n", name); }
    return rc;
  };
  ObStorageSchema old_full, new_full, new_simplified;
  LOCAL_SCHEMA_CALL(definition(10, old_full));
  LOCAL_SCHEMA_CALL(definition(20, new_full));
  LOCAL_SCHEMA_CALL(new_simplified.init(allocator, new_full, true));
  LOCAL_SCHEMA_CHECK(old_full.get_column_count() == new_full.get_column_count());
  LOCAL_SCHEMA_CALL(install("old_merge_after_new_layout", new_full, old_full, new_full));
  LOCAL_SCHEMA_CALL(install("new_layout_after_old_merge", old_full, new_full, new_full));
  LOCAL_SCHEMA_CALL(install("same_version_fill_columns", new_simplified, new_full, new_full));
  LOCAL_SCHEMA_CALL(install("same_version_keep_columns", new_full, new_simplified, new_full));
  LOCAL_SCHEMA_CALL(install("old_merge_after_new_simplified", new_simplified, old_full, new_simplified));
  LOCAL_SCHEMA_CALL(install("new_simplified_after_old_merge", old_full, new_simplified, new_simplified));
  fprintf(stderr, "LOCAL_SCHEMA_PASS version_body=1 same_version_full=1 simplified=1 serialization=1\n");
#undef LOCAL_SCHEMA_CALL
#undef LOCAL_SCHEMA_CHECK
  return OB_SUCCESS;
}
