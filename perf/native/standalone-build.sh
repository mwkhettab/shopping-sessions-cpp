#!/bin/sh
set -eu
mkdir -p build
set --
output=build/shopping
if [ "${DIAGNOSTICS:-0}" = 1 ]; then
  set -- -DSHOPPING_DIAGNOSTICS
  output=build/shopping-profile
fi
"${CXX:-c++}" -std=c++20 -O3 -DNDEBUG -pthread -Wall -Wextra "$@" cpp/main.cpp -o "$output"
