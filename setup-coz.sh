#!/bin/sh

apt-get install -y python3 git npm f++-dev
cd /opt
git clone https://github.com/plasma-umass/coz.git
cd coz
make
