# $OpenBSD: dot.profile,v 1.8 2022/08/10 07:40:37 tb Exp $
#
# sh/ksh initialization

append_path() {
	[ -e "$1" ] && PATH="$PATH:$1"
}

PATH=
append_path "$HOME/.local/bin"
append_path "$HOME/.cargo/bin"
append_path "/bin"
append_path "/sbin"
append_path "/usr/bin"
append_path "/usr/sbin"
append_path "/usr/X11R6/bin"
append_path "/usr/local/bin"
append_path "/usr/local/sbin"
for jdk in 21 17 11 1.8.0; do
	append_path "/usr/local/jdk-$jdk/bin" && break
done
PATH=$(echo "$PATH" | sed 's/^://')

if [ -e /etc/mykshrc ]; then
	ENV=/etc/mykshrc
elif [ -e "$HOME/.kshrc" ]; then
	ENV="$HOME/.kshrc"
fi

[ -z "${XDG_RUNTIME_DIR}" ] && XDG_RUNTIME_DIR="/tmp/$(id -un)"
mkdir -p "${XDG_RUNTIME_DIR}"

export PATH HOME TERM ENV XDG_RUNTIME_DIR
export LANG=en_US.UTF-8
export GOT_AUTHOR="Benjamin Stürz <benni@stuerz.xyz>"
export QT_QPA_PLATFORMTHEME=qt5ct
[ "$(uname)" = 'OpenBSD' ] && export LIBCLANG_PATH=/usr/local/llvm16/lib

