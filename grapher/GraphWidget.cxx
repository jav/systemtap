#include <algorithm>
#include <ctime>
#include <math.h>
#include <iostream>

#include <cairomm/context.h>
#include <libglademm.h>

#include "../config.h"

#include "GraphWidget.hxx"
#include "CairoWidget.hxx"

namespace systemtap
{
  using namespace std;
  using namespace std::tr1;


    
  GraphWidget::GraphWidget()
    : _trackingDrag(false), _width(600), _height(200)
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
    _graphs.push_back(graph);
    try
      {
        _refXmlDataDialog = Gnome::Glade::Xml::create(PKGDATADIR "/graph-dialog.glade");
        _refXmlDataDialog->get_widget("dialog1", _dataDialog);
        Gtk::Button* button = 0;
        _refXmlDataDialog->get_widget("cancelbutton1", button);
        button->signal_clicked()
          .connect(sigc::mem_fun(*this, &GraphWidget::onDataDialogCancel),
                   false);
        _refXmlDataDialog->get_widget("button1", button);
        button->signal_clicked()
          .connect(sigc::mem_fun(*this, &GraphWidget::onDataAdd), false);
        _refXmlDataDialog->get_widget("button2", button);        
        button->signal_clicked()
          .connect(sigc::mem_fun(*this, &GraphWidget::onDataRemove), false);
        _refXmlDataDialog->get_widget("treeview1", _dataTreeView);
        _dataDialog->signal_map()
          .connect(sigc::mem_fun(*this, &GraphWidget::onDataDialogOpen));
        _listStore = Gtk::ListStore::create(_dataColumns);
        _dataTreeView->set_model(_listStore);
        _dataTreeView->append_column("Data", _dataColumns._dataName);
      }
    catch (const Gnome::Glade::XmlError& ex )
      {
        std::cerr << ex.what() << std::endl;
        throw;
      }
  }

  GraphWidget::~GraphWidget()
  {
  }

  void GraphWidget::addGraphData(shared_ptr<GraphDataBase> data)
  {
    _graphs[0]->addGraphData(data);
    _graphData.push_back(data);
  }

  void GraphWidget::addGraph()
  {
    double x = 0.0;
    double y = 0.0;
    if (!_graphs.empty())
      {
        _graphs.back()->getOrigin(x, y);
        y += _graphs.back()->_height + 10;
      }
    shared_ptr<Graph> graph(new Graph(x, y));
    _height = y + graph->_height;
    graph->setOrigin(x, y);
    _graphs.push_back(graph);
    queue_resize();
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
            if (event->button == 3)
              {
                _dataDialog->show();
                return true;
              }
            else
              {
                break;
              }
          }
      }
    if (!_activeGraph)
      return true;
    double activeX, activeY;
    _activeGraph->getOrigin(activeX, activeY);
    if (!_activeGraph->_autoScrolling
        && _activeGraph->_playButton->containsPoint(event->x - activeX,
                                                    event->y - activeY))
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
    // Was data dialog launched?
    if (event->button != 3)
      {
        _activeGraph.reset();
        _trackingDrag = false;
      }
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

  void GraphWidget::on_size_request(Gtk::Requisition* req)
  {
    req->width = _width;
    req->height = _height;
  }

  void GraphWidget::onDataDialogCancel()
  {
    _dataDialog->hide();
  }

  void GraphWidget::onDataAdd()
  {
    Glib::RefPtr<Gtk::TreeSelection> treeSelection =
      _dataTreeView->get_selection();
    Gtk::TreeModel::iterator iter = treeSelection->get_selected();
    if (iter)
      {
        Gtk::TreeModel::Row row = *iter;
        shared_ptr<GraphDataBase> data = row[_dataColumns._graphData];
        _activeGraph->addGraphData(data);
      }
  }

    void GraphWidget::onDataRemove()
  {
  }

  void GraphWidget::onDataDialogOpen()
  {
      _listStore->clear();
      for (GraphDataList::iterator itr = _graphData.begin(),
               end = _graphData.end();
           itr != end;
           ++itr)
      {
          Gtk::TreeModel::iterator litr = _listStore->append();
          Gtk::TreeModel::Row row = *litr;
          row[_dataColumns._dataName] = (*itr)->title;
          row[_dataColumns._graphData] = *itr;
      }
  }
}
