PREPARE TESTS
=============
    ./prepare_tests.sh


CREATE MITM LOGS
================
NOTE: For SSL agents you need to set your username/password to something you dont mind sharing, as it's not possible to change it in the logfiles

Copy and edit file in devices.d according to agent/protocol/parameters used.

Create logs for all actions (change actions more or less are supported):
    for x in status-on already-on reboot off status-off already-off on; do echo "$x"; ./create_test_data.py -f -d <device> -a $x; done

Change hostname, IP and password to e.g. "localhost" and "test" in devices.d/<device>.cfg and data/mitm-logs/...


RUN TESTS
=========
    ./run_tests.py -d <device> [-a <status-on|...>]
