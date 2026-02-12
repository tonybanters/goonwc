#!/bin/sh
cd "$(dirname "$0")"
gdb -ex run -ex "set logging file /tmp/dwc_crash.log" -ex "set logging enabled on" -ex bt -ex quit ./zig-out/bin/dwc
