. ~/.profile

/usr/local/libexec/desktop/restore-keybinds

xset b off

sndioctl output.mute=1
sndioctl output.level=1

bedstatus &
xbgcd &
xidle -delay 900 -timeout 900 -program /usr/local/bin/slock &

pkill xconsole

exec dwm
