#!/bin/bash

test_small_diff() {
    ls . > /tmp/foo
    ls / > /tmp/foo2
    ./bsdiff /tmp/foo /tmp/foo2 /tmp/foo.patch
    if [ $? -ne 0 ]; then
        echo "fail"
        return 1
    fi
    ./bspatch /tmp/foo /tmp/foo3 /tmp/foo.patch
    if [ $? -ne 0 ]; then
        echo "fail"
        return 1
    fi
    diff -q /tmp/foo3 /tmp/foo2
    if [ $? -ne 0 ]; then
        echo "fail"
        return 1
    else
        echo "pass"
        return 0
    fi
}

test_small_diff
