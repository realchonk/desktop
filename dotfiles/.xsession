# vim: set ft=sh:

. ~/.profile

/usr/local/libexec/desktop/restore-keybinds

xset b off

sndioctl output.mute=1
sndioctl output.level=0.5

if [ -z "${SSH_AGENT_PID}" ]; then
	eval "$(ssh-agent)"
fi

bedstatus &
xbgcd &
xidle -delay 900 -timeout 900 -program /usr/local/bin/slock &
dunst &

pkill xconsole

if [ -e "$HOME/.xsession.local" ]; then
	. "$HOME/.xsession.local"
fi

if command -v dbus-launch >/dev/null 2>&1; then
	_dbus_env_file="/tmp/dbus-$(id -u)-env"
	if [ -e "${_dbus_env_file}" ]; then
		export DBUS_SESSION_BUS_ADDRESS="$(cat "${_dbus_env_file}")"
	else
		export $(dbus-launch)
		echo "${DBUS_SESSION_BUS_ADDRESS}" > "${_dbus_env_file}"
	fi
fi
if [ ! -e "/tmp/dbus-$(id -u)-env" ] && command -v dbus-launch >/dev/null 2>&1; then

else

exec dwm -l "$HOME/.local/share/dwm.log"
