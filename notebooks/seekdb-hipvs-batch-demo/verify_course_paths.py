"""Measure backend/result agreement after preparing the course demo's vec database."""
import argparse
import json
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent / 'lib'))
import seekdb_demo as sk


def group_results(rows, probes=100, k=10, corpus_size=10000):
    grouped = {pid: [] for pid in range(probes)}
    for row in rows:
        pid, nid = int(row[0]), int(row[1])
        if pid not in grouped or not 0 <= nid < corpus_size:
            raise RuntimeError('Unexpected probe or neighbor ID: %s' % (row,))
        grouped[pid].append(nid)
    if any(len(values) != k or len(set(values)) != k for values in grouped.values()):
        raise RuntimeError('Every probe must return exactly k distinct neighbors')
    return {pid: set(values) for pid, values in grouped.items()}


def agreement(left, right):
    if not left or left.keys() != right.keys():
        raise RuntimeError('Comparisons require the same nonempty probe set')
    denominator = sum(len(values) for values in left.values())
    if not denominator or any(not values or len(values) != len(right[pid]) for pid, values in left.items()):
        raise RuntimeError('Comparisons require equal nonempty top-k sets')
    return {'queries': len(left),
            'identical_topk_sets': sum(values == right[pid] for pid, values in left.items()),
            'mean_topk_overlap': sum(len(values & right[pid]) for pid, values in left.items()) / denominator}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    report = {'completed': False, 'scope': 'course preinstalled query paths; not Docker deployment or HNSW construction',
              'gpu_selection': 'SQL lib=cuvs; this script does not set GPU selection environment variables',
              'platform_gpu_visibility_variable_names': [name for name in ('HIP_VISIBLE_DEVICES', 'ROCR_VISIBLE_DEVICES', 'CUDA_VISIBLE_DEVICES') if name in os.environ]}
    try:
        report['server_version'] = sk.version()
        report['table_ddl'] = {name: sk.sql_rows('use vec; show create table ' + name) for name in ('t_cuvs', 't_vsag')}
        counts = {name: int(sk.sql_rows('use vec; select count(*) from ' + name)[0][0]) for name in ('t_cuvs', 't_vsag', 'probes_100')}
        if counts != {'t_cuvs': 10000, 't_vsag': 10000, 'probes_100': 100}:
            raise RuntimeError('Run the complete Notebook data preparation first: %s' % counts)
        same_ids = int(sk.sql_rows('use vec; select count(*) from t_cuvs c join t_vsag v on c.c1=v.c1')[0][0])
        different_vectors = int(sk.sql_rows('use vec; select count(*) from t_cuvs c join t_vsag v on c.c1=v.c1 where l2_distance(c.c2,v.c2) <> 0')[0][0])
        if same_ids != 10000 or different_vectors:
            raise RuntimeError('CPU/GPU source data differ')
        report['data_counts'] = counts
        report['source_vectors_equal'] = True
        report['cpu_lateral_ms'] = sk.cpu_lateral(100) * 1000
        cpu_rows = sk.sql_rows('use vec; select probe_id,neighbor_id from cpu_out order by probe_id,neighbor_id')
        report['gpu_batch_ms'] = sk.gpu_batch(100) * 1000
        gpu_rows = sk.sql_rows('use vec; select probe_id,neighbor_id from bk_out order by probe_id,rk')
        cpu, gpu = group_results(cpu_rows), group_results(gpu_rows)
        probes = sk.sql_rows('use vec; select id,pv from probes_100 order by id')
        single = {}
        for table in ('t_cuvs', 't_vsag'):
            rows = []
            for pid, vector in probes:
                found = sk.sql_rows("use vec; select c1 from %s order by l2_distance(c2,'%s') approximate limit 10" % (table, vector))
                rows.extend((pid, row[0]) for row in found)
            single[table] = group_results(rows)
        sk.gpu_batch(100)
        gpu_repeat_rows = sk.sql_rows('use vec; select probe_id,neighbor_id from bk_out order by probe_id,rk')
        gpu_repeat = group_results(gpu_repeat_rows)
        report['agreement'] = {'cpu_vs_gpu_single': agreement(single['t_vsag'], single['t_cuvs']),
                               'cpu_vs_gpu_batch': agreement(cpu, gpu),
                               'cpu_single_vs_lateral': agreement(single['t_vsag'], cpu),
                               'gpu_single_vs_batch': agreement(single['t_cuvs'], gpu),
                               'gpu_batch_repeat': agreement(gpu, gpu_repeat)}
        gt = sk._load_gt()
        report['recall_at_10'] = {name: sum(len(values & gt[pid]) for pid, values in grouped.items()) / 1000
                                  for name, grouped in [('cpu_single', single['t_vsag']), ('gpu_single', single['t_cuvs']), ('cpu_lateral', cpu), ('gpu_batch', gpu)]}
        output_table = 'course_repro_cpu_batch_out'
        sk.sql('use vec; drop table if exists %s; create table %s like bk_out' % (output_table, output_table))
        try:
            elapsed = sk.sql("use vec; call dbms_vector.batch_knn('t_vsag','probes_100',10,'%s')" % output_table, timed=True)[0]
            rows = sk.sql_rows('use vec; select probe_id,neighbor_id from ' + output_table)
            switched = group_results(rows)
            report['cpu_index_same_batch_procedure'] = {'supported': True, 'elapsed_ms': elapsed * 1000,
                                                       'agreement_with_cpu_lateral': agreement(switched, cpu)}
        except RuntimeError as error:
            report['cpu_index_same_batch_procedure'] = {'supported': False, 'error': str(error)}
        report['query_result_ids'] = {name: {str(pid): sorted(values) for pid, values in grouped.items()}
                                      for name, grouped in [('cpu_single', single['t_vsag']), ('gpu_single', single['t_cuvs']), ('cpu_lateral', cpu), ('gpu_batch', gpu), ('gpu_batch_repeat', gpu_repeat)]}
        report['completed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps({key: value for key, value in report.items() if key not in ('query_result_ids', 'table_ddl')}, indent=2), flush=True)


if __name__ == '__main__':
    main()
