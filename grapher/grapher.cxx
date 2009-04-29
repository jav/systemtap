#include "GraphWidget.hxx"
#include "StapParser.hxx"

#include <cmath>
#include <sstream>
#include <string>
#include <map>

#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <unistd.h>
#include <poll.h>

using namespace systemtap;

int main(int argc, char** argv)
{
   Gtk::Main app(argc, argv);

   Gtk::Window win;

   win.set_title("Grapher");
   win.set_default_size(600, 200);

   GraphWidget w;
   
   w.setExtents(0.0, 1.0, 5.0, 0.0);
   w.setLineWidth(2);

   StapParser stapParser(win, w);
   Glib::signal_io().connect(sigc::mem_fun(stapParser,
                                           &StapParser::ioCallback),
                             0,
                             Glib::IO_IN);
   win.add(w);
   w.show();

   Gtk::Main::run(win);

   return 0;
}
