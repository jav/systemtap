#! /bin/sh

# Redirect stdout/stderr to /dev/null before invoking the given test

exec $@ >/dev/null 2>&1
