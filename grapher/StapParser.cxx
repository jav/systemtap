#include "StapParser.hxx"

#include <gtkmm/window.h>
#include <iostream>

namespace systemtap
{
  using namespace std;
  using namespace std::tr1;

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

  void StapParser::parseData(shared_ptr<GraphDataBase> gdata,
                             double time, const string& dataString)
  {
    std::istringstream stream(dataString);
    shared_ptr<GraphData<double> > dblptr;
    shared_ptr<GraphData<string> > strptr;
    dblptr = dynamic_pointer_cast<GraphData<double> >(gdata);
    if (dblptr)
      {
        double data;
        stream >> data;
        dblptr->times.push_back(time);
        dblptr->data.push_back(data);
      }
    else if ((strptr = std::tr1
              ::dynamic_pointer_cast<GraphData<string> >(gdata))
             != 0)
      {
        strptr->times.push_back(time);
        strptr->data.push_back(dataString);
      }
  }

  size_t findTaggedValue(const string& src, const char* tag, string& result)
  {
    size_t found;
    if ((found = src.find(tag)) != string::npos)
        result = src.substr(strlen(tag));
    return found;
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
          // %DataSet and %CSV declare a data set; all other statements begin with
          // the name of a data set.
          size_t found;
          if (dataString[0] == '%')
            {
              if ((found = dataString.find("%DataSet:") == 0))
                {
                  std::string setName;
                  int hexColor;
                  double scale;
                  std::string style;
                  std::istringstream stream(dataString.substr(9));
                  stream >> setName >> scale >> std::hex >> hexColor
                         >> style;
                  if (style == "bar" || style == "dot")
                    {
                      shared_ptr<GraphData<double> >
                        dataSet(new GraphData<double>);
                      if (style == "dot")
                        dataSet->style = GraphDataBase::DOT;
                      dataSet->color[0] = (hexColor >> 16) / 255.0;
                      dataSet->color[1] = ((hexColor >> 8) & 0xff) / 255.0;
                      dataSet->color[2] = (hexColor & 0xff) / 255.0;
                      dataSet->scale = scale;
                      _dataSets.insert(std::make_pair(setName, dataSet));
                      _widget.addGraphData(dataSet);
                    }
                  else if (style == "discreet")
                    {
                      shared_ptr<GraphData<string> >
                        dataSet(new GraphData<string>);
                      dataSet->style = GraphDataBase::EVENT;
                      dataSet->color[0] = (hexColor >> 16) / 255.0;
                      dataSet->color[1] = ((hexColor >> 8) & 0xff) / 255.0;
                      dataSet->color[2] = (hexColor & 0xff) / 255.0;
                      dataSet->scale = scale;
                      _dataSets.insert(std::make_pair(setName, dataSet));
                      _widget.addGraphData(dataSet);
                    }
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
                        _csv.elements
                          .push_back(CSVData::Element(*tokIter,
                                                      setIter->second));
                    }
                }
              else
                {
                  cerr << "Unknown declaration " << dataString << endl;
                }
            }
          else
            {
              std::istringstream stream(dataString);
              string setName;
              stream >> setName;
              DataMap::iterator itr = _dataSets.find(setName);
              if (itr != _dataSets.end())
                {
                  shared_ptr<GraphDataBase> gdata = itr->second;
                  string decl;
                  // Hack: scan from the beginning of dataString again
                  if (findTaggedValue(dataString, "%Title", decl)
                      != string::npos)
                    {
                      gdata->title = decl;
                    }
                  else if (findTaggedValue(dataString, "%XAxisTitle:", decl)
                           != string::npos)
                    {
                      gdata->xAxisText = decl;
                    }
                  else if (findTaggedValue(dataString, "%YAxisTitle:", decl)
                           != string::npos)
                    {
                      gdata->yAxisText = decl;
                    }
                  else if ((found = dataString.find("%YMax:")) != string::npos)
                    {
                      double ymax;
                      std::istringstream stream(dataString.substr(found));
                      stream >> ymax;
                      gdata->scale = ymax;
                    }

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
                          parseData(_csv.elements[i].second, time, *tokIter);
                        }
                    }
                  else
                    {
                      double time;
                      string data;
                      stream >> time >> data;
                      parseData(itr->second, time, data);
                    }
                }
            }
          _buffer.erase(0, ret + 1);
        }
      return true;
    }
}
