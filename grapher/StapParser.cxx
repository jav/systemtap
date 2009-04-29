#include "StapParser.hxx"

#include <gtkmm/window.h>

namespace systemtap
{
bool StapParser::ioCallback(Glib::IOCondition ioCondition)
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
  std::string::size_type ret = std::string::npos;
  while ((ret = _buffer.find('\n')) != std::string::npos)
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
}
