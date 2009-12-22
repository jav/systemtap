// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

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
  Graph(double x = 0.0, double y = 0.0);
  virtual void draw(Cairo::RefPtr<Cairo::Context> cr);
  virtual bool containsPoint(double x, double y);
  double getLineWidth() { return _lineWidth; }
  void setLineWidth(double lineWidth) { _lineWidth = lineWidth; }
  bool getAutoScaling() const { return _autoScaling; }
  void setAutoScaling(bool val) { _autoScaling = val; }
  void addGraphData(std::tr1::shared_ptr<GraphDataBase> data);
  void getExtents(int64_t& left, int64_t& right, double& top, double& bottom)
    const;
  void setExtents(int64_t left, int64_t right, double top, double bottom);
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
  double _xOffset;
  double _yOffset;
  std::tr1::shared_ptr<CairoPlayButton> _playButton;
  int64_t _timeBase;
  GraphDataList& getDatasets() { return _datasets; }
  int64_t getTimeAtPoint(double x);
  void window2GraphCoords(double x, double y, double& xgraph, double& ygraph);
  static void setCurrentTime(int64_t time) { _currentTime = time; }

  /*
   * universal horizontal factor
   */
  double getHorizontalScale()
  {
    return _graphWidth / static_cast<double>(_right - _left);
  }
protected:
  GraphDataList _datasets;
  int64_t _left;
  int64_t _right;
  double _top;
  double _bottom;
  static int64_t _currentTime;
};
}
#endif
