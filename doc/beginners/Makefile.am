# Makefile.am --- automake input file for systemtap tapset reference manual
## process this file with automake to produce Makefile.in

# publican will create a pdf file and a directory with *.html files,
# a directory called images that contain *.png and *.svg files, a
# Common_Content/images with more *.png and *.svg files and a
# Common_Content/css directory with *.css files.

DOC_INSTALL_DIR = $(DESTDIR)$(datadir)/doc/systemtap
SBG = SystemTap_Beginners_Guide
BEGIN_INSTALL_DIR = $(DOC_INSTALL_DIR)/$(SBG)

if BUILD_PUBLICAN
all: $(SBG).pdf $(SBG)/index.html

# publican isn't make -j safe. So cheat a little, always create pdf and html
# at the same time, then make html depend on pdf build for copying.
$(SBG).pdf:
	publican build --formats=pdf,html --langs=en-US && \
	mv build/en-US/pdf/*$(SBG)*.pdf $(SBG).pdf

$(SBG)/index.html: $(SBG).pdf
	mv build/en-US/html $(SBG)

clean-local:
	publican clean
	rm -f $(SBG).pdf
	rm -rf $(SBG)

install-data-hook:
	$(MKDIR_P) $(DOC_INSTALL_DIR)
	$(INSTALL_DATA) $(SBG).pdf $(DOC_INSTALL_DIR)
	$(MKDIR_P) $(DOC_INSTALL_DIR)/$(SBG)
	$(INSTALL_DATA) $(SBG)/*.html $(DOC_INSTALL_DIR)/$(SBG)
	$(MKDIR_P) $(DOC_INSTALL_DIR)/$(SBG)/images
	$(INSTALL_DATA) $(SBG)/images/*.png $(SBG)/images/*.svg \
			$(DOC_INSTALL_DIR)/$(SBG)/images
	$(MKDIR_P) $(DOC_INSTALL_DIR)/$(SBG)/Common_Content/css
	$(MKDIR_P) $(DOC_INSTALL_DIR)/$(SBG)/Common_Content/images
	$(INSTALL_DATA) $(SBG)/Common_Content/css/*.css \
			$(DOC_INSTALL_DIR)/$(SBG)/Common_Content/css
	$(INSTALL_DATA) $(SBG)/Common_Content/images/*.png \
			$(SBG)/Common_Content/images/*.svg \
			$(DOC_INSTALL_DIR)/$(SBG)/Common_Content/images

uninstall-local:
	rm -f $(DOC_INSTALL_DIR)/$(SBG).pdf
	rm -rf $(DOC_INSTALL_DIR)/$(SBG)
endif
