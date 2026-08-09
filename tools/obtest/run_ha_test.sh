#!/bin/bash

function print_usage() {
  printf "Usage: %s [-h] observer_binary\n" "$0"
  printf "Run HA tests using observer binary and test case file\n"
  printf " -h Print this help message and exit\n"
  exit 0
}

while getopts "h" opt; do
  case $opt in
  h)
  print_usage ;;
  *)
  echo "Invalid option: -$OPTARG. Use -h for help." >&2
  exit 1 ;;
  esac
done

if [[ $# -lt 1 ]]; then
  echo "Error: Missing arguments. Use -h for help."
  exit 1
fi

observer_binary_abs_path=$1
test_case_path="ha_farm_test.list"

test_cases=$(paste -s -d, "t/$test_case_path")

printf "Running tests for observer binary: %s\n" "$observer_binary_abs_path"
printf "Using test case file: %s\n" "$test_case_path"
printf "Test cases: %s\n" "$test_cases"

if ! ak obtest --cases "$test_cases" --observer "$observer_binary_abs_path"; then
  echo "Error: Failed to run obtest command."
  exit 1
fi

exit 0
