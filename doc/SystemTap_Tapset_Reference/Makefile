#Makefile for SystemTap_Tapset_Reference

XML_LANG	= en-US
BRAND		= common

#OTHER_LANGS	= as-IN bn-IN de-DE es-ES fr-FR gu-IN hi-IN it-IT ja-JP kn-IN ko-KR ml-IN mr-IN or-IN pa-IN pt-BR ru-RU si-LK ta-IN te-IN zh-CN zh-TW

# Extra Parameters start here

# Extra Parameters stop here
COMMON_CONFIG  = /usr/share/publican
include $(COMMON_CONFIG)/make/Makefile.common

#Pull the xml out of the tapset
TAPSETS=../../tapset
extract-xml:
	./extractxml.pl $(TAPSETS) ./en-US
