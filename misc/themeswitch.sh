#!/bin/sh

config_dir=${XDG_CONFIG_HOME:-$HOME/.config}
data_dir=${XDG_DATA_HOME:-$HOME/.local/share}
state=$data_dir/desktop/themeswitch.state
gtk2_light=${THEMESWITCH_GTK2_LIGHT:-Adwaita}
gtk2_dark=${THEMESWITCH_GTK2_DARK:-Adwaita-dark}
moz_count=0

usage() {
	echo "usage: themeswitch <light|dark|toggle|status>" >&2
	echo "       themeswitch -h" >&2
	exit 1
}

help() {
	cat <<'EOF'
usage: themeswitch [light|dark|toggle|status]

Switch the desktop color scheme globally between light and dark.
With no argument, toggles the current scheme.

Affected:
  GTK 2        gtk-theme-name in ~/.config/gtk-2.0/gtkrc-2.0
  GTK 3 / 4    gtk-application-prefer-dark-theme in
               ~/.config/gtk-3.0/settings.ini and gtk-4.0/settings.ini
  GNOME        org.gnome.desktop.interface color-scheme (libadwaita / GTK4)
  Firefox      layout.css.prefers-color-scheme.content-override, per profile
  Thunderbird  same pref, per profile
  st           SIGUSR1 (dark) / SIGUSR2 (light) to running terminals
  Qt           color_scheme_path in ~/.config/qt5ct/qt5ct.conf and qt6ct/qt6ct.conf

Firefox, Thunderbird and Qt apps apply the change on their next start.

Environment:
  THEMESWITCH_GTK2_LIGHT   GTK2 theme for the light scheme (default: Adwaita)
  THEMESWITCH_GTK2_DARK    GTK2 theme for the dark scheme  (default: Adwaita-dark)
  THEMESWITCH_QT_LIGHT     qt5ct/6ct color-scheme file for the light scheme
  THEMESWITCH_QT_DARK      qt5ct/6ct color-scheme file for the dark scheme

Commands:
  light       Switch to the light scheme.
  dark        Switch to the dark scheme.
  toggle      Switch to the opposite of the current scheme (default).
  status      Print the current scheme (light or dark).
  -h, --help  Show this help.
EOF
}

set_kv() (
	kv_file=$1 kv_key=$2 kv_val=$3
	mkdir -p "$(dirname "$kv_file")"
	if [ ! -f "$kv_file" ]; then
		printf '%s=%s\n' "$kv_key" "$kv_val" > "$kv_file"
	else
		awk -v k="$kv_key" -v v="$kv_val" '
			{ if ($0 ~ "^" k "=") { print k "=" v; r=1 } else print }
			END { if (!r) print k "=" v }
		' "$kv_file" > "$kv_file".tmp && mv "$kv_file".tmp "$kv_file"
	fi
)

set_ini() (
	si_file=$1 si_key=$2 si_val=$3
	mkdir -p "$(dirname "$si_file")"
	if [ ! -f "$si_file" ]; then
		printf '[Settings]\n%s=%s\n' "$si_key" "$si_val" > "$si_file"
	else
		awk -v k="$si_key" -v v="$si_val" '
			{
				if ($0 ~ "^" k "=") { print k "=" v; r=1 } else print
				if ($0 ~ /^\[Settings\][ \t]*$/) s=1
			}
			END {
				if (!r) {
					if (s) print k "=" v
					else printf "[Settings]\n%s=%s\n", k, v
				}
			}
		' "$si_file" > "$si_file".tmp && mv "$si_file".tmp "$si_file"
	fi
)

current_theme() {
	if [ -r "$state" ]; then
		cat "$state"
		return
	fi
	if [ -f "$config_dir/gtk-3.0/settings.ini" ] \
	   && grep -q '^gtk-application-prefer-dark-theme=1' "$config_dir/gtk-3.0/settings.ini"; then
		echo dark
	else
		echo light
	fi
}

set_state() {
	mkdir -p "$(dirname "$state")"
	echo "$1" > "$state"
}

apply_gsettings() {
	command -v gsettings >/dev/null 2>&1 || return 0
	if [ "$1" = dark ]; then
		gsettings set org.gnome.desktop.interface color-scheme 'prefer-dark' 2>/dev/null
	else
		gsettings set org.gnome.desktop.interface color-scheme 'default' 2>/dev/null
	fi
}

apply_st() {
	command -v pkill >/dev/null 2>&1 || return 0
	if [ "$1" = dark ]; then
		pkill -USR1 -x st 2>/dev/null
	else
		pkill -USR2 -x st 2>/dev/null
	fi
}

apply_qt() {
	if [ "$1" = dark ]; then
		cs=${THEMESWITCH_QT_DARK:-}
	else
		cs=${THEMESWITCH_QT_LIGHT:-}
	fi
	[ -n "$cs" ] || return 0
	for conf in "$HOME/.config/qt5ct/qt5ct.conf" "$HOME/.config/qt6ct/qt6ct.conf"; do
		[ -f "$conf" ] || continue
		set_kv "$conf" color_scheme_path "$cs"
	done
}

apply_mozilla() {
	mz_prof=$1 mz_val=0
	[ "$2" = dark ] && mz_val=1
	mz_js=$mz_prof/user.js
	mkdir -p "$mz_prof"
	if [ -f "$mz_js" ]; then
		grep -v '^user_pref("layout.css.prefers-color-scheme.content-override"' "$mz_js" > "$mz_js".tmp 2>/dev/null
		mv "$mz_js".tmp "$mz_js"
	fi
	printf 'user_pref("layout.css.prefers-color-scheme.content-override", %d);\n' "$mz_val" >> "$mz_js"
	moz_count=$((moz_count + 1))
}

apply_firefox() {
	dir=$HOME/.mozilla/firefox
	[ -d "$dir" ] || return 0
	for d in "$dir"/*/; do
		[ -f "${d%/}/prefs.js" ] || continue
		apply_mozilla "${d%/}" "$1"
	done
}

apply_thunderbird() {
	dir=$HOME/.thunderbird
	[ -d "$dir" ] || return 0
	for d in "$dir"/*/; do
		[ -f "${d%/}/prefs.js" ] || continue
		apply_mozilla "${d%/}" "$1"
	done
}

apply_theme() {
	theme=$1
	val=0
	[ "$theme" = dark ] && val=1
	gtk2=$gtk2_light
	[ "$theme" = dark ] && gtk2=$gtk2_dark
	moz_count=0

	set_kv "$config_dir/gtk-2.0/gtkrc-2.0" gtk-theme-name "\"$gtk2\""
	set_ini "$config_dir/gtk-3.0/settings.ini" gtk-application-prefer-dark-theme "$val"
	set_ini "$config_dir/gtk-4.0/settings.ini" gtk-application-prefer-dark-theme "$val"
	apply_gsettings "$theme"
	apply_firefox "$theme"
	apply_thunderbird "$theme"
	apply_st "$theme"
	apply_qt "$theme"
	set_state "$theme"

	echo "switched to $theme"
	if [ "$moz_count" -gt 0 ]; then
		echo "mozilla: $moz_count profile(s) updated; restart to apply"
	fi
}

[ "$#" -gt 0 ] || set -- toggle

case "$1" in
light|dark)
	apply_theme "$1"
	;;
toggle)
	cur=$(current_theme)
	if [ "$cur" = dark ]; then
		apply_theme light
	else
		apply_theme dark
	fi
	;;
status)
	current_theme
	;;
-h|--help|help)
	help
	;;
*)
	usage
	;;
esac

# vim: set ts=4 sw=4 noet:
