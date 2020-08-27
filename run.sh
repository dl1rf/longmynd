#!/usr/bin/env bash

# Wifi t430
VLC=192.168.100.188
# Wired t430
#VLC=192.168.100.244
# Wifi Dell
#VLC=192.168.100.114

# t430 VPN
#VLC=10.159.0.6

FREQ=742000
SR=1500

./longmynd -i ${VLC} 9002 -I 127.0.0.1 9003 -W 9004 -p v -r -1 ${FREQ} ${SR}
#gdb --args ./longmynd -i ${VLC} 9002 -I 127.0.0.1 9003 -W 9004 -p h ${FREQ} ${SR}
