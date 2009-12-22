// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "Graph.hxx"

#include <sstream>
#include <iostream>
#include <iomanip>

namespace systemtap
{
using namespace std;
using namespace std::tr1;

GraphDataList GraphDataBase::graphData;
sigc::signal<void> GraphDataBase::graphDataChanged;

int64_t Graph::_currentTime = 0;

Graph::Graph(double x, double y)
  : _width(600), _height(200), _graphX(0), _graphY(0),
    _graphWidth(580), _graphHeight(180),
    _lineWidth(2), _autoScaling(true), _autoScrolling(true),
    _zoomFactor(1.0), _xOffset(20.0), _yOffset(0.0),
    _playButton(new CairoPlayButton), _timeBase(0),
    _left(0), _right(1), _top(5.0), _bottom(0.0)
{
  setOrigin(x, y);
  _graphX = x;
  _graphY = y;
}
  
  
void Graph::draw(Cairo::RefPtr<Cairo::Context> cr)
{
    
  if (_autoScaling)
    {
      // Find latest time.
      _right =  _currentTime / 1000;
      // Assume 1 pixel = 5 milliseconds
      _left = _right - static_cast<int64_t>(5000 / _zoomFactor);
    }
  cr->save();
  double horizScale = getHorizontalScale();
  cr->translate(_xOffset, _yOffset);
  cr->set_line_width(_lineWidth);

  for (GraphDataList::iterator itr = _datasets.begin(), e = _datasets.end();
       itr != e;
       ++itr)
    {
      shared_ptr<GraphDataBase> graphData = *itr;
      cr->save();
      cr->translate(0.0, _graphHeight);
      cr->scale(1.0, -1.0);
      graphData->style->draw(graphData, this, cr);
      cr->restore();
      cr->save();
      cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                           Cairo::FONT_WEIGHT_BOLD);
      cr->set_font_size(14.0);
      cr->set_source_rgba(1.0, 1.0, 1.0, .8);
    
      if (!graphData->title.empty())
        {
          cr->move_to(20.0, 20.0);
          cr->show_text(graphData->title);
        }
      if (!graphData->xAxisText.empty())
        {
          cr->move_to(10.0, _graphHeight - 5);
          cr->show_text(graphData->xAxisText);
        }
      if (!graphData->yAxisText.empty())
        {
          cr->save();
          cr->translate(10.0, _height - 10.0);
          cr->rotate(-M_PI / 2.0);
          cr->move_to(10.0, 0.0);
          cr->show_text(graphData->yAxisText);
          cr->restore();
        }
      cr->restore();
        
    }
  cr->restore();
  // Draw axes
  double diff = static_cast<double>(_right - _left);
  int64_t majorUnit
    = static_cast<int64_t>(pow(10.0, floor(log(diff) / log(10.0))));
  int64_t startTime = ((_left - _timeBase) / majorUnit) * majorUnit + _timeBase;
  cr->save();
  cr->set_source_rgba(1.0, 1.0, 1.0, .9);
  cr->set_line_cap(Cairo::LINE_CAP_BUTT);
  cr->set_line_width(_lineWidth);
  cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                       Cairo::FONT_WEIGHT_NORMAL);
  cr->set_font_size(10.0);
  cr->translate(_xOffset, 0.0);
  cr->move_to(0.0, _yOffset);
  cr->line_to(0.0, _height);
  cr->move_to(0.0, _graphHeight);
  cr->line_to(_graphWidth - _xOffset, _graphHeight);
  cr->stroke();
  std::valarray<double> dash(1);
  dash[0] = _graphHeight / 10;
  cr->set_dash(dash, 0);
  double prevTextAdvance = 0;
  for (int64_t tickVal = startTime; tickVal <= _right; tickVal += majorUnit)
    {
      double x = (tickVal - _right) * horizScale + _graphWidth;
      cr->move_to(x, _yOffset);
      cr->line_to(x, _graphHeight);
      std::ostringstream stream;
      stream << (tickVal - _timeBase);
      Cairo::TextExtents extents;
      cr->get_text_extents(stream.str(), extents);
      // Room for this label?
      if (x + extents.x_bearing > prevTextAdvance)
        {
          cr->move_to(x, _graphHeight + 5 + extents.height);
          cr->show_text(stream.str());
          prevTextAdvance = x + extents.x_advance;
        }
    }
  cr->stroke();
  cr->restore();
    
  if (!_autoScrolling)
    {
      _playButton->setVisible(true);
      _playButton->setOrigin(_graphWidth / 2 - 25, .875 * _graphHeight - 50);
      _playButton->draw(cr);
    }
    
}

void Graph::addGraphData(shared_ptr<GraphDataBase> data)
{
  _datasets.push_back(data);
}

void Graph::getExtents(int64_t& left, int64_t& right, double& top,
                       double& bottom) const
{
  left = _left;
  right = _right;
  top = _top;
  bottom = _bottom;
}

void Graph::setExtents(int64_t left, int64_t right, double top, double bottom)
{
  _left = left;
  _right = right;
  _top = top;
  _bottom = bottom;
}

bool Graph::containsPoint(double x, double y)
{
  return x >= _x0 && x < _x0 + _width && y >= _y0 && y < _y0 + _height;
}

int64_t Graph::getTimeAtPoint(double x)
{
  return (x - _graphWidth) / getHorizontalScale() + _right;
}

void Graph::window2GraphCoords(double x, double y,
                               double& xgraph, double& ygraph)
{
  xgraph = x -_xOffset;
  ygraph = -(y - _graphY) + _yOffset + _graphHeight;
}
}
