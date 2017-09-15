#!/usr/bin/bash
cd ..
./autogen.sh && ./configure && make && cd mitmproxy/ && python setup.py build && echo "OK"
