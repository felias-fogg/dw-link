#!/bin/bash
#script for packing the dw-link tools: avr-gdb + dw-server
#usage: call the script in this directory; version will be deduced from dw-server -V

BASEURL="https://felias-fogg.github.io/dw-link/dw-tools/"

if [ -f ../dw-server/x86_64-apple-darwin/dw-server ]; then
    VERSION=dw-link-tools-`../dw-server/x86_64-apple-darwin/dw-server -V`
    echo "Creating tool packages for version $VERSION"
else
    echo "No dw-version binary found"
    exit
fi

for dir in ../dw-server/*; do
    if [ -d $dir ]; then
	if  [ -f $dir/avr-gdb -o -f $dir/avr-gdb.exe ]; then
	    if [ -f $dir/dw-server -o -f $dir/dw-server.exe ]; then
		type=${dir##*/}
		echo "Packing tools for: $type"
		rm -rf tools
		mkdir tools
		cp -r $dir/* tools/
		tar -jcv --exclude="*DS_Store" --exclude="*/._*" -f ${VERSION}_${type}.tar.bz2 tools/ 
		rm -rf tools
	    fi
	fi
    fi
done
	


