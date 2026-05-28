#!/usr/bin/env expect

set timeout 2
spawn make run

expect "domotics>"
send "add bulb\r"

expect "domotics>"
send "list\r"

expect "domotics>"
system "ps aux | grep domotics_controller > pids.txt"
send "info 1\r"

expect "domotics>"
send "exit\r"
