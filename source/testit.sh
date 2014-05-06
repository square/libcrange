#!/bin/sh

export DESTDIR=${DESTDIR-"${HOME}/prefix"}
export PATH="DESTDIR/bin:$DESTDIR/usr/bin:$PATH"
export LD_LIBRARY_PATH="$DESTDIR/usr/lib" #FIXME should be lib64 for a 64bit build
export DYLD_LIBRARY_PATH="$LD_LIBRARY_PATH" # OSX
export PYTHONPATH="../python:$DESTDIR/var/libcrange/python"

#rm python/*.pyc
cd t
for i in *.t; do echo "Testing: $i"; ./$i || exit 1; done

export range_config="
dns_data_file=::CONFIG_BASE::/etc/dns_data.tinydns
yst_ip_list=::CONFIG_BASE::/etc/yst-ip-list
yaml_path=::CONFIG_BASE::/rangedata/range.db
loadmodule ::BUILD_ROOT::/usr/lib/libcrange/sqlite
loadmodule ::BUILD_ROOT::/usr/lib/libcrange/ip
loadmodule ::BUILD_ROOT::/usr/lib/libcrange/yst-ip-list

"

#for i in *.t; do echo "Testing: $i"; ./$i || exit 1; done


