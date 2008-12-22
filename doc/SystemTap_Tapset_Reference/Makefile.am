# Makefile.am --- automake input file for systemtap tapset reference manual
## process this file with automake to produce Makefile.in

DOC_INSTALL_DIR = $(DESTDIR)$(datadir)/doc/systemtap
MAN_INSTALL_DIR = $(DESTDIR)$(mandir)/man5
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

if BUILD_DOCS
all: pdfdocs htmldocs mandocs
xmldocs: docproc
	SRCTREE=$(SRCTREE) $(DOCPROC) doc $(abs_srcdir)/tapsets.tmpl > tapsets.xml

htmldocs: xmldocs
	xmlto html -o tapsets tapsets.xml

pdfdocs: xmldocs
	xmlto pdf tapsets.xml

mandocs: xmldocs
	xmlto man -o man5 tapsets.xml

#FIXME need to figure out where to install things appropriately
#installmandocs: mandocs
install-data-hook:
	$(MKDIR_P)  $(DOC_INSTALL_DIR)
	$(INSTALL_DATA) tapsets.pdf $(DOC_INSTALL_DIR)
	$(MKDIR_P)  $(MAN_INSTALL_DIR)
	$(INSTALL_DATA) man5/* $(MAN_INSTALL_DIR)
	$(MKDIR_P)  $(HTML_INSTALL_DIR)
	$(INSTALL_DATA) tapsets/* $(HTML_INSTALL_DIR)
endif
