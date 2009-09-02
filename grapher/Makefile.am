if BUILD_GRAPHER
bin_PROGRAMS = stapgraph

stapgraph_CPPFLAGS =  -DPKGDATADIR='"${pkgdatadir}"'
stapgraph_CXXFLAGS = $(gtkmm_CFLAGS) -Wall -Werror
stapgraph_SOURCES = grapher.cxx StapParser.cxx Graph.cxx GraphWidget.cxx CairoWidget.cxx
stapgraph_LDADD = $(gtkmm_LIBS)
dist_pkgdata_DATA = graph-dialog.glade
endif
