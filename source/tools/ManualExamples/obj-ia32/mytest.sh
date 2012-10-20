#!/bin/bash
cd ..
make dir obj-ia32/inscount0.so
result=`echo $?`
cd obj-ia32
if [ $result -eq 0 ]; then
  pin -t inscount0.so -- ./test
fi
