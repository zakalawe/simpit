#!/bin/bash

FGFS_PREFIX=/home/jmt/FGFS/dist
QT_SDK=/home/jmt/QtSDK/5.9.6/gcc_64

export LD_LIBRARY_PATH=$QT_SDK/lib/:$FGFS_PREFIX/lib
export PATH=$FGFS_PREFIX/bin:$PATH

#./dist/bin/fgqcanvas $HOME/simpit/738_captain.json &

fgfs --airport=EGPH \
	--addon=/home/jmt/simpit/simpit-addon \
	--timeofday=morning \
	--log-level=info \
	--prop:/sim/rendering/graphics-window-qt=true \
	--fg-aircraft=/home/jmt/FGFS/aircraft \
	--aircraft=738 \
	--telnet=5501 \
	--httpd=8080 \
	--prop:/sim/rendering/shaders/skydome=true \
	--prop:/sim/rendering/draw-mask/aircraft=true





