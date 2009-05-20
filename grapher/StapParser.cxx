#include "StapParser.hxx"

#include <gtkmm/window.h>

namespace systemtap
{
using namespace std;

vector<string> commaSplit(const string& inStr, size_t pos = 0)
{
  size_t found = pos;
  vector<string> result;
  while (1)
    {

      size_t commaPos = inStr.find(',', found);
      string token
        = inStr.substr(found, (commaPos == string::npos
                               ? string::npos
                               : commaPos - 1 - found));
      result.push_back(token);
      if (commaPos != string::npos)
        found = commaPos + 1;
      else
        break;
    }
  return result;
}

bool StapParser::ioCallback(Glib::IOCondition ioCondition)
{
  using namespace std;
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
  string::size_type ret = string::npos;
  while ((ret = _buffer.find('\n')) != string::npos)
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
          else if ((found = dataString.find("%CSV:") == 0))
            {
              vector<string> tokens = commaSplit(dataString, found + 5);
              for (vector<string>::iterator tokIter = tokens.begin(),
                     e = tokens.end();
                     tokIter != e;
                   ++tokIter)
                {
                  DataMap::iterator setIter = _dataSets.find(*tokIter);
                  if (setIter != _dataSets.end())
                    _csv.elements.push_back(CSVData::Element(*tokIter,
                                                             setIter->second));
                }
            }
        }
      else
        {
          if (!_csv.elements.empty())
            {
              vector<string> tokens = commaSplit(dataString);
              int i = 0;
              double time;
              vector<string>::iterator tokIter = tokens.begin();
              std::istringstream timeStream(*tokIter++);
              timeStream >> time;
              for (vector<string>::iterator e = tokens.end();
                   tokIter != e;
                   ++tokIter, ++i)
                {
                  std::istringstream stream(*tokIter);
                  double data;
                  stream >>  data;
                  _csv.elements[i].second
                    ->data.push_back(std::make_pair(time, data));
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
        }
      _buffer.erase(0, ret + 1);
    }
  return true;
}
}
