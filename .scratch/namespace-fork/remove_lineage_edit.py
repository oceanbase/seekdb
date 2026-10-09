from pathlib import Path

root = Path(__file__).resolve().parents[2]

def change(name, fn):
    p = root / name
    p.write_text(fn(p.read_text()))

def cut(s, start, end):
    a = s.index(start)
    b = s.index(end, a)
    return s[:a] + s[b:]

def catalog_h(s):
    s = s.replace('  uint64_t snapshot_ref = 0;\n', '')
    s = s.replace('  uint64_t parent_ref = 0; int64_t ref_count = 0; // Canonical snapshot rows only.\n', '')
    s = s.replace('  bool valid_snapshot(uint64_t expected_id) const;\n', '')
    return cut(s, '// The caller owns one transaction for the whole release.', 'class NamespaceCatalogCodec final')

def metadata_h(s):
    s = cut(s, 'struct InstanceNamespacePin\n', 'enum class TabletVisibility')
    s = s.replace('  using SnapshotVisitor = std::function<int(uint64_t, const ns::CatalogRoots &)>;\n', '')
    s = s.replace('  using PinVisitor = std::function<int(const InstanceNamespacePin &)>;\n', '')
    s = cut(s, '  int get_snapshot(uint64_t id,', '  int initialize_snapshot_gc_watermark')
    s = cut(s, '  int get_pin(uint64_t snapshot_id,', '  int read_page(uint64_t page_id,')
    s = cut(s, '// The lineage algorithm owns ordering', '// One call owns one native KV transaction.')
    s = s.replace('// name, pin, and lineage in this transaction. Caller commits or rolls back.',
                  '// name and capped roots in this transaction. The coordination row lock\n  // fences this publication against watermark advancement until commit.')
    s = s.replace('// and retained snapshot roots, then stages at most max_deletes page erases.',
                  '// roots, then stages at most max_deletes page erases.')
    s = s.replace('// Commits the child, name, snapshot pin and lineage before returning child.',
                  '// Commits the child, name and shared capped roots before returning child.')
    s = s.replace('// Releases lineage and marks DELETED. Shared physical GC subsequently',
                  '// Clears the owned roots and marks DELETED. Shared physical GC subsequently')
    return s

def metadata_cpp(s):
    for field, kind in [('snapshot_ref','u64'), ('parent_ref','u64'), ('ref_count','i64')]:
        s = s.replace(f'  append_json_{kind}(out, "{field}", roots.{field});\n', '')
        s = s.replace(f'  if (ret == OB_SUCCESS) {{ ret = json_{kind}(object, "{field}", roots.{field}); }}\n', '')
    s = cut(s, 'int encode_snapshot(', 'int decode_gc_watermark(')
    s = cut(s, 'int InstanceNamespaceMetadata::get_snapshot(', 'int InstanceNamespaceMetadata::initialize_snapshot_gc_watermark(')
    s = cut(s, 'int InstanceNamespaceMetadata::get_pin(', 'int InstanceNamespaceMetadata::read_page(')
    a = s.index('  if (ret == OB_SUCCESS) {\n    ret = scan_snapshots(')
    b = s.index('  // Active readers retain', a)
    s = s[:a] + s[b:]
    s = cut(s, 'int InstanceSnapshotLineageStore::load_for_update(', 'int InstanceNamespaceMetadata::fork_namespace(')
    a = s.index('  uint64_t allocated_id = 0;', s.index('int InstanceNamespaceMetadata::fork_namespace('))
    b = s.index('  if (ret == OB_SUCCESS) { child_id = allocated_id; }', a)
    s = s[:a] + '''  // Hold this row lock through publication. A collector which observes C
  // sees every already admitted fork at S<=C; later forks cannot introduce
  // a dependency older than that floor. Roots carry all inherited older caps.
  int64_t watermark = 0;
  if (ret == OB_SUCCESS) { ret = get_snapshot_gc_watermark(watermark, true); }
  if (ret == OB_ENTRY_NOT_EXIST) { ret = OB_NOT_INIT; }
  if (ret == OB_SUCCESS && roots.snapshot <= watermark) { ret = OB_SNAPSHOT_DISCARDED; }
  uint64_t allocated_id = 0;
  if (ret == OB_SUCCESS) { ret = allocate_namespace_id(allocated_id); }
  if (ret == OB_SUCCESS) {
    roots.catalog.cap = ns::NamespaceCatalogCodec::cap_min(roots.catalog.cap, roots.snapshot);
    roots.directory.cap = ns::NamespaceCatalogCodec::cap_min(roots.directory.cap, roots.snapshot);
    roots.source = 0;
    InstanceNamespaceRecord child;
    child.id = allocated_id;
    child.name = target_name;
    child.roots = roots;
    child.parent_namespace = source_id;
    child.fork_cap = roots.snapshot;
    ret = insert_namespace(child);
  }
''' + s[b:]
    s = s.replace('    const uint64_t snapshot_ref = record.roots.snapshot_ref;\n', '')
    s = s.replace('''    if (ret == OB_SUCCESS && snapshot_ref != 0) {
      InstanceSnapshotLineageStore lineage(*this);
      ret = ns::NamespaceSnapshotLineage::release(snapshot_ref, lineage);
    }
''', '')
    return s

change('src/namespace/catalog.h', catalog_h)
change('src/namespace/catalog.cpp', lambda s: cut(s, 'bool CatalogRoots::valid_snapshot(', 'int64_t NamespaceCatalogCodec::cap_min('))
change('src/rootserver/fork_table/instance_namespace_metadata.h', metadata_h)
change('src/rootserver/fork_table/instance_namespace_metadata.cpp', metadata_cpp)
change('src/share/instance_meta/instance_meta_collection.h', lambda s: s.replace('  SNAPSHOTS = 3,\n', '').replace('  SNAPSHOT_PINS = 8,\n', ''))
change('src/share/instance_meta/instance_meta_key_codec.h', lambda s: s.replace('    static const KeyFieldDesc snapshot_id[] = {{"snapshot_id", KeyFieldType::UINT64_BE}};\n', '').replace('{"SNAPSHOTS", snapshot_id, 1}', '{nullptr, nullptr, 0}').replace('        {"SNAPSHOT_PINS", snapshot_id, 1},\n', ''))
change('src/rootserver/fork_table/namespace_fork_kernel_prototype.cpp', lambda s: s.replace('snapshot=%ld snapshot_ref=%llu items=%zu', 'snapshot=%ld items=%zu').replace('        static_cast<unsigned long long>(root.snapshot_ref), items.size(),', '        items.size(),'))
