#!/bin/bash
# Send N numbered lines to an RPMsg TTY and check each echo (run on the board).
# usage: rpmsg_echo_test.sh [tty] [count]
TTY=${1:-/dev/ttyRPMSG0}
N=${2:-1000}
stty -F "$TTY" raw -echo -onlcr
exec 3<>"$TTY"
ok=0; bad=0; lost=0
i=1
while [ "$i" -le "$N" ]; do
	msg="m33-echo-$i-$$"
	printf '%s\n' "$msg" >&3
	if read -t 1 -r line <&3; then
		if [ "$line" = "$msg" ]; then ok=$((ok + 1)); else bad=$((bad + 1)); echo "mismatch $i: '$line'"; fi
	else
		lost=$((lost + 1)); echo "timeout $i"
	fi
	i=$((i + 1))
done
exec 3>&-
echo "rpmsg_echo_test tty=$TTY n=$N ok=$ok bad=$bad lost=$lost"
