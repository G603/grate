#!/bin/sh

srcdir=`dirname $0`
test -z "$srcdir" && srcdir=.

ORIGDIR=`pwd`
cd $srcdir

test -d m4 || mkdir m4
autoreconf -v --install || exit 1
cd $ORIGDIR || exit $?

# rm Makefile
# $srcdir/configure "$@"
# $srcdir/configure --host=aarch64-linux-gnu  --prefix=/home/lr/Momenta/l_project/LC/HDMI/grate/install