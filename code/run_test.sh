#!/usr/bin/env expect

set timeout 2
spawn make run

expect "domotics>"
send "add bulb\r"

expect "domotics>"
send "list\r"

expect "domotics>"
send "exit\r"
