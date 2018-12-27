#!/bin/bash

FGFS_PREFIX=/home/jmt/FGFS/dist
QT_SDK=/home/jmt/QtSDK/5.9.6/gcc_64

export LD_LIBRARY_PATH=$QT_SDK/lib/:$FGFS_PREFIX/lib
export PATH=$FGFS_PREFIX/bin:$PATH

fgfs --airport=EGPH \
	--addon=/home/jmt/simpit/simpit-addon \
	--log-level=info \
	--fg-scenery=/home/jmt/navigraph_201813_procedures \
	--prop:/sim/rendering/graphics-window-qt=true \
	--aircraft-dir=/home/jmt/simpit/737-800 \
	--aircraft=738 \
	--telnet=5501 \
	--httpd=8080 \
	--prop:/sim/rendering/shaders/skydome=true \
	--prop:/sim/rendering/multithreading-mode=CullDrawThreadPerContext \
	--enable-terrasync
	
	
	





