if [ $# -lt 1 ]; then
  echo "no dir is specified, use ./run_test_dir.sh filename"
  exit 1
fi

TEST_DIR=$1
BLACK_FILE='black_test.list'
TEST_LIST_FILE='temp_test.list'

echo "run $TEST_DIR"

echo -n "" > $TEST_LIST_FILE

for CASE_NAME in `find $TEST_DIR -name '*.test'`;do
  if [ -e "$BLACK_FILE" ] && (cat $BLACK_FILE | grep $CASE_NAME > /dev/null); then
    continue
  fi
  echo -n "$CASE_NAME" >> $TEST_LIST_FILE
  echo -e  >> $TEST_LIST_FILE
done