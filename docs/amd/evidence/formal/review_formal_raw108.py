"""Read-only independent review of the imported AMD formal evidence."""
from pathlib import Path, PurePosixPath
import hashlib, importlib.util, json, math, re, statistics, sys

ROOT = Path('D:/ob2026_plan/runs/amd107/cloud-final-formal')
OUT = Path('D:/ob2026_plan/validation/amd-build107/formal-review108.json')
sha = lambda b: hashlib.sha256(b).hexdigest()
read = lambda p: json.loads(p.read_text(encoding='utf-8'))
manifest = read(ROOT/'export-manifest.json')
index = {x['path']: x for x in manifest['files']}
assert len(index) == len(manifest['files'])
local = omitted = compact_local = bulk_local = 0
for name, row in index.items():
    p = ROOT/name
    if p.exists():
        b = p.read_bytes()
        assert len(b) == row['size'] and sha(b) == row['sha256'], name
        local += 1
        if row['compact_included']: compact_local += 1
        else: bulk_local += 1
    else:
        assert not row['compact_included'], name
        assert not p.exists(), name
        omitted += 1
assert manifest['snapshot_stable_during_export'] and not manifest['warnings']
archive_path = ROOT.parent/'full-archive-review108.json'
archive = read(archive_path)
assert archive['status']=='PASS' and archive['all_original_file_sizes_and_sha256_match']
assert archive['download_archive']['expected_size_and_sha256_match']
assert archive['original_files_verified']==local==21665 and bulk_local==2676 and compact_local==18989
assert archive['original_manifest_sha256']==sha((ROOT/'export-manifest.json').read_bytes())
assert not archive['readback_vector_semantics_recomputed_by_this_review']
spec = importlib.util.spec_from_file_location('reviewed_adapter', ROOT/'completion_proof.py')
adapter = importlib.util.module_from_spec(spec); spec.loader.exec_module(adapter)
launch = read(ROOT/'candidate-quality-launch.json')
assert sha((ROOT/'hnsw_bridge/seekdb_cuvs_bridge.c').read_bytes()) == adapter.check_source(ROOT/'hnsw_bridge/seekdb_cuvs_bridge.c')
source_hash = adapter.check_source(ROOT/'hnsw_bridge/seekdb_cuvs_bridge.c')
results = []
proof_local = proof_omitted = 0
for dirname, gtname in [('quality64-formal10k','gt107'),('quality64-formal100k-warn','gt100k')]:
    folder = ROOT/dirname
    report, args, data = [read(folder/name) for name in ('report.json','arguments.json','input-manifest.json')]
    gold = read(ROOT/gtname/'gold.json')
    assert sha((ROOT/gtname/'gold.json').read_bytes()) == data['artifacts']['gold.json']['sha256']
    assert report['status'] == 'formal_measured_requires_evidence_review' and report['qualified_gpu_build']
    assert report['mode'] == 'formal' and len(report['rounds']) == 6
    assert (args['m'],args['ef_construction'],args['ef_search']) == (64,400,1000)
    assert report['actual_session_ob_hnsw_ef_search'] == 1000
    assert report['script_sha256'] == sha((ROOT/'sql_benchmark.py').read_bytes())
    n, d, q, k = [data[x] for x in ('base_count','dimension','query_count','k')]
    assert (q,k,data['recall_targets']) == (100,10,1000)
    assert report['dataset_sha256'] == data['inputs']['base']['sha256']
    expected_ids = {str(x) for x in range(q)}
    assert set(gold) == expected_ids
    events = [json.loads(line) for line in (folder/'sql-events.jsonl').read_text().splitlines()]
    events_by_id = {x['number']:x for x in events}
    assert len(events_by_id) == len(events)
    assert all(x['status']=='ok' and x['actual_session_ob_hnsw_ef_search']==1000 for x in events)
    reviewed_rounds = []
    for row in report['rounds']:
        backend, number = row['backend'], row['round']
        rf = folder/('%02d-%s'%(number,backend))
        proof = row['completion_proof']
        assert row['status']=='verified_round_complete'
        assert row['raw_data_ready_before_timer'] and row['target_index_absent_before_timer']
        assert row['plan_contains_index_name']
        assert row['qualified_gpu_build'] == (backend=='gpu')
        for field in ['complete_index_ready','no_lazy_work_remaining','index_used','hnsw_semantics']:
            assert proof[field], (dirname,backend,number,field)
        assert proof['cpu_fallback'] is False and proof['indexed_rows']==n
        assert (proof['schema'],proof['table'],proof['index'],proof['backend']) == (report['schema'],row['table'],row['index'],backend)
        for label, count in [('base',n),('query',q)]:
            rb=proof['raw_data_readback'][label]
            assert rb['bitwise_equal'] and rb['rows']==count and rb['float32_values']==count*d
            assert rb['float32_sha256']==data['inputs'][label]['sha256']
        for item in proof['evidence']:
            relative=PurePosixPath(item['path']).relative_to('/workspace/amd107').as_posix()
            assert relative in index and index[relative]['sha256']==item['sha256'],relative
            if (ROOT/relative).exists():proof_local+=1
            else:proof_omitted+=1
        selected=read(rf/'completion-evidence/sql-success-events.json')
        assert [x['number'] for x in selected]==proof['sql_success_event_numbers']
        assert len(selected)==3
        for event in selected:
            assert event == events_by_id[event['number']]
            stem=rf/'completion-evidence'/('sql-%06d'%event['number'])
            session=read(stem.with_suffix('.session.json'))
            assert session['matches'] and session['actual_session_ob_hnsw_ef_search']==1000
            assert event['started_utc']>=row['build_started_utc']
        ddl=(rf/'completion-evidence'/selected[0]['sql']).read_text()
        assert 'CREATE VECTOR INDEX' in ddl and 'm=64,ef_construction=400,ef_search=1000' in ddl
        assert 'lib='+('vsag' if backend=='cpu' else 'cuvs') in ddl
        plan=json.dumps(read(rf/'explain.json'))
        assert re.search(r'VECTOR\s+INDEX\s+SCAN',plan,re.I) and row['index'] in plan
        captures=[]
        for path in sorted((rf/'completion-evidence').glob('phase-log-*.delta')):
            b=path.read_bytes();text=b.decode()
            assert not adapter.BAD_LOG.search(text)
            captures.append({'text':text,'sha256':sha(b),'path':str(path)})
        if backend=='gpu':
            adapter.check_gpu_markers(captures,n,d,args)
            gpu=proof['gpu_build_evidence']
            assert proof['gpu_build_participation'] and gpu['source_sha256']==source_hash
            assert gpu['bridge_library_sha256']==launch['bridge_sha256']
            assert gpu['synchronized_full_hnsw_and_caller_fd_snapshot']
            library=read(rf/'completion-evidence/bridge-library.json')
            assert library['sha256']==launch['bridge_sha256'] and library['loaded_in_observer_maps']
        else:
            assert not any('event=build_start ' in x['text'] for x in captures)
        predictions=read(rf/'predictions.json')
        assert set(predictions)==expected_ids
        hits=0
        for query, ids in predictions.items():
            assert len(ids)==k and len(set(ids))==k and all(type(x)is int and 0<=x<n for x in ids)
            hits+=len(set(ids)&set(gold[query][:k]))
        recalled=read(rf/'recall.json')
        assert recalled==row['recall']
        assert hits==recalled['hits']==recalled['targets']==1000
        assert recalled['query_count']==q and recalled['pooled']==recalled['minimum_query']==1.0
        assert set(recalled['hits_per_query'])==expected_ids and set(recalled['hits_per_query'].values())=={k}
        assert row['ddl_return_sec']<=row['ddl_to_first_query_return_sec']<=row['build_to_proven_ready_sec']
        reviewed_rounds.append({'round':number,'backend':backend,'ddl_seconds':row['ddl_return_sec'],
            'ddl_to_first_query_seconds':row['ddl_to_first_query_return_sec'],
            'proven_ready_seconds':row['build_to_proven_ready_sec'],'hits':hits,'targets':q*k,
            'recall':hits/(q*k),'strict_proof':True,'folder':dirname+'/'+rf.name})
    medians={}
    for backend in ('cpu','gpu'):
        group=[x for x in reviewed_rounds if x['backend']==backend]
        assert sorted(x['round'] for x in group)==[0,1,2]
        medians[backend]=statistics.median(x['proven_ready_seconds'] for x in group)
        assert math.isclose(medians[backend],report['median_build_sec'][backend],rel_tol=1e-12)
    speedup=medians['cpu']/medians['gpu']
    assert math.isclose(speedup,report['speedup'],rel_tol=1e-12)
    results.append({'name':dirname,'gt':gtname,'base_count':n,'dimension':d,'query_count':q,'k':k,
        'base_sha256':data['inputs']['base']['sha256'],'query_sha256':data['inputs']['query']['sha256'],
        'gold_sha256':data['artifacts']['gold.json']['sha256'],'rounds':reviewed_rounds,
        'median_build_seconds':medians,'speedup':speedup,'all_recall':1.0})
output={'status':'PASS','scope':'Independent original-manifest, complete-prediction/GT and strict-proof review',
    'local_originals_checked':local,'bulk_omitted_locally':omitted,'manifest_items':len(index),
    'proof_evidence_local_references_checked':proof_local,'proof_evidence_bulk_hash_only':proof_omitted,
    'compact_originals_checked':compact_local,'full_archive_bulk_originals_checked':bulk_local,
    'full_archive_locally_verified':True,'full_readbacks_independently_reparsed_locally':False,
    'full_archive_review_receipt_sha256':sha(archive_path.read_bytes()),
    'full_archive_original_bytes_checked':archive['original_bytes_verified'],
    'persistence_verified_by_this_review':False,
    'persistence_separate_review_receipt':'../persistence/persistence-review108.json','datasets':results}
OUT.write_bytes((json.dumps(output,indent=2)+'\n').encode())
print(json.dumps({k:v for k,v in output.items() if k!='datasets'}))
for dataset in results:print(json.dumps({k:v for k,v in dataset.items() if k!='rounds'}))
