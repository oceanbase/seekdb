#!/usr/bin/env python3
"""Run the single-process namespace SQL integration gate."""
import argparse
import resource


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--case", choices=("full",), default="full")
    parser.add_argument("--in-process", action="store_true",
                        help="accepted for compatibility; single-process is the only mode")
    args = parser.parse_args()
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    from namespace_inprocess_prototype import run_case
    run_case(args.binary, "sql")


if __name__ == "__main__":
    main()
