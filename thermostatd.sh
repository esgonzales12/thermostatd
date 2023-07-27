#!/bin/sh

start() {
    printf "thermostatd startup"
    /usr/sbin/thermostatd
    touch /var/lock/thermostatd
    echo "OK"
}

stop() {
    printf "thermostatd shutdown"
    killall thermostatd
    rm -f /var/lock/thermostatd
    echo "OK"
}

restart() {
    stop
    start
}

case "$1" in
    start)
    start
    ;;
    stop)
    stop
    ;;
    restart|reload)
    restart
    ;;
    *)
    echo "Usage: $0 {start|stop|restart}"
    exit 1
esac

exit $?