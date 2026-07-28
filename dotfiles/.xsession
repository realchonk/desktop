# vim: set ft=sh:

. ~/.profile

export _JAVA_AWT_WM_NONREPARENTING=1

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
bidle -t 900 &
dunst &

pkill xconsole

_has_command() {
	command -v "$1" >/dev/null 2>&1
}

if _has_command dbus-launch; then
	_dbus_env_file="/tmp/dbus-$(id -u)-env"
	if [ ! -e "${_dbus_env_file}" ]; then
		dbus-launch > "${_dbus_env_file}"
	fi
	. "${_dbus_env_file}"
fi

if [ -e "$HOME/.xsession.local" ]; then
	. "$HOME/.xsession.local"
fi

for prog in firefox thunderbird lxsession; do
	_has_command "$prog" && "$prog" &
done

if _has_command signal-desktop; then
	signal-desktop &
else _has_command gurk; then
	gurkx &
fi

exec dwm -l "$HOME/.local/share/dwm.log"
