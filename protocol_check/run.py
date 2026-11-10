#!/usr/bin/env python3
"""Interactive JSONL handshake and public SQL checks; no platform callbacks."""
import argparse
import json
from pathlib import Path
import signal
import subprocess
import sys
import time

from _transport import Session, strict_json
from _assertions import assert_response

ROOT = Path(__file__).resolve().parent


def within(root, relative):
    path = (root / relative).resolve(strict=True)
    path.relative_to(root.resolve())
    return path


def check_case(session, case, package, config):
    expectations = strict_json(within(package, case['result']).read_text())
    statements = within(package, case['test']).read_text().splitlines()
    index, mode = 0, 'exact'
    for line_number, text in enumerate(statements, 1):
        sql = text.strip()
        if not sql or sql.startswith('#'):
            continue
        if sql == '--restart':
            session.shutdown()
            session.start()
        elif sql in ('--mode exact', '--mode ann'):
            mode = sql.split()[1]
        elif sql.startswith('--'):
            raise ValueError(f'unsupported public directive: {sql}')
        else:
            if index >= len(expectations):
                raise ValueError('missing expected result')
            response = session.request(sql, mode)
            try:
                assert_response(response, expectations[index], config)
            except AssertionError as exc:
                raise AssertionError(f'line {line_number}: {sql}: {exc}') from exc
            index += 1
    if index != len(expectations):
        raise ValueError('SQL/result count mismatch')


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument('--image', help='built student Docker image (recommended)')
    target.add_argument('--command', help='trusted local development only: JSON argv; {data_dir} placeholder supported')
    parser.add_argument('--handshake-only', action='store_true', help='check ready and shutdown, without SQL')
    parser.add_argument('--case', action='append', help='public case ID; repeat to select multiple cases')
    parser.add_argument('--package', type=Path, default=ROOT.parent/'mysqltest')
    parser.add_argument('--config', type=Path, default=ROOT/'config.json')
    parser.add_argument('--output', type=Path, required=True, help='new directory for reports and stderr logs')
    args = parser.parse_args(argv)
    config = strict_json(args.config.read_text())
    command = strict_json(args.command) if args.command else None
    if command is not None and (not isinstance(command, list) or not command
            or any(not isinstance(x, str) for x in command)):
        parser.error('--command must be a nonempty JSON array of strings')
    if args.handshake_only and args.case:
        parser.error('--handshake-only and --case cannot be combined')
    if args.handshake_only:
        cases = [{'id': 'handshake'}]
    else:
        manifest = strict_json((args.package/'manifest.json').read_text())
        cases = manifest['cases']
        if args.case:
            unknown = set(args.case) - {case['id'] for case in cases}
            if unknown:
                parser.error('unknown public case: '+', '.join(sorted(unknown)))
            cases = [case for case in cases if case['id'] in args.case]
    if not cases:
        parser.error('no public cases selected')
    args.output.mkdir(parents=True, exist_ok=False)
    results, aborted = [], None
    for case in cases:
        if aborted:
            results.append(dict(id=case['id'], status='SKIPPED', message=aborted))
            continue
        before = time.monotonic()
        row = dict(id=case['id'], status='PASS', message='')
        session = None
        try:
            session = Session(config, args.output/(case['id']+'.stderr.log'),
                              image=args.image, command=command)
            session.start()
            if not args.handshake_only:
                check_case(session, case, args.package, config)
            session.shutdown()
        except KeyboardInterrupt:
            row.update(status='ERROR', message='interrupted by operator')
            aborted = 'not run after interruption'
        except (TimeoutError, subprocess.TimeoutExpired) as exc:
            row.update(status='TIMEOUT', message=str(exc))
            aborted = 'not run after first timeout'
        except Exception as exc:
            row.update(status='FAIL', message=str(exc))
        finally:
            if session is not None:
                try:
                    session.close()
                except Exception as exc:
                    row.update(status='ERROR', message=row['message']+'; cleanup failed: '+str(exc))
                    aborted = 'not run after cleanup failure'
        row['duration_ms'] = round((time.monotonic()-before)*1000)
        results.append(row)
        print(f"{row['id']}: {row['status']} {row['message']}", flush=True)
        (args.output/'cases.json').write_text(json.dumps(results, ensure_ascii=False, indent=2)+'\n')
    passed = all(row['status']=='PASS' for row in results)
    report = dict(scope='public-protocol-check', passed=passed, cases=results,
                  note='Local diagnostic only; no performance/Agent scores or platform callbacks.')
    (args.output/'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(f"Report: {args.output/'result.json'}")
    return 0 if passed else 1


if __name__ == '__main__':
    def terminate(signum, frame):
        raise KeyboardInterrupt('terminated')
    signal.signal(signal.SIGTERM, terminate)
    sys.exit(main())
