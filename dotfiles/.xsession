# vim: set ft=sh:

. ~/.profile

/usr/local/libexec/desktop/restore-keybinds

xset b off

case "$(uname)" in
OpenBSD)
	sndioctl output.mute=1
	sndioctl output.level=0.5
	;;
Linux)
	wpctl set-mute '@DEFAULT_SINK@' 1
	wpctl set-level '@DEFAULT_SINK@' 0.5
	;;
esac

if [ -z "${SSH_AGENT_PID}" ]; then
	eval "$(ssh-agent)"
fi

bedstatus &
xbgcd &
xidle -delay 900 -timeout 900 -program /usr/local/bin/slock &
dunst &

pkill xconsole

if command -v dbus-launch >/dev/null 2>&1; then
	_dbus_env_file="/tmp/dbus-$(id -u)-env"
	if [ ! -e "${_dbus_env_file}" ]; then
		dbus-launch > "${_dbus_env_file}"
	fi
	. "${_dbus_env_file}"
fi

if [ -e "$HOME/.xsession.local" ]; then
	. "$HOME/.xsession.local"
fi

exec dwm -l "$HOME/.local/share/dwm.log"
