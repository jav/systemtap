# Makefile.am --- automake input file for systemtap tapset reference manual
## process this file with automake to produce Makefile.in

DOC_INSTALL_DIR = $(DESTDIR)$(datadir)/doc/systemtap
MAN_INSTALL_DIR = $(DESTDIR)$(mandir)/man3
HTML_INSTALL_DIR = $(DESTDIR)$(datadir)/doc/systemtap/tapsets


###
# The build process is as follows (targets):
#              (xmldocs) [by docproc]
# file.tmpl --> file.xml +--> file.ps   (psdocs)   [by xmlto]
#                        +--> file.pdf  (pdfdocs)  [by xmlto]
#                        +--> DIR=file  (htmldocs) [by xmlto]
#                        +--> man/      (mandocs)  [by xmlto]

noinst_PROGRAMS = docproc
SRCTREE=$(abs_top_srcdir)/
DOCPROC=$(abs_builddir)/docproc

if BUILD_REFDOCS
all: tapsets.pdf stamp-htmldocs stamp-mandocs
tapsets.xml: docproc $(shell find $(SRCTREE) -name '*.stp')
	SRCTREE=$(SRCTREE) $(DOCPROC) doc $(abs_srcdir)/tapsets.tmpl > tapsets.xml.new
	if cmp tapsets.xml.new tapsets.xml >/dev/null ; then \
		echo tapsets.xml unchanged; \
	else \
		mv tapsets.xml.new tapsets.xml; \
	fi

stamp-htmldocs: tapsets.xml
	xmlto html -o tapsets tapsets.xml
	touch stamp-htmldocs

# bump up the allocated space so "xmlto pdf" works
tapsets.pdf: tapsets.xml
	env pool_size=2000000 hash_extra=2000000 xmlto pdf tapsets.xml

stamp-mandocs: tapsets.xml
	xmlto man -o man3 tapsets.xml
	touch stamp-mandocs

#FIXME need to figure out where to install things appropriately
#installmandocs: mandocs
install-data-hook:
	$(MKDIR_P)  $(DOC_INSTALL_DIR)
	$(INSTALL_DATA) tapsets.pdf $(DOC_INSTALL_DIR)
	$(MKDIR_P)  $(MAN_INSTALL_DIR)
	$(INSTALL_DATA) man3/* $(MAN_INSTALL_DIR)
	$(MKDIR_P)  $(HTML_INSTALL_DIR)
	$(INSTALL_DATA) tapsets/* $(HTML_INSTALL_DIR)
endif
