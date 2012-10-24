#!/bin/bash
cd ..
make dir obj-ia32/inscount0.so
result=`echo $?`
cd obj-ia32
if [ $result -eq 0 ]; then
  echo
  echo Running Program...
  echo
  #time pin -t inscount0.so -- ./test
  time pin -t inscount0.so -- ./Lab2Benchmarks/401.bzip2/bzip2_base.i386 ./Lab2Benchmarks/401.bzip2/input.combined 200
fi
