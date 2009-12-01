#ifndef SYSTEMTAP_GRAPHDATA_HXX
#define SYSTEMTAP_GRAPHDATA_HXX 1

#include <stdint.h>

#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <tr1/memory>

#include <boost/circular_buffer.hpp>

#include "GraphStyle.hxx"

namespace systemtap
{
  struct GraphDataBase
  {
    virtual ~GraphDataBase() {}

    typedef boost::circular_buffer<int64_t> TimeList;
    GraphDataBase(TimeList::capacity_type cap = 50000)
      : scale(1.0), style(&GraphStyleBar::instance), times(cap)
    {
      color[0] = 0.0;  color[1] = 1.0;  color[2] = 0.0;
    }
    virtual std::string elementAsString(size_t element) = 0;
    // size of grid square at "normal" viewing
    double scale;
    double color[3];
    GraphStyle* style;
    std::string title;
    std::string xAxisText;
    std::string yAxisText;
    TimeList times;
  };

  template<typename T>
  class GraphData : public GraphDataBase
  {
  public:
    typedef T data_type;
    typedef boost::circular_buffer<data_type> DataList;
    GraphData(typename DataList::capacity_type cap = 50000)
      : GraphDataBase(cap), data(cap)
    {
    }
    std::string elementAsString(size_t element)
    {
      std::ostringstream stream;
      stream << data[element];
      return stream.str();
    }
    DataList data;
  };
  struct CSVData
  {
    typedef std::pair<std::string, std::tr1::shared_ptr<GraphDataBase> >
    Element;
    std::vector<Element> elements;
  };
}
#endif
