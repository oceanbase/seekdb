// Local-only large-partition preparation regression; no physical tablet creation.
static int run_tablet_binding_native_probe()
{
  ObArenaAllocator allocator("BindingProbe");
  ObTableSchema sql_definition(&allocator);
  int descriptor_ret = InstanceMetaStore::build_schema(ObTabletID(900001), sql_definition);
  ObCreateTabletSchema physical, copied, decoded;
  if (descriptor_ret == OB_SUCCESS) { descriptor_ret = physical.init(allocator, sql_definition, false); }
  if (descriptor_ret == OB_SUCCESS) { descriptor_ret = copied.init(allocator, physical); }
  if (descriptor_ret != OB_SUCCESS) {
    fprintf(stderr, "INSTANCE_META_PROBE_FAIL creation_descriptor_copy ret=%d\n", descriptor_ret);
    return descriptor_ret;
  }
  std::string serialized(physical.get_serialize_size(), '\0');
  int64_t pos = 0;
  descriptor_ret = physical.serialize(&serialized[0], serialized.size(), pos);
  if (descriptor_ret == OB_SUCCESS) {
    int64_t read_pos = 0;
    descriptor_ret = decoded.deserialize(allocator, serialized.data(), serialized.size(), read_pos);
    if (descriptor_ret == OB_SUCCESS && (read_pos != pos || !decoded.is_valid()
        || !copied.is_valid() || copied.get_table_id() != physical.get_table_id()
        || decoded.get_table_id() != physical.get_table_id()
        || copied.get_schema_version() != physical.get_schema_version())) {
      descriptor_ret = OB_ERR_UNEXPECTED;
    }
  }
  if (descriptor_ret != OB_SUCCESS) {
    fprintf(stderr, "INSTANCE_META_PROBE_FAIL creation_descriptor_codec ret=%d\n", descriptor_ret);
    return descriptor_ret;
  }
  fprintf(stderr, "INSTANCE_CREATION_DESCRIPTOR_PASS copy=1 codec=1 table_id=%lu\n",
      copied.get_table_id());
  rootserver::TableCreationDescriptor definition, restored;
  std::string description;
  sql_definition.set_data_table_id(910001);
  sql_definition.set_aux_lob_meta_tid(910002);
  sql_definition.set_aux_lob_piece_tid(910003);
  descriptor_ret = definition.init(sql_definition, DATA_CURRENT_VERSION);
  if (descriptor_ret == OB_SUCCESS) { descriptor_ret = definition.encode(description); }
  if (descriptor_ret == OB_SUCCESS) {
    auto &store = share::server_service<ObAccessService>()->instance_meta_store();
    InstanceMetaStore::Transaction tx;
    descriptor_ret = store.begin(tx, ObTimeUtility::current_time() + 120000000);
    if (descriptor_ret == OB_SUCCESS) {
      rootserver::InstanceNamespaceMetadata metadata(store, tx);
      uint64_t object = 0;
      std::string loaded;
      descriptor_ret = metadata.save_object(description, object);
      if (descriptor_ret == OB_SUCCESS) { descriptor_ret = metadata.read_object(object, loaded); }
      if (descriptor_ret == OB_SUCCESS) { descriptor_ret = restored.decode(loaded); }
      if (descriptor_ret == OB_SUCCESS && (restored.data_table_id() != 910001
          || restored.lob_meta_table_id() != 910002 || restored.lob_piece_table_id() != 910003)) {
        descriptor_ret = OB_ERR_UNEXPECTED;
      }
      const int end = store.rollback(tx);
      if (descriptor_ret == OB_SUCCESS) { descriptor_ret = end; }
    }
  }
  definition.reset();
  sql_definition.reset();
  obcall::ObBatchCreateTabletArg batch;
  int64_t index = -1;
  if (descriptor_ret == OB_SUCCESS) { descriptor_ret = batch.init_create_tablet(SCN::min_scn(), false); }
  if (descriptor_ret == OB_SUCCESS) { descriptor_ret = restored.append_to(batch, index); }
  restored.reset();
  if (descriptor_ret == OB_SUCCESS && (index != 0 || batch.create_tablet_schemas_.count() != 1
      || !batch.create_tablet_schemas_.at(0)->is_valid()
      || batch.create_tablet_schemas_.at(0)->get_table_id() != copied.get_table_id()
      || batch.tablet_extra_infos_.count() != 1
      || batch.tablet_extra_infos_.at(0).data_format_version_ != DATA_CURRENT_VERSION)) {
    descriptor_ret = OB_ERR_UNEXPECTED;
  }
  batch.reset();
  if (descriptor_ret == OB_SUCCESS) {
    const std::string malformed[] = {std::string(), description.substr(0, description.size() - 1),
        description + "x", "wrongtag" + description.substr(8)};
    for (const auto &bytes : malformed) {
      if (restored.decode(bytes) == OB_SUCCESS || restored.is_valid()) {
        descriptor_ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
  }
  if (descriptor_ret != OB_SUCCESS) {
    fprintf(stderr, "INSTANCE_META_PROBE_FAIL persisted_creation_descriptor ret=%d\n", descriptor_ret);
    return descriptor_ret;
  }
  fprintf(stderr, "INSTANCE_PERSISTED_CREATION_DESCRIPTOR_PASS kv=1 sql_released=1 batch_owned=1 malformed=1\n");
  ObTableSchema schema(&allocator);
  int ret = InstanceMetaStore::build_schema(ObTabletID(900001), schema);
  if (ret != OB_SUCCESS) { return ret; }
  schema.set_table_id(900001);
  schema.set_table_type(USER_TABLE);
  schema.set_part_level(PARTITION_LEVEL_ONE);
  schema.get_part_option().set_part_num(8000);
  schema.set_aux_lob_meta_tid(OB_INVALID_ID);
  schema.set_aux_lob_piece_tid(OB_INVALID_ID);
  std::string no_partitions, many_partitions;
  ret = definition.init(schema, DATA_CURRENT_VERSION);
  if (ret == OB_SUCCESS) { ret = definition.encode(no_partitions); }
  for (int64_t i = 0; OB_SUCC(ret) && i < 8000; ++i) {
    ObPartition partition(&allocator);
    partition.set_part_id(i);
    partition.set_tablet_id(ObTabletID(900000 + i));
    ret = schema.add_partition(partition);
  }
  if (OB_FAIL(ret)) { return ret; }
  ret = definition.init(schema, DATA_CURRENT_VERSION);
  if (ret == OB_SUCCESS) { ret = definition.encode(many_partitions); }
  if (ret != OB_SUCCESS || no_partitions != many_partitions) {
    fprintf(stderr, "INSTANCE_META_PROBE_FAIL descriptor_partition_amplification ret=%d\n", ret);
    return ret == OB_SUCCESS ? OB_ERR_UNEXPECTED : ret;
  }
  fprintf(stderr, "INSTANCE_DESCRIPTOR_PARTITIONS_PASS partitions=8000 descriptor_bytes=%lu\n", many_partitions.size());
  auto &store = share::server_service<ObAccessService>()->instance_meta_store();
  InstanceMetaStore::Transaction tx;
  ret = store.begin(tx, ObTimeUtility::current_time() + 120000000);
  if (ret == OB_SUCCESS) {
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    rootserver::InstanceCatalogPageStore pages(metadata);
    ns::NamespaceCatalogTree tree(pages);
    ns::CatalogChanges definitions, sources;
    ns::CatalogPageRef definition_root, source_root;
    uint64_t object = 0;
    ret = metadata.save_object(many_partitions, object);
    definitions[ns::NamespaceCatalogCodec::object_key(900001)] = {
        {ns::NamespaceCatalogCodec::encode_entry(object, 900001, 0), 0}, false};
    for (uint64_t i = 0; i < 8000; ++i) {
      ns::CatalogTabletSource source{900001, 1000000 + i, 123456, 900000 + i, 0, 0};
      sources[ns::NamespaceCatalogCodec::object_key(900000 + i)] = {
          {ns::NamespaceCatalogCodec::encode_source(source), 321}, false};
    }
    if (ret == OB_SUCCESS && !tree.apply({}, definitions, definition_root).ok()) { ret = OB_ERR_UNEXPECTED; }
    if (ret == OB_SUCCESS && !tree.apply({}, sources, source_root).ok()) { ret = OB_ERR_UNEXPECTED; }
    schema.reset(); // No runtime schema or partition enumeration survives.
    for (uint64_t i = 0; ret == OB_SUCCESS && i < 8000; ++i) {
      ns::CatalogTabletSource source;
      int64_t cap = 0;
      ret = metadata.find_tablet_source(source_root, 900000 + i, source, cap);
      if (ret == OB_SUCCESS && (source.data_tablet_id != 900000 + i || source.table_id != 900001
          || source.physical_tablet_id != 1000000 + i || source.create_transaction_id != 123456 || cap != 321)) {
        ret = OB_ERR_UNEXPECTED;
      }
      if (ret == OB_SUCCESS && i % 997 == 0) {
        std::string loaded;
        ret = metadata.read_table_definition(definition_root, source.table_id, loaded);
        if (ret == OB_SUCCESS && loaded != many_partitions) { ret = OB_ERR_UNEXPECTED; }
      }
    }
    const int end = store.rollback(tx);
    if (ret == OB_SUCCESS) { ret = end; }
  }
  if (ret == OB_SUCCESS) {
    fprintf(stderr, "INSTANCE_BINDING_PROBE_PASS partitions=8000 persisted_source=1 sql_released=1\n");
  } else {
    fprintf(stderr, "INSTANCE_META_PROBE_FAIL persisted_binding ret=%d\n", ret);
  }
  return ret;
}
