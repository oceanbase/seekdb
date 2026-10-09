from pathlib import Path
local = Path(__file__).resolve().parent
def cut(s, start, end):
    a=s.index(start); b=s.index(end,a)
    return s[:a]+s[b:]

p=local/'instance_namespace_metadata_probe.ipp'; s=p.read_text()
a=s.index('  ns::CatalogRoots snapshot = record.roots;')
b=s.index('  // A captured source',a)
s=s[:a]+'''  uint64_t rejected_id = 0;
  ret = meta.fork_namespace(record.name, "too-old",
      [&](int64_t &scn) { scn = watermark; return OB_SUCCESS; }, rejected_id);
  META_RECORD_ASSERT(ret == OB_SNAPSHOT_DISCARDED && rejected_id == 0);
  ret = meta.find_namespace("too-old", found);
  META_RECORD_ASSERT(ret == OB_ENTRY_NOT_EXIST);
  uint64_t child_id = 0;
  META_RECORD_CALL(meta.fork_namespace(record.name, "repo-child",
      [&](int64_t &scn) { scn = second_snapshot; return OB_SUCCESS; }, child_id));
  InstanceNamespaceRecord child;
  META_RECORD_CALL(meta.get_namespace(child_id, child));
  META_RECORD_ASSERT(child.id == id + 1 && child.parent_namespace == id
      && child.fork_cap == second_snapshot
      && child.roots.catalog.cap == first_snapshot
      && child.roots.directory.cap == first_snapshot);
''' + s[b:]
a=s.index('  META_RECORD_CALL(meta.get_snapshot(second_snapshot, loaded));')
b=s.index('  META_RECORD_CALL(meta.fork_namespace(record.name, "repo-auto",',a)
s=s[:a]+'  uint64_t automatic_child_id = 0;\n'+s[b:]
s=s.replace('  META_RECORD_CALL(meta.get_snapshot(first_snapshot, loaded));\n  META_RECORD_ASSERT(loaded.ref_count == 3);\n','')
s=cut(s,'  rootserver::InstanceNamespacePin absent_pin;','  bool pruned = false;')
a=s.index('  META_RECORD_CALL(meta.get_snapshot(first_snapshot, loaded));')
b=s.index('  META_RECORD_CALL(meta.advance_snapshot_gc_watermark(second_snapshot));',a)
s=s[:a]+s[b:]
s=s.replace('  ret = meta.insert_pin({static_cast<uint64_t>(second_snapshot), 5});', '''  ret = meta.fork_namespace(record.name, "at-watermark",
      [&](int64_t &scn) { scn = second_snapshot; return OB_SUCCESS; }, rejected_id);''')
a=s.index('  ns::CatalogRoots gc_snapshot;'); b=s.index('  int64_t deleted_pages = 0;',a)
s=s[:a]+'''  InstanceNamespaceRecord gc_child;
  gc_child.id = 500001;
  gc_child.name = "gc-child";
  gc_child.parent_namespace = gc_namespace.id;
  gc_child.fork_cap = 777;
  gc_child.roots.schema_version = 5;
  gc_child.roots.catalog = snapshot_page;
  META_RECORD_CALL(gc_meta.insert_namespace(gc_child));
''' + s[b:]
s=s.replace('lineage=1 pins=1', 'capped_roots=1 watermark_fence=1')
p.write_text(s)

p=local/'instance_namespace_durable_probe.ipp';s=p.read_text()
s=s.replace('child.roots.snapshot_ref != 0', 'child.roots.directory.page != 0')
s=s.replace('    rootserver::InstanceNamespacePin pin;\n','')
a=s.index('      META_DURABLE_CALL(meta.get_pin(');b=s.index('      META_DURABLE_CALL(kv.rollback(tx));',a)
s=s[:a]+'''      META_DURABLE_ASSERT(child.roots.directory.cap == child.fork_cap);
''' + s[b:]
s=s.replace('      META_DURABLE_CALL(meta.get_pin(child.roots.snapshot_ref, pin));\n','')
a=s.index('      ret = meta.get_pin(');b=s.index('      META_DURABLE_CALL(kv.rollback(tx));',a)
s=s[:a]+'''      InstanceNamespaceRecord deleted;
      META_DURABLE_CALL(meta.get_namespace(child.id, deleted));
      META_DURABLE_ASSERT(deleted.roots.state == 2 && deleted.roots.directory.page == 0
          && deleted.roots.catalog.page == 0);
''' + s[b:]
p.write_text(s)

for name in ['fork_history_gc_probe.py','fork_detached_restart_probe.py']:
    p=local/name;s=p.read_text().replace("['snapshot_ref']", "['fork_cap']")
    p.write_text(s)

p=local/'run_four_gates.py';s=p.read_text()
s=s.replace("        [local / 'physical_retention_mvcc_probe.py', '--binary', binary],", "        [local / 'physical_retention_mvcc_probe.py', '--binary', binary],\n        [local / 'source_roots_only_probe.py', '--binary', binary],")
p.write_text(s)
