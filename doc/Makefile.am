# Makefile.am --- automake input file for systemtap docs
## process this file with automake to produce Makefile.in

PDF_FILES = tutorial.pdf langref.pdf
DOC_INSTALL_DIR = $(DESTDIR)$(datadir)/doc/systemtap

if BUILD_DOCS
all-local: $(PDF_FILES)

clean-local:
	rm -f *.pdf *.out *.log *.aux *.toc *.lot *.idx *.glo

install-data-hook:
	$(MKDIR_P) $(DOC_INSTALL_DIR)
	$(INSTALL_DATA) $(PDF_FILES) $(DOC_INSTALL_DIR)

uninstall-local:
	@list='$(PDF_FILES)'; for p in $$list; do \
	  echo " rm -f '$(DOC_INSTALL_DIR)/$$p'"; \
	  rm -f "$(DOC_INSTALL_DIR)/$$p"; \
	done
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

