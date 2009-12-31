// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "CairoWidget.hxx"

#include <math.h>
#include <vector>

#include <boost/algorithm/string.hpp>

namespace systemtap
{
using namespace std;
using namespace boost;
  
void CairoPlayButton::draw(Cairo::RefPtr<Cairo::Context> cr)
{
  if (!_visible)
    return;
  cr->save();
  cr->set_line_width(1.0);
  // square with rounded corners
  cr->move_to(_x0, _y0 + _radius);
  cr->arc(_x0 + _radius, _y0 + _radius, _radius, M_PI, -M_PI_2);
  cr->line_to(_x0 + _size - _radius, _y0);
  cr->arc(_x0 + _size - _radius, _y0 + _radius, _radius, -M_PI_2, 0.0);
  cr->line_to(_x0 + _size, _y0 + _size - _radius);
  cr->arc(_x0 + _size - _radius, _y0 + _size - _radius, _radius, 0.0, M_PI_2);
  cr->line_to(_x0 + _radius, _y0 + _size);
  cr->arc(_x0 + _radius, _y0 + _size - _radius, _radius, M_PI_2, M_PI);
  cr->close_path();
  //cr->rectangle(_x0, _y0, 50.0, 50.0);
  cr->set_source_rgba(1.0, 1.0, 1.0, .8);
  cr->stroke();
  // play equalateral triangle
  cr->move_to(_x0 + .25 * _size, _y0 + (.5 - 1.0 / (sqrt(3.0) * 2.0)) * _size);
  cr->line_to(_x0 + .75 * _size, _y0 + .5 * _size);
  cr->line_to(_x0 + .25 * _size, _y0 + (.5 + 1.0 / (sqrt(3.0) * 2.0)) * _size);
  cr->close_path();
  cr->fill();
  cr->restore();
}

bool CairoPlayButton::containsPoint(double x, double y)
{
  if (x >= _x0 && (x < (_x0 + 50.0)) && (y >= _y0) && (y < (_y0 + 50)))
    return true;
  else
    return false;
}

void CairoTextBox::draw(Cairo::RefPtr<Cairo::Context> cr)
{
  if (!_visible)
    return;
  cr->save();
  cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                       Cairo::FONT_WEIGHT_BOLD);
  cr->set_font_size(10.0);
  Cairo::FontExtents fontExtent;
  cr->get_font_extents(fontExtent);
  // Some naughty fonts have a height less than ascent + descent
  double fontHeight = max(fontExtent.ascent + fontExtent.descent + 1.0,
                          fontExtent.height);
  vector<string> lines;
  split(lines, contents, is_from_range('\n', '\n'));
  vector<Cairo::TextExtents> extents;
  double width = 0.0, height = 0.0;
  for (vector<string>::iterator itr = lines.begin(), end = lines.end();
       itr != end;
       ++itr)
    {
      Cairo::TextExtents extent;
      cr->get_text_extents(*itr, extent);
      extents.push_back(extent);
      width = max(width, extent.width);
      height += fontHeight;
    }
  cr->move_to(_x0 - 2, _y0 - 2);
  cr->line_to(_x0 + width + 2, _y0 - 2);
  cr->line_to(_x0 + width + 2, _y0 + height + 2);
  cr->line_to(_x0 - 2, _y0 + height + 2);
  cr->close_path();
  cr->set_source_rgba(1.0, 1.0, 1.0, .8);
  cr->fill();
  cr->set_source_rgba(0.0, 0.0, 0.0, 1.0);
  double texty = _y0;
  for (vector<string>::iterator itr = lines.begin(), end = lines.end();
       itr != end;
       ++itr)
    {
      cr->move_to(_x0, texty + fontExtent.ascent);
      cr->show_text(*itr);
      texty += fontHeight;
    }
  cr->restore();
}
}
