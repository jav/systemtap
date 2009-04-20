#include "GraphWidget.hxx"

#include <cmath>
#include <sstream>
#include <string>
#include <map>

#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <unistd.h>
#include <poll.h>

using namespace systemtap;

class StapParser
{
  Glib::ustring _buffer;
  typedef std::map<std::string, std::tr1::shared_ptr<GraphData> > DataMap;
  DataMap _dataSets;
  Gtk::Window& _win;
  GraphWidget& _widget;
public:
  StapParser(Gtk::Window& win,
             GraphWidget& widget) : _win(win), _widget(widget) {}
  
  bool ioCallback(Glib::IOCondition ioCondition)
  {
    if ((ioCondition & Glib::IO_IN) == 0)
      return true;
    char buf[256];
    ssize_t bytes_read = 0;
    bytes_read = read(0, buf, sizeof(buf) - 1);
    if (bytes_read <= 0)
      {
        _win.hide();
        return true;
      }
    buf[bytes_read] = '\0';
    _buffer += buf;
    Glib::ustring::size_type ret = Glib::ustring::npos;
    while ((ret = _buffer.find('\n')) != Glib::ustring::npos)
      {
        Glib::ustring dataString(_buffer, 0, ret);
        if (dataString[0] == '%')
          {
            size_t found;
            if ((found = dataString.find("%Title:") == 0))
              {
                std::string title = dataString.substr(7);
                _widget.setTitle(title);
              }
            else if ((found = dataString.find("%XAxisTitle:") == 0))
              {
                _widget.setXAxisText(dataString.substr(12));
              }
            else if ((found = dataString.find("%YAxisTitle:") == 0))
              {
                _widget.setYAxisText(dataString.substr(12));
              }
            else if ((found = dataString.find("%YMax:") == 0))
              {
                double ymax;
                std::istringstream stream(dataString.substr(6));
                stream >> ymax;
                // _gdata->scale = ymax;
              }
            else if ((found = dataString.find("%DataSet:") == 0))
              {
                std::tr1::shared_ptr<GraphData> dataSet(new GraphData);
                std::string setName;
                int hexColor;
                std::string style;
                std::istringstream stream(dataString.substr(9));
                stream >> setName >> dataSet->scale >> std::hex >> hexColor
                       >> style;
                dataSet->color[0] = (hexColor >> 16) / 255.0;
                dataSet->color[1] = ((hexColor >> 8) & 0xff) / 255.0;
                dataSet->color[2] = (hexColor & 0xff) / 255.0;
                if (style == "dot")
                  dataSet->style = GraphData::DOT;
                _dataSets.insert(std::make_pair(setName, dataSet));
                _widget.addGraphData(dataSet);
              }
          }
        else
          {
            std::string dataSet;
            double time;
            double data;
            std::istringstream stream(dataString);
            stream >> dataSet >> time >> data;
            DataMap::iterator itr = _dataSets.find(dataSet);
            if (itr != _dataSets.end())
              itr->second->data.push_back(std::make_pair(time, data));
          }
        _buffer.erase(0, ret + 1);
      }
    return true;
  }
};

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
