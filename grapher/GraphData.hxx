#ifndef SYSTEMTAP_GRAPHDATA_HXX
#define SYSTEMTAP_GRAPHDATA_HXX 1

#include <string>
#include <utility>
#include <vector>
#include <tr1/memory>

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
    GraphDataBase() : scale(1.0), style(BAR)
    {
      color[0] = 0.0;  color[1] = 1.0;  color[2] = 0.0;
    }
    typedef std::vector<double> TimeList;
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
    typedef std::vector<data_type> DataList;
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
