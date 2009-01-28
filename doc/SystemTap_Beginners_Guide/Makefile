#Makefile for SystemTap_Beginners_Guide

XML_LANG	= en-US

BRAND = RedHat
#	please define the BRAND in the pre:: section below
#SHOW_REMARKS = 1

#OTHER_LANGS	= as-IN bn-IN de-DE es-ES fr-FR gu-IN hi-IN it-IT ja-JP kn-IN ko-KR ml-IN mr-IN or-IN pa-IN pt-BR ru-RU si-LK ta-IN te-IN zh-CN zh-TW

CONDITION=$(BRAND)

COMMON_CONFIG  = /usr/share/publican
include $(COMMON_CONFIG)/make/Makefile.common

pre::
	cp -a ../../testsuite en-US/extras/;

post::
	rm -rf en-US/extras/testsuite
	sed -i -e 's/<productname>Fedora/<productname>Red Hat Enterprise Linux/g' en-US/Book_Info.xml;
	sed -i -e 's/<productnumber>10/<productnumber>5/g' en-US/Book_Info.xml;