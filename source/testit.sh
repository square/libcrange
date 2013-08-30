#!/bin/sh

export DESTDIR="${HOME}/prefix"
export PATH="$DESTDIR/usr/bin:$PATH"
export LD_LIBRARY_PATH="$DESTDIR/usr/lib" #FIXME should be lib64 for a 64bit build
export DYLD_LIBRARY_PATH="$LD_LIBRARY_PATH" # OSX
export PYTHONPATH="../python:$DESTDIR/var/libcrange/python"

#rm python/*.pyc
cd t
for i in *.t; do echo "Testing: $i"; ./$i || exit 1; done
