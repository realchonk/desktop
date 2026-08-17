#!/bin/sh

set -e

creds=$(pass show storage)
path=$(echo "${creds}" | sed -n '/^path: /s///p')
username=$(echo "${creds}" | sed -n '/^username: /s///p')
password=$(echo "${creds}" | sed -n '/^password: /s///p')
uid=$(id -u)
gid=$(id -g)
mp='/mnt/storage'

if [ "$1" = '-u' ]; then
	doas umount "$mp"
else
	[ -f "${mp}/.mounted" ] || doas mount.cifs -o "username=${username},password=${password},uid=${uid},gid=${gid}" "${path}" "$mp"
fi
