// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SYSTEMTAP_GRAPHSTYLE_HXX
#define SYSTEMTAP_GRAPHSTYLE_HXX 1
#include <tr1/memory>

#include <cairomm/context.h>

namespace systemtap
{
class GraphDataBase;
class Graph;

class GraphStyle
{
public:
  virtual void draw(std::tr1::shared_ptr<GraphDataBase> graphData,
                    Graph* graph, Cairo::RefPtr<Cairo::Context> cr) = 0;
  virtual ssize_t dataIndexAtPoint(double x, double y,
                                   std::tr1::shared_ptr<GraphDataBase>
                                   graphData,
                                   std::tr1::shared_ptr<Graph> graph)
  {
    return -1;
  }
};

class GraphStyleBar : public GraphStyle
{
public:
  void draw(std::tr1::shared_ptr<GraphDataBase> graphData,
            Graph* graph, Cairo::RefPtr<Cairo::Context> cr);
    
  static GraphStyleBar instance;
  ssize_t dataIndexAtPoint(double x, double y,
                           std::tr1::shared_ptr<GraphDataBase> graphData,
                           std::tr1::shared_ptr<Graph> graph);
};

class GraphStyleDot : public GraphStyle
{
public:
  void draw(std::tr1::shared_ptr<GraphDataBase> graphData,
            Graph* graph, Cairo::RefPtr<Cairo::Context> cr);
  static GraphStyleDot instance;
};

class GraphStyleEvent : public GraphStyle
{
public:
  void draw(std::tr1::shared_ptr<GraphDataBase> graphData,
            Graph* graph, Cairo::RefPtr<Cairo::Context> cr);
  virtual ssize_t dataIndexAtPoint(double x, double y,
                                   std::tr1::shared_ptr<GraphDataBase>
                                   graphData,
                                   std::tr1::shared_ptr<Graph> graph);
  static GraphStyleEvent instance;
};  
}
#endif
