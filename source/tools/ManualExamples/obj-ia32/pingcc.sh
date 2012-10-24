#!/bin/bash
cd ..
cp ~/Backup.cpp ./inscount0.cpp
make dir obj-ia32/inscount0.so
result=`echo $?`
cd obj-ia32
if [ $result -eq 0 ]; then
  echo
  echo Running Program...
  echo
  time pin -t inscount0.so -- ./Lab2Benchmarks/403.gcc/gcc_base.i386 ./Lab2Benchmarks/403.gcc/scilab.i -o scilab.s
fi
