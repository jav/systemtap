#include "StapParser.hxx"

#include <unistd.h>

#include <gtkmm/window.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <cstring>

#include <boost/algorithm/string.hpp>
#include <boost/range.hpp>

namespace systemtap
{
  using namespace std;
  using namespace std::tr1;

vector<string> commaSplit(const boost::sub_range<Glib::ustring>& range)
{
  using namespace boost;
  vector<string> result;
  split(result, range, is_any_of(","));
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
    else if ((strptr = dynamic_pointer_cast<GraphData<string> >(gdata))
             != 0)
      {
        strptr->times.push_back(time);
        strptr->data.push_back(dataString);
      }
  }

  bool findTaggedValue(const string& src, const char* tag, string& result)
  {
    using namespace boost;
    sub_range<const string> found = find_first(src, tag);
    if (found.empty())
      return false;
    result.insert(result.end(),found.end(), src.end());
    return true;
  }

  bool StapParser::ioCallback(Glib::IOCondition ioCondition)
    {
      using namespace std;
      using std::tr1::shared_ptr;
      using namespace boost;
      if (ioCondition & Glib::IO_HUP)
        {
          _win->hide();
          return true;
        }
      if ((ioCondition & Glib::IO_IN) == 0)
        return true;
      char buf[256];
      ssize_t bytes_read = 0;
      bytes_read = read(_inFd, buf, sizeof(buf) - 1);
      if (bytes_read <= 0)
        {
          _win->hide();
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
          sub_range<Glib::ustring> found;
          if (dataString[0] == '%')
            {
              if ((found = find_first(dataString, "%DataSet:")))
                {
                  string setName;
                  int hexColor;
                  double scale;
                  string style;
                  istringstream stream(Glib::ustring(found.end(),
                                                     dataString.end()));
                  stream >> setName >> scale >> std::hex >> hexColor
                         >> style;
                  if (style == "bar" || style == "dot")
                    {
                      std::tr1::shared_ptr<GraphData<double> >
                        dataSet(new GraphData<double>);
                      if (style == "dot")
                        dataSet->style = GraphDataBase::DOT;
                      dataSet->color[0] = (hexColor >> 16) / 255.0;
                      dataSet->color[1] = ((hexColor >> 8) & 0xff) / 255.0;
                      dataSet->color[2] = (hexColor & 0xff) / 255.0;
                      dataSet->scale = scale;
                      _dataSets.insert(std::make_pair(setName, dataSet));
                      _widget->addGraphData(dataSet);
                    }
                  else if (style == "discreet")
                    {
                      std::tr1::shared_ptr<GraphData<string> >
                        dataSet(new GraphData<string>);
                      dataSet->style = GraphDataBase::EVENT;
                      dataSet->color[0] = (hexColor >> 16) / 255.0;
                      dataSet->color[1] = ((hexColor >> 8) & 0xff) / 255.0;
                      dataSet->color[2] = (hexColor & 0xff) / 255.0;
                      dataSet->scale = scale;
                      _dataSets.insert(std::make_pair(setName, dataSet));
                      _widget->addGraphData(dataSet);
                    }
                }
              else if ((found = find_first(dataString, "%CSV:")))
                {
                  vector<string> tokens
                    = commaSplit(sub_range<Glib::ustring>(found.end(),
                                                          dataString.end()));
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
                  if (findTaggedValue(dataString, "%Title:", decl)
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
                  else if ((found = find_first(dataString, "%YMax:")))
                    {
                      double ymax;
                      std::istringstream
                        stream(Glib::ustring(found.end(), dataString.end()));
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

  bool StapParser::errIoCallback(Glib::IOCondition ioCondition)
  {
    using namespace std;
    if ((ioCondition & Glib::IO_IN) == 0)
      return true;
    char buf[256];
    ssize_t bytes_read = 0;
    bytes_read = read(_errFd, buf, sizeof(buf) - 1);
    if (bytes_read <= 0)
      {
        _win->hide();
        return true;
      }
    if (write(STDOUT_FILENO, buf, bytes_read) < 0)
      ;
    return true;
  }
}
