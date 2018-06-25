#!/bin/bash

export LD_LIBRARY_PATH=/home/jmt/QtSDK/5.9.1/gcc_64/lib/:/home/jmt/FGFS/dist/lib

./dist/bin/fgfs --config=/home/jmt/FGFS/twoview.xml --airport=EGPH \
	 --timeofday=noon --log-level=info \
	--prop:/sim/rendering/graphics-window-qt=true \
	  --aircraft=org.flightgear.fgaddon.777-200 \
	--enable-terrasync \
	 --telnet=5501 \
	--httpd=8080 \
	  --prop:/sim/rendering/shaders/skydome=true




