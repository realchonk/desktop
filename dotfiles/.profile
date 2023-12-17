# $OpenBSD: dot.profile,v 1.8 2022/08/10 07:40:37 tb Exp $
#
# sh/ksh initialization

PATH=$HOME/.local/bin:$HOME/.cargo/bin:/bin:/sbin:/usr/bin:/usr/sbin:/usr/X11R6/bin:/usr/local/bin:/usr/local/sbin
export PATH HOME TERM

export ENV=/etc/mykshrc
export LANG=en_US.UTF-8
export GOT_AUTHOR="Benjamin Stürz <benni@stuerz.xyz>"
export QT_QPA_PLATFORMTHEME=qt5ct
