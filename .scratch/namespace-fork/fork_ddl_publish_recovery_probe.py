#!/usr/bin/env python3
"""Legacy entry now tests the shared native commit boundary (requires probe binary)."""
import argparse
import resource
from ddl_catalog_atomic_probe import run
if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',required=True)
    parser.add_argument('--owner',choices=('initial','child'),required=True)
    args=parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE,(0,0))
    run(args.binary,args.owner,'commit_crash')
