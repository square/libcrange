#!/bin/sh

set -e
set -x
[ -n "$DESTDIR" ] || export DESTDIR=$HOME/prefix

rm -rf $DESTDIR
make clean || true # ignore failures

aclocal
case "${OSTYPE}" in
  darwin*)
    glibtoolize --force # brew
    ;;
  *)
    libtoolize --force
    ;;
esac
autoheader
automake -a
autoconf

case "${OSTYPE}" in
  darwin*)

    # brewperl. /usr/bin/perl on osx results in https://github.com/eam/libcrange/issues/7
    ./configure --prefix=/usr --enable-perl=/opt/local/bin/perl --enable-python=/usr/local/bin/python

    # This is a complete hack, can't find an easy way to disable
    # 32bit arch on OSX
    perl -pi -wle's/-arch i386//g' Makefile src/Makefile
    ;;
  *)
    ./configure --prefix=/usr
    ;;
esac
make
make install
cd perl
sh ./build
cd ..

cp -a ../root/* $DESTDIR/

# hacks to get config going
#mkdir $DESTDIR/etc
# cp ../root/etc/libcrange.conf.example $DESTDIR/etc/range.conf 


# configure dns zonefile data
DNS_FILE=$DESTDIR/etc/dns_data.tinydns
echo dns_data_file=$DNS_FILE >> $DESTDIR/etc/range.conf
echo "+foo1.example.com:1.2.3.1:0" >> $DNS_FILE
echo "+foo1.example.com:1.2.3.1:0" >> $DNS_FILE
echo "+foo2.example.com:1.2.3.2:0" >> $DNS_FILE
echo "+foo3.example.com:1.2.3.3:0" >> $DNS_FILE
echo "+foo4.example.com:1.2.3.4:0" >> $DNS_FILE

#configure site -> netblocks
YST_IP_LIST_FILE=$DESTDIR/etc/yst-ip-list
echo yst_ip_list=$YST_IP_LIST_FILE >> $DESTDIR/etc/range.conf
echo "foosite 1.2.3.0/24" >> $YST_IP_LIST_FILE

# configure nodes.cf / yamlfile
RANGE_DATADIR=$DESTDIR/rangedata
mkdir -p $RANGE_DATADIR
echo yaml_path=$RANGE_DATADIR >> $DESTDIR/etc/range.conf
# FIXME order matters in this file, yaml_path must be set before yamlfile loads
echo loadmodule $DESTDIR/usr/lib/libcrange/yamlfile >>  $DESTDIR/etc/range.conf 

# load the rest of modules last
echo loadmodule $DESTDIR/usr/lib/libcrange/ip >>  $DESTDIR/etc/range.conf 
echo loadmodule $DESTDIR/usr/lib/libcrange/yst-ip-list >>  $DESTDIR/etc/range.conf 

# Before perlmodules are loaded, set var for @INC
# $DESTDIR/usr/local/lib64/perl5 = RHEL6
# $DESTDIR/usr/local/lib/perl/5.14.2 = ubuntu/travis
echo perl_inc_path=$DESTDIR/usr/local/lib64/perl5:$DESTDIR/var/libcrange/perl:$DESTDIR/usr/local/lib/perl/5.14.2 >> $DESTDIR/etc/range.conf
echo perlmodule LibrangeUtils >>  $DESTDIR/etc/range.conf
echo perlmodule LibrangeAdminscf >> $DESTDIR/etc/range.conf



# Create some cluster data

echo "---" >> $RANGE_DATADIR/GROUPS.yaml
echo "bar:" >> $RANGE_DATADIR/GROUPS.yaml
echo "- foo1..2.example.com" >> $RANGE_DATADIR/GROUPS.yaml

