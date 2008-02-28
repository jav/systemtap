# Makefile.am --- automake input file for systemtap docs
## process this file with automake to produce Makefile.in

if BUILD_DOCS
all-local: tutorial.pdf langref.pdf

clean-local:
	rm -f *.pdf *.out *.log *.aux *.toc *.lot *.idx *.glo
endif

SUFFIXES = ps pdf dvi ps tex

.ps.pdf:
	ps2pdf -r600 $<

.dvi.ps:
	dvips -t letter -o $@ $<

.tex.dvi:
	pwd=`pwd`; cd $(srcdir); \
	latex -output-directory=$$pwd $<; \
	touch $*.glo \
	makeindex $*.glo -s nomencl.ist -o $*.gls \
	latex -output-directory=$$pwd $<; \
	latex -output-directory=$$pwd $<; \
	latex -output-directory=$$pwd $<

EXTRA_DIST = tutorial.tex langref.tex tutorial
