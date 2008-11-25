# Makefile.am --- automake input file for systemtap tapset reference manual
## process this file with automake to produce Makefile.in

###
# The build process is as follows (targets):
#              (xmldocs) [by docproc]
# file.tmpl --> file.xml +--> file.ps   (psdocs)   [by xmlto]
#                        +--> file.pdf  (pdfdocs)  [by xmlto]
#                        +--> DIR=file  (htmldocs) [by xmlto]
#                        +--> man/      (mandocs)  [by xmlto]

bin_PROGRAMS = docproc
docproc_SOURCES = docproc.c

all: pdfdocs htmldocs mandocs


SRCTREE=$(abs_top_srcdir)/
DOCPROC=$(abs_builddir)/docproc

xmldocs: docproc
	SRCTREE=$(SRCTREE) $(DOCPROC) doc $(abs_srcdir)/tapsets.tmpl > tapsets.xml

htmldocs: xmldocs
	xmlto html -o tapset tapsets.xml

pdfdocs: xmldocs
	xmlto pdf tapsets.xml

mandocs: xmldocs
	xmlto man tapsets.xml

#FIXME need to figure out where to install things appropriately
#installmandocs: mandocs
#	$(MKDIR_P) /usr/local/man/man5/
#	install Documentation/DocBook/man/*.5.gz /usr/local/man/man5/

