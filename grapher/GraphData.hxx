#ifndef SYSTEMTAP_GRAPHDATA_HXX
#define SYSTEMTAP_GRAPHDATA_HXX 1

#include <string>
#include <utility>
#include <vector>
#include <tr1/memory>

#include <boost/circular_buffer.hpp>

namespace systemtap
{
  struct GraphDataBase
  {
    virtual ~GraphDataBase() {}
    enum Style
      { BAR,
        DOT,
        EVENT
      };
    typedef boost::circular_buffer<double> TimeList;
    GraphDataBase(TimeList::capacity_type cap = 50000)
      : scale(1.0), style(BAR), times(cap)
    {
      color[0] = 0.0;  color[1] = 1.0;  color[2] = 0.0;
    }
    // size of grid square at "normal" viewing
    double scale;
    double color[3];
    Style style;
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
