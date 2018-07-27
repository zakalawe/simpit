#!/bin/bash

FGFS_PREFIX=/home/jmt/FGFS/dist
QT_SDK=/home/jmt/QtSDK/5.9.6/gcc_64

export LD_LIBRARY_PATH=$QT_SDK/lib/:$FGFS_PREFIX/lib
export PATH=$FGFS_PREFIX/bin:$PATH

#     --addon=$HOME/simpit/simpit-addon \

fgfs --launcher --log-level=info --httpd=8080 \
    --httpd=8080 \
    --prop:/sim/input/no-event-input=true \
    --prop:/sim/input/enable-hid=true \
