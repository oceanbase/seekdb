"""Read-only audit of native HNSW_DATA emitted by the matching VSAG library."""
from pathlib import Path
import hashlib,json,struct
import numpy as np
import argparse
import sys

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('action',choices=['prepare','analyze'])
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
root=args.output.resolve()
if args.action=='prepare':
    from benchmark import generate_data,fingerprint
    if (root/'manifest.json').exists():
        raise FileExistsError('Refusing to overwrite a prior diagnostic dataset')
    root.mkdir(parents=True,exist_ok=True)
    vectors,queries,ids=generate_data(10000,128,100,20261031)
    for name,order in [('input',np.arange(len(ids))),('pk',np.argsort(ids))]:
        vectors[order].astype('<f4').tofile(root/(name+'.f32'))
        ids[order].astype('<i8').tofile(root/(name+'.i64'))
    queries.astype('<f4').tofile(root/'queries.f32')
    manifest=dict(rows=10000,dim=128,queries=100,seed=20261031,numpy=np.__version__,
                  dataset_sha256=fingerprint(vectors,queries,ids))
    (root/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print(json.dumps(manifest))
    sys.exit(0)
reports=[]
paths=sorted(root.glob('*.hnsw'))
if not paths:
    raise FileNotFoundError('No native HNSW exports found')
for path in paths:
    order=path.stem.split('-')[0]
    data=path.read_bytes()
    vectors=np.fromfile(root/(order+'.f32'),dtype='<f4').reshape(10000,128)
    ids=np.fromfile(root/(order+'.i64'),dtype='<i8')
    n=struct.unpack_from('<Q',data,16)[0]
    stride,labeloff,vecoff=struct.unpack_from('<3Q',data,24)
    entry=struct.unpack_from('<I',data,52)[0]
    m=struct.unpack_from('<Q',data,56)[0]
    assert n==len(ids) and 0<=entry<n
    labels=[];adj=[];lookup={int(i):j for j,i in enumerate(ids)}
    for node in range(n):
        pos=96+node*stride
        label=struct.unpack_from('<q',data,pos+labeloff)[0]
        labels.append(label)
        assert label in lookup
        assert data[pos+vecoff:pos+labeloff]==vectors[lookup[label]].tobytes()
        size=struct.unpack_from('<H',data,pos)[0]
        assert size<=2*m
        links=list(struct.unpack_from('<'+str(size)+'I',data,pos+4))
        assert all(x<n for x in links)
        adj.append(links)
    assert len(set(labels))==n and set(labels)==set(map(int,ids))
    def reachable(graph):
        seen={entry};todo=[entry]
        while todo:
            u=todo.pop()
            for x in graph[u]:
                if x not in seen:
                    seen.add(x);todo.append(x)
        return seen
    base=reachable(adj)
    offset=96+n*stride
    level_stride=4+4*m
    for node in range(n):
        length=struct.unpack_from('<I',data,offset)[0]
        offset+=4
        assert length%level_stride==0
        for level in range(length//level_stride):
            pos=offset+level*level_stride
            size=struct.unpack_from('<H',data,pos)[0]
            assert size<=m
            links=list(struct.unpack_from('<'+str(size)+'I',data,pos+4))
            assert all(x<n for x in links)
            adj[node].extend(links)
        offset+=length
    assert offset==len(data)
    union=reachable(adj)
    indegree=[0]*n
    for links in adj:
        for x in links:indegree[x]+=1
    reports.append(dict(file=path.name,order=order,num_nodes=n,actual_m=m,
        all_labels_unique=True,all_vectors_bitwise_equal=True,entry_node=entry,
        base_reachable=len(base),all_layer_reachable=len(union),
        unreachable_ids=[labels[i] for i in range(n) if i not in union],
        zero_indegree_ids=[labels[i] for i,x in enumerate(indegree) if not x],
        native_sha256=hashlib.sha256(data).hexdigest()))
(root/'native-graph-analysis.json').write_text(json.dumps(reports,indent=2))
for r in reports:
    print(r['file'],'M=',r['actual_m'],'stored=',r['num_nodes'],'base_reachable=',r['base_reachable'],
          'all_layer_reachable=',r['all_layer_reachable'],'zero_indegree=',len(r['zero_indegree_ids']))
