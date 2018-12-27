#!/bin/bash

FGFS_PREFIX=/home/jmt/FGFS/dist
QT_SDK=/home/jmt/QtSDK/5.9.6/gcc_64

export LD_LIBRARY_PATH=$QT_SDK/lib/:$FGFS_PREFIX/lib
export PATH=$FGFS_PREFIX/bin:$PATH

fgfs --launcher


