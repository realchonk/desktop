#!/bin/sh
# Author: Benjamin Stürz
# License: ISC

#logfile="$HOME/pinentry.log"
logfile="/dev/null"

log() {
	echo "$*" >> "$logfile"
}

send() {
	log "P: $*"
	echo "$*"
}

ok() {
	send 'OK'
}

unimp() {
	send 'ERR 536870981 Not implemented <User defined source 1>'
}

reset_vars() {
	title=
	prompt=
	desc=
}

reset_vars

log "A: $*"
send 'OK Pleased to meet you'

while read -r line; do
	log "C: $line"
	cmd=$(echo "$line" | awk '{print $1}' | tr '[[:lower:]]' '[[:upper:]]')
	args=$(echo "$line" | awk '{for (i=2; i<NF; ++i) printf "%s ", $i; print $NF}')

	case "$cmd" in
	'#')
		;;
	NOP)
		ok
		;;
	CANCEL)
		unimp
		;;
	OPTION)
		# TODO
		ok
		;;
	BYE)
		send 'OK closing connection'
		;;
	AUTH)
		unimp
		;;
	RESET)
		reset_vars
		ok
		;;
	END)
		unimp
		;;
	HELP)
		send '# NOP'
		send '# CANCEL'
		send '# OPTION'
		send '# BYE'
		send '# AUTH'
		send '# RESET'
		send '# END'
		send '# HELP'
		send '# SETDESC'
		send '# SETPROMPT'
		send '# SETKEYINFO'
		send '# SETREPEAT'
		send '# SETREPEATERROR'
		send '# SETREPEATOK'
		send '# SETERROR'
		send '# SETOK'
		send '# SETNOTOK'
		send '# SETCANCEL'
		send '# GETPIN'
		send '# CONFIRM'
		send '# MESSAGE'
		send '# SETQUALITYBAR'
		send '# SETQUALITYBAR_TT'
		send '# SETGENPIN'
		send '# SETGENPIN_TT'
		send '# GETINFO'
		send '# SETTITLE'
		send '# SETTIMEOUT'
		send '# CLEARPASSPHRASE'
		ok
		;;
	SETDESC)
		desc=$args
		ok
		;;
	SETPROMPT)
		prompt=$args
		ok
		;;
	SETKEYINFO)
		# TODO
		ok
		;;
	SETREPEAT)
		# TODO
		ok
		;;
	SETREPEATERROR)
		# TODO
		ok
		;;
	SETREPEATOK)
		# TODO
		ok
		;;
	SETERROR)
		# TODO
		ok
		;;
	SETOK)
		# TODO
		ok
		;;
	SETNOTOK)
		# TODO
		ok
		;;
	SETCANCEL)
		# TODO
		ok
		;;
	GETPIN)
		_prompt=$prompt
		[ -z "${_prompt}" ] && _prompt='PIN:'

		pin=$(dmenu -P -p "$prompt" < /dev/null)

		if [ $? -eq 0 ]; then
			[ -n "$pin" ] && send "D ${pin}"
			ok
		else
			send 'ERR 83886179 Operation cancelled <Pinentry>'
		fi
		;;
	CONFIRM)
		resp=$(printf 'No\nYes\n' | dmenu -p "$prompt")

		if [ $? -eq 0 ]; then
			case "$resp" in
			Yes)
				ok
				;;
			No)
				send 'ERR 114 Not confirmed <Unspecified source>'
				;;
			*)
				send 'ERR 99 Operation cancelled <Unspecified source>'
				;;
			esac
		else
			send 'ERR 99 Operation cancelled <Unspecified source>'
		fi
		;;
	MESSAGE)
		# TODO
		unimp
		;;
	SETQUALITYBAR)
		# TODO
		ok
		;;
	SETQUALITYBAR_TT)
		# TODO
		ok
		;;
	SETGENPIN)
		# TODO
		ok
		;;
	SETGENPIN_TT)
		# TODO
		ok
		;;
	GETINFO)
		arg0=$(echo "$args" | awk '{print $1}')
		case "${arg0}" in
		flavor)
			send 'D dmenu2'
			ok
			;;
		pid)
			send "D $$"
			ok
			;;
		version)
			send 'D 0.0.0'
			ok
			;;
		ttyinfo)
			send "D - - - - $(id -u)/$(id -g) 0"
			ok
			;;
		*)
			send 'ERR 83886360 IPC parameter error <Pinentry>'
			;;
		esac
		;;
	SETTITLE)
		title=$args
		ok
		;;
	SETTIMEOUT)
		# TODO
		ok
		;;
	CLEARPASSPHRASE)
		# TODO
		send 'ERR 83886337 General IPC error <Pinentry>'
		;;
	esac
done
