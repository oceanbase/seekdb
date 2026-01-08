#!/usr/bin/env bash

check_arguments()
{
    echo "Starting automatic dump analysis..."

    if [ "$#" -eq 0 ]
    then
        echo "Finding files automatically..."
        if [ -f "bin/observer" ]
        then
            exec="bin/observer"
        elif [ -f "observer" ]
        then
            exec="./observer"
        else
            exec="./a.out"
        fi
        core=`ls core.* 2>/dev/null | head -1`
    elif [ "$#" -eq 2 ]
    then
        exec="$1"
        core="$2"
    else
        echo "Usage: `basename $0` [<exec> <core>]"
        exit 1
    fi

    top=`dirname $0`
    bt="bt-all.txt"
    pmp="pt-pmp.txt"
}

check_files()
{
    gdb --version >/dev/null 2>&1 ||
    {
        echo "gdb not exists"
        exit 1
    }

    pt-pmp --version >/dev/null 2>&1 ||
    {
        echo "pt-pmp not exists, to install:"
        echo "  sudo yum install percona-toolkit -b test"
        echo "Or"
        echo "  wget https://www.percona.com/downloads/percona-toolkit/2.2.14/tarball/percona-toolkit-2.2.14.tar.gz"
        exit 1
    }

    if [ ! -f "$exec" ]
    then
        echo "exec file \"$exec\" not exists"
        exit 1
    fi

    if [ ! -f "$core" ]
    then
        echo "core file \"$core\" not exists"
        exit 1
    fi
}

dump_variables()
{
    echo "Variables:"
    echo "exec: $exec"
    echo "core: $core"
    echo ""
}

fire()
{
    if [ x"$exec" != x"./a.out" ]
    then
        "$exec" -V 2>&1 | grep -v '^profiling:' | tee version.txt
    fi

    # gdb --nx --batch-silent --command=./auto-analysis.gdb --core=core.31205 --exec=bin/observer --symbols=bin/observer
    gdb --nx --batch-silent -ex "source $top/all.gdb" -x="$top/auto-analysis.gdb" --core="$core" --exec="$exec" --symbols="$exec"
    pt-pmp "$bt" > "$pmp"
}

list_output()
{
    echo "List output files:"
    ls -l1 *.txt
}

check_arguments "$@"
check_files
dump_variables
fire
list_output
