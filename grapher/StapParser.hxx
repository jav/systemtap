#include "GraphData.hxx"
#include "GraphWidget.hxx"

#include <string>
namespace systemtap
{
class StapParser
{
  std::string _buffer;
  typedef std::map<std::string, std::tr1::shared_ptr<GraphDataBase> > DataMap;
  DataMap _dataSets;
  CSVData _csv;
  Gtk::Window& _win;
  GraphWidget& _widget;
  int _errFd;
  int _inFd;
public:
  StapParser(Gtk::Window& win,
             GraphWidget& widget) : _win(win), _widget(widget), _errFd(-1),
                                    _inFd(-1)
  {
  }
  void parseData(std::tr1::shared_ptr<GraphDataBase> gdata,
                 double time, const std::string& dataString);
  bool ioCallback(Glib::IOCondition ioCondition);
  bool errIoCallback(Glib::IOCondition ioCondition);
  int getErrFd() { return _errFd; }
  void setErrFd(int fd) { _errFd = fd; }
  int getInFd() { return _inFd; }
  void setInFd(int fd) { _inFd = fd; }
};
}
