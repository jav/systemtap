if BUILD_GRAPHER
bin_PROGRAMS = stapgraph
man_MANS = stapgraph.1

# Note that since libglade_CFLAGS/libglade_LIBS variables are a
# "superset" of the gtkmm_CFLAGS/gtkmm_LIBS variabes, we'll just use
# the libglade ones.
stapgraph_CPPFLAGS = -DPKGDATADIR='"${pkgdatadir}"'
stapgraph_CXXFLAGS = $(libglade_CFLAGS) -Wall -Werror
stapgraph_SOURCES = grapher.cxx StapParser.cxx Graph.cxx GraphWidget.cxx CairoWidget.cxx GraphStyle.cxx
stapgraph_LDADD = $(libglade_LIBS)
stapgraph_LDFLAGS = -Wl,--as-needed
dist_pkgdata_DATA = graph-dialog.glade stap-start.glade processwindow.glade
endif
