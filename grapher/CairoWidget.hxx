// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SYSTEMTAP_CAIROWIDGET_H
#define SYSTEMTAP_CAIROWIDGET_H 1

#include <cairomm/context.h>
namespace systemtap
{
class CairoWidget
{
public:
  CairoWidget(bool visible = false)
    : _visible(visible)
  {}
  bool isVisible() const { return _visible; }
  void setVisible(bool visible) { _visible = visible; }
  void getOrigin(double &x, double &y) const
  {
    x = _x0;
    y = _y0;
  }
  void setOrigin(double x, double y)
  {
    _x0 = x;
    _y0 = y;
  }
  virtual void draw(Cairo::RefPtr<Cairo::Context> cr) = 0;
  virtual bool containsPoint(double x, double y) { return false; }
protected:
  bool _visible;
  double _x0;
  double _y0;
};

class CairoPlayButton : public CairoWidget
{
public:
  CairoPlayButton(bool visible = false)
    : CairoWidget(visible), _size(50.0), _radius(5)
  {
  }
  virtual void draw(Cairo::RefPtr<Cairo::Context> cr);
  virtual bool containsPoint(double x, double y);
protected:
  double _size;
  double _radius;
};

class CairoTextBox : public CairoWidget
{
public:
  void draw(Cairo::RefPtr<Cairo::Context> cr);
  std::string contents;
};
}
#endif
