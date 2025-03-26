. ~/.profile

/usr/local/libexec/desktop/restore-keybinds

xset b off

sndioctl output.mute=1
sndioctl output.level=0.5

if ! pgrep -u "$(id -u)" ssh-agent >/dev/null; then
	eval $(ssh-agent)
fi

bedstatus &
xbgcd &
xidle -delay 900 -timeout 900 -program /usr/local/bin/slock &

pkill xconsole

if [ -e "$HOME/.xsession.local" ]; then
	. "$HOME/.xsession.local"
fi

exec dwm -l "$HOME/.local/share/dwm.log"
