#ifndef SYSTEMTAP_GRAPH_HXX
#define SYSTEMTAP_GRAPH_HXX 1

#include <cairomm/context.h>

#include "GraphData.hxx"
#include "CairoWidget.hxx"

namespace systemtap
{
  class Graph : public CairoWidget
  {
  public:
    friend class GraphWidget;
    Graph();
    virtual void draw(Cairo::RefPtr<Cairo::Context> cr);
    virtual bool containsPoint(double x, double y);
    double getLineWidth() { return _lineWidth; }
    void setLineWidth(double lineWidth) { _lineWidth = lineWidth; }
    bool getAutoScaling() const { return _autoScaling; }
    void setAutoScaling(bool val) { _autoScaling = val; }
    void addGraphData(std::tr1::shared_ptr<GraphDataBase> data);
    void getExtents(double& left, double& right, double& top, double& bottom)
        const;
    void setExtents(double left, double right, double top, double bottom);
    // extents of the whole graph area
    double _width;
    double _height;
    // Position, extents of the graph
    double _graphX;
    double _graphY;
    double _graphWidth;
    double _graphHeight;
    double _lineWidth;
    bool _autoScaling;
    bool _autoScrolling;
    double _zoomFactor;
    std::tr1::shared_ptr<CairoPlayButton> _playButton;
  protected:
        typedef std::vector<std::tr1::shared_ptr<GraphDataBase> > DatasetList;
    DatasetList _datasets;
    double _left;
    double _right;
    double _top;
    double _bottom;
  };
}
#endif
