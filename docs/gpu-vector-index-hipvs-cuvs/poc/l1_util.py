import sys, os, json, numpy as np
DS="/work/datasets"; RES="/work/results"
N,DIM,Q,K=10000,128,100,10
if sys.argv[1]=="export":
    base=np.load(f"{DS}/base_{N}x{DIM}.npy").astype("float32")
    query=np.load(f"{DS}/query_{Q}x{DIM}.npy").astype("float32")
    base.tofile(f"{DS}/base.f32"); query.tofile(f"{DS}/query.f32")
    print("exported base",base.shape,"query",query.shape)
elif sys.argv[1]=="compare":
    tag=sys.argv[2]
    gt=np.load(f"{DS}/gt_{Q}x{K}.npy")
    nbr=np.fromfile(f"{RES}/out_{tag}.u32",dtype=np.uint32).reshape(Q,K).astype("int64")
    hit=sum(len(set(nbr[i].tolist())&set(gt[i].tolist())) for i in range(Q))
    recall=hit/(Q*K)
    print(f"{tag} recall@{K} = {recall:.4f}")
    js=f"{RES}/L1_hipvs.json"; d=json.load(open(js)) if os.path.exists(js) else {}
    d[tag]={"recall":recall}; json.dump(d,open(js,"w"),indent=2)
