#!/bin/sh

is_muted() {
	case "$(uname)" in
	OpenBSD)
		sndioctl output.mute | cut -d= -f2
		;;
	Linux)
		if [ "$(wpctl get-volume '@DEFAULT_SINK@' | sed -n '/^Volume: .*\[MUTED\]/p')" ]; then
			echo 1
		else
			echo 0
		fi
	esac
}

set_muted() {
	case "$(uname)" in
	OpenBSD)
		sndioctl "output.mute=$1"
		;;
	Linux)
		wpctl set-mute '@DEFAULT_SINK@' "$1"
		;;
	esac
}

get_volume() {
	case "$(uname)" in
	OpenBSD)
		sndioctl output.level | cut -d= -f2
		;;
	Linux)
		wpctl get-volume '@DEFAULT_SINK@' | sed -n '/^Volume: /s/^Volume: \([0-9.]*\).*$/\1/p'
		;;
	esac
}

set_volume() {
	case "$(uname)" in
	OpenBSD)
		sndioctl "output.level=$1"
		;;
	Linux)
		wpctl set-volume '@DEFAULT_SINK@' "$1"
		;;
	esac
}

is_mic_muted() {
	case "$(uname)" in
	OpenBSD)
		sndioctl input.mute | cut -d= -f2
		;;
	Linux)
		if [ "$(wpctl get-volume '@DEFAULT_SOURCE@' | sed -n '/^Volume: .*\[MUTED\]/p')" ]; then
			echo 1
		else
			echo 0
		fi
	esac
}

set_mic_muted() {
	case "$(uname)" in
	OpenBSD)
		sndioctl "input.mute=$1"
		;;
	Linux)
		wpctl set-mute '@DEFAULT_SOURCE@' "$1"
		;;
	esac
}

get_mic_volume() {
	case "$(uname)" in
	OpenBSD)
		sndioctl input.level | cut -d= -f2
		;;
	Linux)
		wpctl get-volume '@DEFAULT_SOURCE@' | sed -n '/^Volume: /s/^Volume: \([0-9.]*\).*$/\1/p'
		;;
	esac
}

set_mic_volume() {
	case "$(uname)" in
	OpenBSD)
		sndioctl "input.level=$1"
		;;
	Linux)
		wpctl set-volume '@DEFAULT_SOURCE@' "$1"
		;;
	esac
}

usage() {
	echo "usage: audioctl <command> [args]" >&2
	echo "       audioctl -h" >&2
	exit 1
}

help() {
	cat <<'EOF'
usage: audioctl <command> [args]

Control the default audio output (speakers) and input (microphone).

Output commands:
  get-volume            Print the output volume (0.0-1.0).
  set-volume <volume>   Set the output volume (e.g. 0.5).
  is-muted              Print 1 if the output is muted, otherwise 0.
  set-muted <0/1>       Mute (1) or unmute (0) the output.

Microphone commands:
  get-mic-volume            Print the microphone volume (0.0-1.0).
  set-mic-volume <volume>   Set the microphone volume (e.g. 0.5).
  is-mic-muted              Print 1 if the microphone is muted, otherwise 0.
  set-mic-muted <0/1>       Mute (1) or unmute (0) the microphone.

Other:
  -h, --help            Show this help.
EOF
}

[ "$#" -gt 0 ] || usage

case "$1" in
-h|--help|help)
	help
	exit 0
	;;
get-volume)
	[ "$#" -eq 1 ] || { echo "usage: audioctl get-volume" >&2; exit 1; }
	get_volume
	;;
set-volume)
	[ "$#" -eq 2 ] || { echo "usage: audioctl set-volume <volume>" >&2; exit 1; }
	set_volume "$2"
	;;
is-muted)
	[ "$#" -eq 1 ] || { echo "usage: audioctl is-muted" >&2; exit 1; }
	is_muted
	;;
set-muted)
	[ "$#" -eq 2 ] || { echo "usage: audioctl set-muted <0/1>" >&2; exit 1; }
	set_muted "$2"
	;;
get-mic-volume)
	[ "$#" -eq 1 ] || { echo "usage: audioctl get-mic-volume" >&2; exit 1; }
	get_mic_volume
	;;
set-mic-volume)
	[ "$#" -eq 2 ] || { echo "usage: audioctl set-mic-volume <volume>" >&2; exit 1; }
	set_mic_volume "$2"
	;;
is-mic-muted)
	[ "$#" -eq 1 ] || { echo "usage: audioctl is-mic-muted" >&2; exit 1; }
	is_mic_muted
	;;
set-mic-muted)
	[ "$#" -eq 2 ] || { echo "usage: audioctl set-mic-muted <0/1>" >&2; exit 1; }
	set_mic_muted "$2"
	;;
*)
	usage
	;;
esac

# vim: set ts=4 sw=4 noet:
