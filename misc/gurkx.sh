#!/bin/sh
cmd=$(command -v gurk)
# gurk needs an absolute path for $0
exec st -n gurk -e "$cmd"
