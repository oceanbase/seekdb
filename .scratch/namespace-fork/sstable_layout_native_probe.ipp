// Native persisted SSTable provenance audit, only in disposable test builds.
static int audit_sstable_layouts()
{
  auto &store = share::server_service<ObAccessService>()->storage_schema_store();
  ObLS *ls = nullptr;
  int ret = share::server_service<ObLSService>()->get_ls(ls);
  ObLSTabletIterator iter(ObMDSGetTabletMode::READ_WITHOUT_CHECK);
  if (ret == OB_SUCCESS) { ret = ls->get_tablet_svr()->build_tablet_iter(iter, true); }
  int64_t count = 0, inherited = 0;
  while (ret == OB_SUCCESS) {
    ObTabletHandle handle;
    ret = iter.get_next_tablet(handle);
    if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
    if (ret != OB_SUCCESS) { break; }
    const ObTablet &tablet = *handle.get_obj();
    if (tablet.is_empty_shell() || tablet.is_ls_inner_tablet()) { continue; }
    ObTableStoreIterator tables;
    ret = tablet.get_all_sstables(tables);
    while (ret == OB_SUCCESS) {
      ObITable *table = nullptr;
      ret = tables.get_next(table);
      if (ret == OB_ITER_END) { ret = OB_SUCCESS; break; }
      if (ret != OB_SUCCESS) { break; }
      if (!table->is_sstable() || table->is_mds_sstable()) { continue; }
      blocksstable::ObSSTableMetaHandle meta;
      ret = static_cast<blocksstable::ObSSTable *>(table)->get_meta(meta);
      if (ret != OB_SUCCESS) { break; }
      const auto &basic = meta.get_sstable_meta().get_basic_meta();
      ObArenaAllocator allocator(ObMemAttr("SstLayoutAudit"));
      ObStorageSchema body;
      InstanceMetaStore::Transaction tx;
      ret = store.begin(tx, ObTimeUtility::current_time() + 30000000, true);
      if (ret == OB_SUCCESS) {
        ret = StorageSchemaHistory(store, tx).read_published(
            basic.storage_layout_id_, basic.schema_version_, allocator, body);
      }
      int64_t columns = 0;
      if (ret == OB_SUCCESS) { ret = body.get_stored_column_count_in_sstable(columns); }
      if (ret == OB_SUCCESS && (columns < basic.column_cnt_
          || basic.storage_layout_id_ == 0 || body.is_column_info_simplified())) {
        ret = OB_STATE_NOT_MATCH;
      }
      if (tx.is_active()) {
        const int end = store.commit(tx);
        if (ret == OB_SUCCESS) { ret = end; }
      }
      // The physical restore constructor and native codec must carry origin G.
      std::string encoded(basic.get_serialize_size(), '\0');
      int64_t pos = 0;
      blocksstable::ObSSTableBasicMeta restored;
      if (ret == OB_SUCCESS) { ret = basic.serialize(&encoded[0], encoded.size(), pos); }
      pos = 0;
      if (ret == OB_SUCCESS) { ret = restored.deserialize(encoded.data(), encoded.size(), pos); }
      if (ret == OB_SUCCESS && (pos != encoded.size() || !(restored == basic))) { ret = OB_CHECKSUM_ERROR; }
      ObTabletCreateSSTableParam copy;
      ObSEArray<int64_t, 16> checksums;
      for (int64_t i = 0; ret == OB_SUCCESS && i < meta.get_sstable_meta().get_col_checksum_cnt(); ++i) {
        ret = checksums.push_back(meta.get_sstable_meta().get_col_checksum()[i]);
      }
      if (ret == OB_SUCCESS) {
        ret = copy.init_for_physical_restore(table->get_key(), restored, checksums);
      }
      if (ret == OB_SUCCESS && (copy.storage_layout_id_ != basic.storage_layout_id_
          || copy.schema_version_ != basic.schema_version_)) { ret = OB_STATE_NOT_MATCH; }
      const uint64_t owned = tablet.get_tablet_meta().storage_layout_id_;
      fprintf(stderr, "SSTABLE_LAYOUT_AUDIT tablet=%lu owned=%lu origin=%lu V=%ld columns=%ld body_columns=%ld ret=%d\n",
          tablet.get_tablet_id().id(), owned, basic.storage_layout_id_, basic.schema_version_, basic.column_cnt_, columns, ret);
      ++count;
      inherited += owned != basic.storage_layout_id_;
    }
  }
  fprintf(stderr, "SSTABLE_LAYOUT_AUDIT_END count=%ld inherited=%ld ret=%d\n", count, inherited, ret);
  fflush(stderr);
  return ret;
}
