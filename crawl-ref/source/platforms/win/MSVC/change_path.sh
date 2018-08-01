#!/bin/bash


REAL_PATH=`find ../../../ -name "$1"`
echo $REAL_PATH |sed -e 's/\//\\/g' >> result

#echo "<ClCompile Include=\"$WIN_PATH\" />"

