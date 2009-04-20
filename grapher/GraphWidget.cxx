#include <algorithm>
#include <ctime>
#include <math.h>
#include <sstream>
#include <iomanip>
#include <cairomm/context.h>
#include "GraphWidget.hxx"
#include "CairoWidget.hxx"

namespace systemtap
{
  GraphWidget::GraphWidget()
    : _left(0.0), _right(1.0), _top(1.0), _bottom(0.0), _lineWidth(10),
      _autoScaling(true), _autoScrolling(true), _zoomFactor(1.0),
      _trackingDrag(false), _playButton(new CairoPlayButton)
  {
    add_events(Gdk::POINTER_MOTION_MASK | Gdk::BUTTON_PRESS_MASK
               | Gdk::BUTTON_RELEASE_MASK | Gdk::SCROLL_MASK);
    Glib::signal_timeout()
      .connect(sigc::mem_fun(*this, &GraphWidget::on_timeout), 1000);
    signal_expose_event()
      .connect(sigc::mem_fun(*this, &GraphWidget::on_expose_event), false);
    signal_button_press_event()
      .connect(sigc::mem_fun(*this, &GraphWidget::on_button_press_event),
               false);
    signal_button_release_event()
      .connect(sigc::mem_fun(*this, &GraphWidget::on_button_release_event),
               false);
    signal_motion_notify_event()
      .connect(sigc::mem_fun(*this, &GraphWidget::on_motion_notify_event),
               false);
    signal_scroll_event()
      .connect(sigc::mem_fun(*this, &GraphWidget::on_scroll_event), false);
  }

  void GraphWidget::getExtents(double& left, double& right, double& top,
                               double& bottom) const
  {
    left = _left;
    right = _right;
    top = _top;
    bottom = _bottom;
  }

  void GraphWidget::setExtents(double left, double right, double top,
                               double bottom)
  {
    _left = left;
    _right = right;
    _top = top;
    _bottom = bottom;

  }
  GraphWidget::~GraphWidget()
  {
  }

  void GraphWidget::addGraphData(std::tr1::shared_ptr<GraphData> data)
  {
    _datasets.push_back(data);
  }
  
  bool GraphWidget::on_expose_event(GdkEventExpose* event)
  {
    // This is where we draw on the window
    Glib::RefPtr<Gdk::Window> window = get_window();
    if(!window)
      return true;

    Gtk::Allocation allocation = get_allocation();
    
    const int graphWidth = allocation.get_width();
    const int graphHeight = allocation.get_height();
    const int width = graphWidth - 20;
    const int height = graphHeight - 20;

    Cairo::RefPtr<Cairo::Context> cr = window->create_cairo_context();
    if(event && !_autoScaling)
      {
        // clip to the area indicated by the expose event so that we only
        // redraw the portion of the window that needs to be redrawn
        cr->rectangle(event->area.x, event->area.y,
                      event->area.width, event->area.height);
        cr->clip();
      }
    if (_autoScaling)
      {
        // line separation
	int linesPossible = width / ((int)_lineWidth + 2);
        // Find latest time.
        double latestTime = 0;
        for (DatasetList::iterator ditr = _datasets.begin(),
               de = _datasets.end();
             ditr != de;
             ++ditr)
          {
            if (!(*ditr)->data.empty())
              {
                double lastDataTime = (*ditr)->data.back().first;
                if (lastDataTime > latestTime)
                  latestTime = lastDataTime;
              }
          }
        double minDiff = 0.0;
        double maxTotal = 0.0;
        for (DatasetList::iterator ditr = _datasets.begin(),
               de = _datasets.end();
             ditr != de;
             ++ditr)
          {
            GraphData::List& gdata = (*ditr)->data;
            if (gdata.size() <= 1)
              continue;
            double totalDiff = 0.0;
            for (GraphData::List::reverse_iterator ritr = gdata.rbegin(),
                   re = gdata.rend();
                 ritr + 1 != gdata.rend();
                 ritr++)
              {
                double timeDiff = ritr->first - (ritr + 1)->first;
                if (timeDiff < minDiff || (timeDiff != 0 && minDiff == 0))
                  minDiff = timeDiff;
                if (minDiff != 0
                    && (totalDiff + timeDiff) / minDiff > linesPossible)
                  break;
                totalDiff += timeDiff;
              }
            if (totalDiff > maxTotal)
              maxTotal = totalDiff;
          }
        // Now we have a global scale.
        _right = latestTime;
        if (maxTotal != 0)
          _left = latestTime - maxTotal;
        else
          _left = _right - 1.0;
      }
    cr->save();
    double horizScale = _zoomFactor * width / ( _right - _left);
    cr->translate(20.0, 0.0);
    cr->set_line_width(_lineWidth);
    cr->save();
    cr->set_source_rgba(0.0, 0.0, 0.0, 1.0);
    cr->paint();
    cr->restore();

    for (DatasetList::iterator itr = _datasets.begin(), e = _datasets.end();
         itr != e;
         ++itr)
      {
        cr->save();
        cr->translate(0.0, height);
        cr->scale(1.0, -1.0);
        GraphData::List::iterator lower
          = std::lower_bound((*itr)->data.begin(), (*itr)->data.end(), _left,
                             GraphData::Compare());
        GraphData::List::iterator upper
          = std::upper_bound((*itr)->data.begin(), (*itr)->data.end(), _right,
                             GraphData::Compare());
        for (GraphData::List::iterator ditr = lower, de = upper;
             ditr != de;
             ++ditr)
          {
            cr->set_source_rgba((*itr)->color[0], (*itr)->color[1],
                                (*itr)->color[2], 1.0);
            if ((*itr)->style == GraphData::BAR)
              {
                cr->move_to((ditr->first - _left) * horizScale, 0);
                cr->line_to((ditr->first - _left) * horizScale,
                            ditr->second * height / (*itr)->scale);
                cr->stroke();
              }
            else
              {
                cr->arc((ditr->first - _left) * horizScale,
                        ditr->second * height / (*itr)->scale,
                        _lineWidth / 2.0, 0.0, M_PI * 2.0);
                cr->fill();
              }
          }
        cr->restore();
      }
    cr->restore();
    cr->save();
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_BOLD);
    cr->set_font_size(14.0);
    cr->set_source_rgba(1.0, 1.0, 1.0, .8);
    
    if (!_title.empty())
      {
        cr->move_to(20.0, 20.0);
        cr->show_text(_title);
      }
    if (!_xAxisText.empty())
      {
        cr->move_to(10.0, graphHeight - 5);
        cr->show_text(_xAxisText);
      }
    if (!_yAxisText.empty())
      {
        cr->save();
        cr->translate(10.0, height - 10.0);
        cr->rotate(-M_PI / 2.0);
        cr->move_to(10.0, 0.0);
        cr->show_text(_yAxisText);
        cr->restore();
      }
    cr->restore();
    // Draw axes
    double diff = _right - _left;
    double majorUnit = pow(10.0, floor(log(diff) / log(10.0)));
    double startTime = floor(_left / majorUnit) * majorUnit;
    cr->save();
    cr->set_source_rgba(1.0, 1.0, 1.0, .9);
    cr->set_line_cap(Cairo::LINE_CAP_BUTT);
    cr->set_line_width(_lineWidth);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(10.0);
    cr->move_to(20.0, 0.0);
    cr->line_to(20.0, height);
    cr->move_to(20.0, height);
    cr->line_to(graphWidth, height);
    cr->stroke();
    std::valarray<double> dash(1);
    dash[0] = height / 10;
    cr->set_dash(dash, 0.0);
    for (double tickVal = startTime; tickVal < _right; tickVal += majorUnit)
      {
        cr->move_to((tickVal - _left) * horizScale + 20.0, graphHeight - 5);
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(0) << tickVal;
        cr->show_text(stream.str());
        cr->move_to((tickVal - _left) * horizScale + 20.0, 0.0);
        cr->line_to((tickVal - _left) * horizScale + 20.0, height);
        cr->stroke();
      }
    cr->stroke();
    cr->restore();
    
    if (!_autoScrolling)
      {
        _playButton->setVisible(true);
        _playButton->setOrigin(width / 2 - 25, .875 * height - 50);
        _playButton->draw(cr);
      }
    
    return true;
  }

  bool GraphWidget::on_button_press_event(GdkEventButton* event)
  {
    if (!_autoScrolling && _playButton->containsPoint(event->x, event->y))
      {
        _autoScaling = true;
        _autoScrolling = true;
        queue_draw();
      }
    else
      {
        _trackingDrag = true;
        _autoScaling = false;
        _autoScrolling = false;
        _dragOriginX = event->x;
        _dragOriginY = event->y;
        _dragOrigLeft = _left;
        _dragOrigRight = _right;
      }
    return true;
  }

  bool GraphWidget::on_button_release_event(GdkEventButton* event)
  {
    _trackingDrag = false;
    return true;
  }
  
  bool GraphWidget::on_motion_notify_event(GdkEventMotion* event)
  {
    Glib::RefPtr<Gdk::Window> win = get_window();
    if(!win)
      return true;
    double x;
    double y;
    // XXX Hint
    if (event->is_hint)
      {
      }
    else
      {
        x = event->x;
        y = event->y;
      }
    if (_trackingDrag)
      {
        Gtk::Allocation allocation = get_allocation();
        const int width = allocation.get_width();
        double motion = (x - _dragOriginX) / (double) width;
        double increment = motion * (_dragOrigLeft - _dragOrigRight);
        _left = _dragOrigLeft + increment;
        _right = _dragOrigRight + increment;
        queue_draw();
      }
    return true;
  }

  bool GraphWidget::on_scroll_event(GdkEventScroll* event)
  {
    if (event->direction == GDK_SCROLL_UP)
      _zoomFactor += .1;
    else if (event->direction == GDK_SCROLL_DOWN)
      _zoomFactor -= .1;
    queue_draw();
    return true;
  }

  bool GraphWidget::on_timeout()
  {
    queue_draw();
    return true;
  }
}
