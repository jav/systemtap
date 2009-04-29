#include "GraphData.hxx"
#include "GraphWidget.hxx"

#include <string>
namespace systemtap
{
class StapParser
{
  std::string _buffer;
  typedef std::map<std::string, std::tr1::shared_ptr<GraphData> > DataMap;
  DataMap _dataSets;
  Gtk::Window& _win;
  GraphWidget& _widget;
public:
  StapParser(Gtk::Window& win,
             GraphWidget& widget) : _win(win), _widget(widget) {}

  bool ioCallback(Glib::IOCondition ioCondition);

};
}
