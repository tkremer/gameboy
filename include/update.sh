#!/bin/bash

# I use this script to include library header files into separate project git repositories. I don't like submodules.
# Copyright (c) 2019 Thomas Kremer

# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 or 3 as
# published by the Free Software Foundation.

# run from parent directory.
# usage:
#   include/update.sh
#
#   updates all files from ../include/ to include/ that are already there.

src="../include"
dest="include"

for f in "$src"/*; do
  fname="${f#"$src"/}"
  f2="$dest/$fname"
  if [ -f "$f" -a -f "$f2" -a "$f" -nt "$f2" ]; then
    if [ -L "$f" ]; then
      echo "can't handle symlink \"$f\" yet." >&2
      continue;
    fi
    if [ -L "$f2" ]; then
      rm "$f2"
    fi
    echo "installing new version of \"$fname\"."
    cp -a "$f" "$f2"
  fi
done
