#ifndef SYSTEMTAP_CAIROWIDGET_H
#define SYSTEMTAP_CAIROWIDGET_H 1

#include <cairomm/context.h>
namespace systemtap
{
  class CairoWidget
  {
  public:
    CairoWidget(bool visible = false)
      : _visible(visible), _size(50.0), _radius(5)
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
    double _size;
    double _radius;
  };

  class CairoPlayButton : public CairoWidget
  {
  public:
    virtual void draw(Cairo::RefPtr<Cairo::Context> cr);
    virtual bool containsPoint(double x, double y);
  };
}
#endif
