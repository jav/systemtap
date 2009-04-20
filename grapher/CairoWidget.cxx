#include "CairoWidget.hxx"

#include <math.h>

namespace systemtap
{
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
}
