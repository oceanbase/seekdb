if [ $# -lt 1 ]; then
  echo "no dir is specified, use ./run_test_dir.sh filename"
  exit 1
fi

TEST_DIR=$1
BLACK_FILE='black_test.list'
TEST_LIST_FILE='.run_test.list'

echo "run $TEST_DIR"

echo -n "" > $TEST_LIST_FILE
COUNT=0

for CASE_NAME in `find $TEST_DIR -name '*.test'`;do
  if [ -e "$BLACK_FILE" ] && (cat $BLACK_FILE | grep $CASE_NAME > /dev/null); then
    continue
  fi
  if [ $COUNT -ne 0 ]; then
    echo -n "," >> $TEST_LIST_FILE
  fi
  echo -n "$CASE_NAME" >> $TEST_LIST_FILE
  COUNT=$[ COUNT + 1 ]
done

./obtest -t $TEST_LIST_FILE disable-reboot
