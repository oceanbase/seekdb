#$/bin/bash

current_dir=`pwd`
SOURCE_RESTORE_DIR=$1
if [ -z $SOURCE_RESTORE_DIR ]; then
    SOURCE_RESTORE_DIR=../ObRestore
fi

mvn_version=$(mvn --version 2>&1)

#if [[ "$mvn_version" =~ "command not found" ]]; then
    echo "maven not found, wget from hudson"
    if [ ! -d tmp ]; then
        mkdir tmp
    fi

    wget -P tmp/ --append-output=tmp/wget.log http://11.166.86.153:8877/tools/apache-maven-3.2.5-bin.tar.gz 2>&1
    tar -zxf tmp/apache-maven-3.2.5-bin.tar.gz -C tmp
    export MAVEN_HOME=`pwd`/tmp/apache-maven-3.2.5
    export PATH=$MAVEN_HOME/bin:$PATH
#fi

javac_version=$(javac -version 2>&1)
#if [[ "$javac_version" =~ "command not found" ]]; then
    echo "jdk not found, wget from hudson"
    if [ ! -d tmp ]; then
        mkdir tmp
    fi

    wget -P tmp/ --append-output=tmp/wget.log http://11.166.86.153:8877/tools/jdk-8u231-linux-x64.tar.gz 2>&1
    tar -zxf tmp/jdk-8u231-linux-x64.tar.gz -C tmp
    export JAVA_HOME=`pwd`/tmp/jdk1.8.0_231
    export PATH=$JAVA_HOME/bin:$PATH
#fi

echo build restore from $SOURCE_RESTORE_DIR

cd ${SOURCE_RESTORE_DIR}
mvn -s settings.xml -f pom.xml clean package -Dmaven.test.skip=true -l restore_build.log

# cleanup
cd $current_dir
rm -rf tmp
