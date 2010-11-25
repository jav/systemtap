#! /bin/sh

set -e

echo "Generating list of all kernel functions from /proc/kallsyms"
grep ' [tT] ' /proc/kallsyms | fgrep -v '[' | awk '{print $3}' > probes.current
echo "Found `wc -l < probes.current` of them: see probes.current"

echo "Compiling module"
python gen_code.py

ls -al kprobe_module.ko
echo "Run insmod kprobe_module.ko, if you dare."
