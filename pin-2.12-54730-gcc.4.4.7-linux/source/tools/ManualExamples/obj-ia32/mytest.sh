#!/bin/bash
cd ..
make
cd obj-ia32
pin -t inscount0.so -- ~/test
