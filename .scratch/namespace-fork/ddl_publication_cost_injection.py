#!/usr/bin/env python3
"""Local per-publication work counters; no state survives a publication call."""
import argparse,re
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('action',choices=('enable','disable'));a=p.parse_args()
root=Path(__file__).resolve().parents[2]
target=root/'src/rootserver/fork_table/namespace_schema_publication.cpp'
s=target.read_text();s=re.sub(r'// LOCAL_DDL_COST_BEGIN\n.*?// LOCAL_DDL_COST_END\n','',s,flags=re.S)
if a.action=='enable':
    def before(anchor,code):
        global s
        assert s.count(anchor)==1,anchor
        s=s.replace(anchor,'// LOCAL_DDL_COST_BEGIN\n'+code+'\n// LOCAL_DDL_COST_END\n'+anchor)
    before('int publication_physical_status(uint64_t physical, ObTabletCreateDeleteMdsUserData &data)',
           'thread_local int64_t publication_physical_lookups = 0;')
    before('  ObTabletHandle handle;','  ++publication_physical_lookups;')
    before('  ns::CatalogChanges definitions, sources;','  publication_physical_lookups = 0;')
    anchor='    ret = metadata.stage_catalog_delta(record.id, record.roots.schema_version,\n        version, definitions, sources);\n'
    assert s.count(anchor)==1
    s=s.replace(anchor,anchor+'''// LOCAL_DDL_COST_BEGIN
    if (getenv("SEEKDB_TEST_DDL_COST") != nullptr) {
      fprintf(stderr, "DDL_PUBLICATION_COST namespace=%lu version=%ld definitions=%zu sources=%zu physical_status=%ld bindings=%zu ret=%d\\n",
          record.id, version, definitions.size(), sources.size(), publication_physical_lookups, bindings.size(), ret);
    }
// LOCAL_DDL_COST_END
''')
if s!=target.read_text():target.write_text(s)
