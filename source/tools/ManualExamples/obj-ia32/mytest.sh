#!/bin/bash
cd ..
make dir obj-ia32/inscount0.so
result=`echo $?`
cd obj-ia32
if [ $result -eq 0 ]; then
  echo
  echo Running Program...
  echo
  time pin -t inscount0.so -- ./test
fi
