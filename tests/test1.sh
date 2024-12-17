#!/bin/bash

../build/server/server &
server_pid=$!

../build/replica/replica 8080 &
replica1_pid=$!

../build/replica/replica 8081 &
replica2_pid=$!



kill -9 $server_pid $replica1_pid $replica2_pid