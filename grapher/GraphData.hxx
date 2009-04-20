#ifndef SYSTEMTAP_GRAPHDATA_HXX
#define SYSTEMTAP_GRAPHDATA_HXX 1

#include <utility>
#include <vector>

namespace systemtap
{
  struct GraphData
  {
  public:
    enum Style
      { BAR,
        DOT
      };
    GraphData() : scale(1.0), style(BAR)
    {
      color[0] = 0.0;  color[1] = 1.0;  color[2] = 0.0;
    }
    typedef std::pair<double, double> Datum;
    typedef std::vector<Datum> List;
    // size of grid square at "normal" viewing
    double scale;
    double color[3];
    Style style;
    List data;
    struct Compare
    {
      bool operator() (const Datum& lhs, const Datum& rhs) const
      {
        return lhs.first < rhs.first;
      }
      bool operator() (double lhs, const Datum& rhs) const
      {
        return lhs < rhs.first;
      }
      bool operator() (const Datum& lhs, double rhs) const
      {
        return lhs.first < rhs;
      }
    };
  };
}
#endif
