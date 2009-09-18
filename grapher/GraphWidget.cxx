#include <algorithm>
#include <ctime>
#include <math.h>

#include <cairomm/context.h>
#include "GraphWidget.hxx"
#include "CairoWidget.hxx"

namespace systemtap
{
  using namespace std;
  using namespace std::tr1;
  
  GraphWidget::GraphWidget()
    : _trackingDrag(false)
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
    // Temporary testing of multiple graphs
    shared_ptr<Graph> graph(new Graph);
    graph->_graphHeight = 180;
    graph->_graphWidth = 580;
    _graphs.push_back(graph);
  }

  GraphWidget::~GraphWidget()
  {
  }

  void GraphWidget::addGraphData(std::tr1::shared_ptr<GraphDataBase> data)
  {
    _graphs[0]->addGraphData(data);
  }
  
  bool GraphWidget::on_expose_event(GdkEventExpose* event)
  {
    // This is where we draw on the window
    Glib::RefPtr<Gdk::Window> window = get_window();
    if(!window)
      return true;

    Cairo::RefPtr<Cairo::Context> cr = window->create_cairo_context();
#if 0
    if(event && !_autoScaling)
      {
        // clip to the area indicated by the expose event so that we only
        // redraw the portion of the window that needs to be redrawn
        cr->rectangle(event->area.x, event->area.y,
                      event->area.width, event->area.height);
        cr->clip();
      }
#endif
    cr->save();
    cr->set_source_rgba(0.0, 0.0, 0.0, 1.0);
    cr->paint();
    for (GraphList::iterator g = _graphs.begin(); g != _graphs.end(); ++g)
      {
        double x, y;
        (*g)->getOrigin(x, y);
        cr->save();
        cr->translate(x, y);
        (*g)->draw(cr);
        cr->restore();
      }
    return true;
  }

  bool GraphWidget::on_button_press_event(GdkEventButton* event)
  {
    for (GraphList::iterator g = _graphs.begin(); g != _graphs.end(); ++g)
      {
        if (event->x >= (*g)->_graphX
            && event->x < (*g)->_graphX + (*g)->_graphWidth
            && event->y >= (*g)->_graphY
            && event->y < (*g)->_graphY + (*g)->_graphHeight)
          {
            _activeGraph = *g;
            break;
          }
      }
    if (!_activeGraph)
      return true;
    if (!_activeGraph->_autoScrolling
        && _activeGraph->_playButton->containsPoint(event->x, event->y))
      {
        _activeGraph->_autoScaling = true;
        _activeGraph->_autoScrolling = true;
        queue_draw();
      }
    else
      {
        _trackingDrag = true;
        _activeGraph->_autoScaling = false;
        _activeGraph->_autoScrolling = false;
        _dragOriginX = event->x;
        _dragOriginY = event->y;
        _dragOrigLeft = _activeGraph->_left;
        _dragOrigRight = _activeGraph->_right;
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
    double x = 0.0;
    double y = 0.0;
    // XXX Hint
    if (event->is_hint)
      {
      }
    else
      {
        x = event->x;
        y = event->y;
      }
    if (_trackingDrag && _activeGraph)
      {
        Gtk::Allocation allocation = get_allocation();
        const int width = allocation.get_width();
        double motion = (x - _dragOriginX) / (double) width;
        double increment = motion * (_dragOrigLeft - _dragOrigRight);
        _activeGraph->_left = _dragOrigLeft + increment;
        _activeGraph->_right = _dragOrigRight + increment;
        queue_draw();
      }
    return true;
  }

  bool GraphWidget::on_scroll_event(GdkEventScroll* event)
  {
    for (GraphList::iterator gitr = _graphs.begin();
         gitr != _graphs.end();
         ++gitr)
      {
        if ((*gitr)->containsPoint(event->x, event->y))
          {
            if (event->direction == GDK_SCROLL_UP)
              (*gitr)->_zoomFactor += .1;
            else if (event->direction == GDK_SCROLL_DOWN)
              (*gitr)->_zoomFactor -= .1;
            queue_draw();
            break;
          }
      }
    return true;
  }

  bool GraphWidget::on_timeout()
  {
    queue_draw();
    return true;
  }
}
