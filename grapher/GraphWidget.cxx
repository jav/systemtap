#include <algorithm>
#include <ctime>
#include <iterator>
#include <math.h>
#include <iostream>

#include <glibmm/timer.h>
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
    : _trackingDrag(false), _width(600), _height(200), _mouseX(0.0),
      _mouseY(0.0)
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
        // XXX
        _refXmlDataDialog->get_widget("okbutton1", button);
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
    _graphs.back()->addGraphData(data);
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
    if (_hoverText && _hoverText->isVisible())
      _hoverText->draw(cr);
    cr->restore();
    return true;
  }

  bool GraphWidget::on_button_press_event(GdkEventButton* event)
  {
    shared_ptr<Graph> g = getGraphUnderPoint(event->x, event->y);
    if (g)
      {
        _activeGraph = g;
        if (event->button == 3)
          {
            _dataDialog->show();
            return true;
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
        establishHoverTimeout();
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
    _mouseX = event->x;
    _mouseY = event->y;
    if (_trackingDrag && _activeGraph)
      {
        Gtk::Allocation allocation = get_allocation();
        const int width = allocation.get_width();
        double motion = (_mouseX - _dragOriginX) / (double) width;
        double increment = motion * (_dragOrigLeft - _dragOrigRight);
        _activeGraph->_left = _dragOrigLeft + increment;
        _activeGraph->_right = _dragOrigRight + increment;
        queue_draw();
      }
    if (_hoverText && _hoverText->isVisible())
      {
        _hoverText->setVisible(false);
        queue_draw();
      }
    establishHoverTimeout();

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
          if (!(*itr)->title.empty())
              row[_dataColumns._dataName] = (*itr)->title;
          else
              row[_dataColumns._dataName] = (*itr)->name;
          row[_dataColumns._graphData] = *itr;
      }
  }

  bool GraphWidget::onHoverTimeout()
  {
    shared_ptr<Graph> g = getGraphUnderPoint(_mouseX, _mouseY);
    if (g && !g->_autoScrolling)
      {
        if (!_hoverText)
          _hoverText = shared_ptr<CairoTextBox>(new CairoTextBox());
        _hoverText->setOrigin(_mouseX + 10, _mouseY - 5);
        Graph::DatasetList& dataSets = g->getDatasets();
        for (Graph::DatasetList::reverse_iterator ritr = dataSets.rbegin(),
                 end = dataSets.rend();
             ritr != end;
             ++ritr)
        {
            ssize_t index
                = (*ritr)->style->dataIndexAtPoint(_mouseX, _mouseY, *ritr, g);
            if (index >= 0)
            {
                _hoverText->contents = (*ritr)->name
                    + ": " + (*ritr)->elementAsString(index);
                _hoverText->setVisible(true);
                queue_draw();
                break;
            }
        }
      }
    return false;
  }

  shared_ptr<Graph> GraphWidget::getGraphUnderPoint(double x, double y)
  {
    for (GraphList::iterator g = _graphs.begin(); g != _graphs.end(); ++g)
      {
        if (x >= (*g)->_graphX
            && x < (*g)->_graphX + (*g)->_graphWidth
            && y >= (*g)->_graphY
            && y < (*g)->_graphY + (*g)->_graphHeight)
          return *g;
      }
    return shared_ptr<Graph>();
  }

  void GraphWidget::establishHoverTimeout()
  {
    if (_hover_timeout_connection.connected())
      _hover_timeout_connection.disconnect();
    _hover_timeout_connection = Glib::signal_timeout()
      .connect(sigc::mem_fun(*this, &GraphWidget::onHoverTimeout), 1000);
  }
}
