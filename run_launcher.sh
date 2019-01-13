#!/bin/bash

FGFS_PREFIX=/home/jmt/FGFS/dist

export LD_LIBRARY_PATH=$QT_SDK/lib/:$FGFS_PREFIX/lib
export PATH=$FGFS_PREFIX/bin:$PATH

fgfs --launcher


