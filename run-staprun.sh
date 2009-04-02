#!/bin/sh

exec sudo env builddir="${builddir}" \
     	      SYSTEMTAP_STAPIO="${builddir}/run-stapio" \
	      SYSTEMTAP_STAPRUN="$0" \
     	  "${builddir}/staprun" ${1+"$@"}
