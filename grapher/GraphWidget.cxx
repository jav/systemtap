// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include <algorithm>
#include <ctime>
#include <iterator>
#include <math.h>
#include <iostream>

#define __STDC_LIMIT_MACROS
#include <stdint.h>

#include <glibmm/timer.h>
#include <cairomm/context.h>
#include <libglademm.h>

#include "../config.h"

#include "GraphWidget.hxx"
#include "CairoWidget.hxx"
#include "Time.hxx"

namespace systemtap
{
using namespace std;
using namespace std::tr1;


    
GraphWidget::GraphWidget()
  : _trackingDrag(false), _width(600), _height(200), _mouseX(0.0),
    _mouseY(0.0), _globalTimeBase(Time::getAbs() / 1000)
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
      _refXmlDataDialog->get_widget("closebutton1", button);
      button->signal_clicked()
        .connect(sigc::mem_fun(*this, &GraphWidget::onDataDialogCancel),
                 false);
      _refXmlDataDialog->get_widget("treeview1", _dataTreeView);
      _dataDialog->signal_show()
        .connect(sigc::mem_fun(*this, &GraphWidget::onDataDialogOpen));
      _dataDialog->signal_hide()
        .connect(sigc::mem_fun(*this, &GraphWidget::onDataDialogClose));
      _listStore = Gtk::ListStore::create(_dataColumns);
      _dataTreeView->set_model(_listStore);
      _dataTreeView->append_column_editable("Enabled",
                                            _dataColumns._dataEnabled);
      _dataTreeView->append_column("Data", _dataColumns._dataName);
      _dataTreeView->append_column("Title", _dataColumns._dataTitle);
      // Disable selection in list
      Glib::RefPtr<Gtk::TreeSelection> listSelection
        = _dataTreeView->get_selection();
      listSelection
        ->set_select_function(sigc::mem_fun(*this,
                                            &GraphWidget::no_select_fun));
      _refXmlDataDialog->get_widget("checkbutton1", _relativeTimesButton);
      _relativeTimesButton->signal_clicked()
        .connect(sigc::mem_fun(*this,
                               &GraphWidget::onRelativeTimesButtonClicked));
      // Set button's initial value from that in .glade file
      _displayRelativeTimes = _relativeTimesButton->get_active();
      graphDataSignal()
        .connect(sigc::mem_fun(*this, &GraphWidget::onGraphDataChanged));
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

void GraphWidget::onGraphDataChanged()
{
  // add any new graph data to the last graph
  GraphDataList newData;
  GraphDataList& allData = getGraphData();
  for (GraphDataList::iterator gditr = allData.begin(), gdend = allData.end();
       gditr != gdend;
       ++gditr)
    {
      bool found = false;
      for (GraphList::iterator gitr = _graphs.begin(), gend = _graphs.end();
           gitr != gend;
           ++gitr)
        {
          GraphDataList& gdata = (*gitr)->getDatasets();
          if (find(gdata.begin(), gdata.end(), *gditr) != gdata.end())
            {
              found = true;
              break;
            }
        }
      if (!found)
        newData.push_back(*gditr);
    }
  copy(newData.begin(), newData.end(),
       back_inserter(_graphs.back()->getDatasets()));
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
  graph->_timeBase = _globalTimeBase;
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
  int64_t currentTime = Time::getAbs();
  Graph::setCurrentTime(currentTime);
  for (GraphList::iterator g = _graphs.begin(); g != _graphs.end(); ++g)
    {
      if (_displayRelativeTimes)
        (*g)->_timeBase = _globalTimeBase;
      else
        (*g)->_timeBase = 0.0;
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
      _activeGraph->_left
        = _dragOrigLeft + increment / _activeGraph->_zoomFactor;
      _activeGraph->_right
        = _dragOrigRight + increment / _activeGraph->_zoomFactor;
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
          double oldZoom = (*gitr)->_zoomFactor;
          if (event->direction == GDK_SCROLL_UP)
            (*gitr)->_zoomFactor *= 1.1;
          else if (event->direction == GDK_SCROLL_DOWN)
            (*gitr)->_zoomFactor /= 1.1;
          int64_t left, right;
          double top, bottom;
          (*gitr)->getExtents(left, right, top, bottom);
          right = left - (left - right) * (oldZoom / (*gitr)->_zoomFactor);
          (*gitr)->setExtents(left, right, top, bottom);
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

void GraphWidget::onDataDialogOpen()
{
  _listStore->clear();
  for (GraphDataList::iterator itr = getGraphData().begin(),
         end = getGraphData().end();
       itr != end;
       ++itr)
    {
      Gtk::TreeModel::iterator litr = _listStore->append();
      Gtk::TreeModel::Row row = *litr;
      row[_dataColumns._dataName] = (*itr)->name;
      if (!(*itr)->title.empty())
        row[_dataColumns._dataTitle] = (*itr)->title;
      row[_dataColumns._graphData] = *itr;
      GraphDataList& gsets = _activeGraph->getDatasets();
      GraphDataList::iterator setItr
        = find(gsets.begin(), gsets.end(), *itr);
      row[_dataColumns._dataEnabled] = (setItr != gsets.end());
    }
  _listConnection =_listStore->signal_row_changed()
    .connect(sigc::mem_fun(*this, &GraphWidget::onRowChanged));

}

void GraphWidget::onDataDialogClose()
{
  if (_listConnection.connected())
    _listConnection.disconnect();
}

bool GraphWidget::onHoverTimeout()
{
  shared_ptr<Graph> g = getGraphUnderPoint(_mouseX, _mouseY);
  if (g && !g->_autoScrolling)
    {
      if (!_hoverText)
        _hoverText = shared_ptr<CairoTextBox>(new CairoTextBox());
      _hoverText->setOrigin(_mouseX + 10, _mouseY - 5);
      GraphDataList& dataSets = g->getDatasets();
      for (GraphDataList::reverse_iterator ritr = dataSets.rbegin(),
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

void GraphWidget::onRelativeTimesButtonClicked()
{
  _displayRelativeTimes = _relativeTimesButton->get_active();
  queue_draw();
}

void GraphWidget::onRowChanged(const Gtk::TreeModel::Path&,
                               const Gtk::TreeModel::iterator& litr)
{
  Gtk::TreeModel::Row row = *litr;
  bool val = row[_dataColumns._dataEnabled];
  shared_ptr<GraphDataBase> data = row[_dataColumns._graphData];
  GraphDataList& graphData = _activeGraph->getDatasets();
  if (val
      && find(graphData.begin(), graphData.end(), data) == graphData.end())
    {
      _activeGraph->addGraphData(data);
    }
  else if (!val)
    {
      graphData.erase(remove(graphData.begin(), graphData.end(), data),
                      graphData.end());
    }
}
}
