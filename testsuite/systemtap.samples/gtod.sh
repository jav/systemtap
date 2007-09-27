#!/bin/sh

stap $1 -c "$2 $3" | sort

