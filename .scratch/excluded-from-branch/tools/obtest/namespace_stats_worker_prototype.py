#!/usr/bin/env python3
"""Run namespace optimizer statistics isolation gate."""
import argparse
import resource

from namespace_inprocess_prototype import run_case


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    run_case(args.binary, "stats")


if __name__ == "__main__":
    main()
