#!/bin/sh
#
# Copyright (c) 2007 The DragonFly Project.  All rights reserved.
#
# This code is derived from software contributed to The DragonFly Project
# by Simon 'corecode' Schubert <corecode@fs.ei.tum.de>.
#
# Redistribution and use in source and binary forms, with or without 
# modification, are permitted provided that the following conditions are met:
#
# - Redistributions of source code must retain the above copyright notice, 
#   this list of conditions and the following disclaimer.
# - Redistributions in binary form must reproduce the above copyright notice, 
#   this list of conditions and the following disclaimer in the documentation 
#   and/or other materials provided with the distribution.
# - Neither the name of The DragonFly Project nor the names of its
#   contributors may be used to endorse or promote products derived
#   from this software without specific, prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

osver=`uname -r | awk -F - '{ print $1; }'`
cpuver=`uname -p | awk -F - '{ print $1; }'`
[ -f /etc/pkg_radd.conf ] && . /etc/pkg_radd.conf

# If dports is installed it takes priority over pkgsrc.
#
# dports uses /usr/local/etc/pkg.conf
#
if [ -e /usr/local/sbin/pkg ]; then
    if [ ! -f /usr/local/etc/pkg.conf ]; then
	echo "You need to setup /usr/local/etc/pkg.conf first."
	echo "See the sample in /usr/local/etc/pkg.conf.sample"
	exit 1
    fi
    exec pkg install "$@"
else
    : ${BINPKG_BASE:=http://mirror-master.dragonflybsd.org/packages}
    : ${BINPKG_SITES:=$BINPKG_BASE/$cpuver/DragonFly-$osver/stable}
    : ${PKG_PATH:=$BINPKG_SITES/All}
    export PKG_PATH
    exec pkg_add "$@"
fi
