#!/bin/sh

cd t
for i in *.t; do ./$i || exit 1; done
