#!/bin/sh

git submodule init
git submodule update

touch AUTHORS NEWS README ChangeLog
cp LICENSE COPYING

autoreconf -fis
