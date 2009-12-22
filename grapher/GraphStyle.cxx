// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "GraphStyle.hxx"

#include "GraphData.hxx"
#include "Graph.hxx"

namespace systemtap
{
using namespace std;
using namespace tr1;

typedef pair<GraphDataBase::TimeList::iterator,
             GraphDataBase::TimeList::iterator>  TimeListPair;

GraphStyleBar GraphStyleBar::instance;
  
void GraphStyleBar::draw(std::tr1::shared_ptr<GraphDataBase> graphData,
                         Graph* graph, Cairo::RefPtr<Cairo::Context> cr)
{
  shared_ptr<GraphData<double> > realData
    = dynamic_pointer_cast<GraphData<double> >(graphData);
  if (!realData)
    return;
  int64_t left, right;
  double top, bottom;
  graph->getExtents(left, right, top, bottom);
  double horizScale = graph->getHorizontalScale();
  cr->save();
  double lineWidth = cr->get_line_width();
  cr->translate(graph->_graphWidth, 0.0);
  cr->scale(horizScale, 1.0);
  cr->translate(left - right, 0.0);
  cr->set_line_width(lineWidth / horizScale);
  cr->set_source_rgba(graphData->color[0], graphData->color[1],
                      graphData->color[2], 1.0);
  GraphDataBase::TimeList::iterator lower
    = lower_bound(graphData->times.begin(), graphData->times.end(), left);
  GraphDataBase::TimeList::iterator upper
    = upper_bound(graphData->times.begin(), graphData->times.end(), right);
  for (GraphDataBase::TimeList::iterator ditr = lower, de = upper;
       ditr != de;
       ++ditr)
    {
      size_t dataIndex = ditr - graphData->times.begin();
      cr->move_to((*ditr - left), 0);
      cr->line_to((*ditr - left),
                  realData->data[dataIndex] * graph->_graphHeight
                  / graphData->scale);
      cr->stroke();
    }
  cr->restore();
}

ssize_t GraphStyleBar::dataIndexAtPoint(double x, double y,
                                        shared_ptr<GraphDataBase> graphData,
                                        shared_ptr<Graph> graph)
{
  shared_ptr<GraphData<double> > realData
    = dynamic_pointer_cast<GraphData<double> >(graphData);
  if (!realData || graphData->times.empty())
    return -1;
  int64_t left, right;
  double top, bottom;
  graph->getExtents(left, right, top, bottom);
  int64_t t = graph->getTimeAtPoint(x);
  TimeListPair range
    = equal_range(graphData->times.begin(), graphData->times.end(), t);
  if (range.first == graphData->times.end())
    return -1;
  size_t dataIndex = distance(graphData->times.begin(), range.first);
  double val = realData->data[dataIndex];
  double ycoord = val * graph->_graphHeight / graphData->scale;
  if (y >= graph->_yOffset + graph->_graphHeight - ycoord)
    return static_cast<ssize_t>(dataIndex);
  else
    return -1;
}
  
GraphStyleDot GraphStyleDot::instance;
  
void GraphStyleDot::draw(std::tr1::shared_ptr<GraphDataBase> graphData,
                         Graph* graph, Cairo::RefPtr<Cairo::Context> cr)
{
  shared_ptr<GraphData<double> > realData
    = dynamic_pointer_cast<GraphData<double> >(graphData);
  if (!realData)
    return;
  int64_t left, right;
  double top, bottom;
  graph->getExtents(left, right, top, bottom);
  double horizScale = graph->getHorizontalScale();;
  GraphDataBase::TimeList::iterator lower
    = lower_bound(graphData->times.begin(), graphData->times.end(), left);
  GraphDataBase::TimeList::iterator upper
    = upper_bound(graphData->times.begin(), graphData->times.end(), right);
  cr->translate(graph->_graphWidth, 0.0);
  cr->scale(horizScale, 1.0);
  cr->translate(left - right, 0.0);
  cr->set_source_rgba(graphData->color[0], graphData->color[1],
                      graphData->color[2], 1.0);

  for (GraphDataBase::TimeList::iterator ditr = lower, de = upper;
       ditr != de;
       ++ditr)
    {
      size_t dataIndex = ditr - graphData->times.begin();
      // XXX Fix unequal scale in x, y
      cr->arc((*ditr - left),
              (realData->data[dataIndex]
               * graph->_graphHeight / graphData->scale),
              (graph->_lineWidth / (2.0 * horizScale)), 0.0, M_PI * 2.0);
      cr->fill();
    }
  cr->restore();
}

GraphStyleEvent GraphStyleEvent::instance;
  
void GraphStyleEvent::draw(std::tr1::shared_ptr<GraphDataBase> graphData,
                           Graph* graph, Cairo::RefPtr<Cairo::Context> cr)
{
  shared_ptr<GraphData<string> > stringData
    = dynamic_pointer_cast<GraphData<string> >(graphData);
  if (!stringData)
    return;
  int64_t left, right;
  double top, bottom;
  graph->getExtents(left, right, top, bottom);
  double horizScale = graph->getHorizontalScale();
  double eventHeight = graph->_graphHeight * (graphData->scale / 100.0);
  cr->save();
  cr->set_line_width(3 * graph->_lineWidth);
  cr->set_source_rgba(graphData->color[0], graphData->color[1],
                      graphData->color[2], .33);
  cr->move_to(0, eventHeight);
  cr->line_to(graph->_graphWidth, eventHeight);
  cr->stroke();
  cr->restore();
  cr->save();
  // Global translation for user scaling
  cr->translate(graph->_graphWidth, 0.0);
  cr->scale(horizScale, 1.0);
  cr->translate(left - right, 0.0);

  GraphDataBase::TimeList::iterator lower
    = lower_bound(graphData->times.begin(), graphData->times.end(), left);
  GraphDataBase::TimeList::iterator upper
    = upper_bound(graphData->times.begin(), graphData->times.end(), right);
  for (GraphDataBase::TimeList::iterator ditr = lower, de = upper;
       ditr != de;
       ++ditr)
    {
      // size_t dataIndex = ditr - graphData->times.begin();
      double eventHeight = graph->_graphHeight * (graphData->scale / 100.0);
      cr->save();
      cr->set_source_rgba(graphData->color[0], graphData->color[1],
                          graphData->color[2], 1.0);
      // Do cairo transformations here instead of our own math?
      cr->rectangle((*ditr - left) - (1.5 * graph->_lineWidth / horizScale),
                    eventHeight - 1.5 * graph->_lineWidth,
                    3.0 * (graph->_lineWidth / horizScale),
                    3.0 * graph->_lineWidth);
      cr->fill();
      cr->restore();
    }
  cr->restore();
}

ssize_t GraphStyleEvent::dataIndexAtPoint(double x, double y,
                                          shared_ptr<GraphDataBase> graphData,
                                          shared_ptr<Graph> graph)
{
  shared_ptr<GraphData<string> > stringData
    = dynamic_pointer_cast<GraphData<string> >(graphData);
  if (!stringData || graphData->times.empty())
    return -1;
  int64_t left, right;
  double top, bottom;
  graph->getExtents(left, right, top, bottom);
  double horizScale = graph->getHorizontalScale();
  double eventHeight = graph->_graphHeight * (graphData->scale / 100.0);
  GraphDataBase::TimeList::iterator lower
    = lower_bound(graphData->times.begin(), graphData->times.end(), left);
  GraphDataBase::TimeList::iterator upper
    = upper_bound(graphData->times.begin(), graphData->times.end(), right);
  // easier to transform x,y into graph coordinates
  double xgraph, ygraph;
  graph->window2GraphCoords(x, y, xgraph, ygraph);
  double yrect = eventHeight - 1.5 * graph->_lineWidth;
  double rectWidth = 3 * graph->_lineWidth;
  for (GraphDataBase::TimeList::iterator ditr = lower, de = upper;
       ditr != de;
       ++ditr)
    {
      double xrect = ((*ditr - right) * horizScale + graph->_graphWidth
                      - .5 * rectWidth);
      if (xrect <= xgraph && xgraph < xrect + rectWidth
          && yrect <= ygraph && ygraph < yrect + 3.0 * graph->_lineWidth)
        return static_cast<ssize_t>(distance(graphData->times.begin(), ditr));
    }
  return -1;
}
}
