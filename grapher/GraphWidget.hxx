// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#ifndef SYSTEMTAP_GRAPHWIDGET_H
#define SYSTEMTAP_GRAPHWIDGET_H

#include <string>
#include <vector>
#include <tr1/memory>

#include <gtkmm.h>
#include <libglademm.h>
#include <Graph.hxx>

namespace systemtap
{
class CairoPlayButton;

class DataModelColumns : public Gtk::TreeModelColumnRecord
{
public:
  DataModelColumns()
  {
    add(_dataEnabled);
    add(_dataName);
    add(_dataTitle);
    add(_graphData);
  }
  Gtk::TreeModelColumn<bool> _dataEnabled;
  Gtk::TreeModelColumn<Glib::ustring> _dataName;
  Gtk::TreeModelColumn<Glib::ustring> _dataTitle;
  Gtk::TreeModelColumn<std::tr1::shared_ptr<GraphDataBase> > _graphData;
};
  
class GraphWidget : public Gtk::DrawingArea
{
public:
  GraphWidget();
  virtual ~GraphWidget();
  void addGraph();

protected:
  typedef std::vector<std::tr1::shared_ptr<Graph> > GraphList;
  GraphList _graphs;
  // For click and drag
  std::tr1::shared_ptr<Graph> _activeGraph;
  // Dragging all graphs simultaneously, or perhaps seperately
  typedef std::vector<std::pair<double, double> > DragList;
  DragList dragCoords;
  //Override default signal handler:
  virtual bool on_expose_event(GdkEventExpose* event);
  virtual bool on_motion_notify_event(GdkEventMotion* event);
  virtual bool on_button_press_event(GdkEventButton* event);
  virtual bool on_button_release_event(GdkEventButton* event);
  virtual bool on_scroll_event(GdkEventScroll* event);
  bool on_timeout();
  virtual void on_size_request(Gtk::Requisition* req);
  bool _trackingDrag;
  double _dragOriginX;
  double _dragOriginY;
  double _dragOrigLeft;
  double _dragOrigRight;
  double _width;
  double _height;
  Glib::RefPtr<Gnome::Glade::Xml> _refXmlDataDialog;
  Gtk::Dialog* _dataDialog;
  Gtk::TreeView* _dataTreeView;
  void onDataDialogCancel();
  void onDataDialogOpen();
  void onDataDialogClose();
  bool onHoverTimeout();
  DataModelColumns _dataColumns;
  Glib::RefPtr<Gtk::ListStore> _listStore;
  sigc::connection _hover_timeout_connection;
  std::tr1::shared_ptr<CairoTextBox> _hoverText;
  double _mouseX;
  double _mouseY;
  int64_t _globalTimeBase;
  std::tr1::shared_ptr<Graph> getGraphUnderPoint(double x, double y);
  void establishHoverTimeout();
  Gtk::CheckButton* _relativeTimesButton;
  bool _displayRelativeTimes;
  void onRelativeTimesButtonClicked();
  void onRowChanged(const Gtk::TreeModel::Path&,
                    const Gtk::TreeModel::iterator&);
  sigc::connection _listConnection;
  bool no_select_fun(const Glib::RefPtr<Gtk::TreeModel>& model,
                     const Gtk::TreeModel::Path& path,
                     bool)
  {
    return false;
  }
  void onGraphDataChanged();
};
}
#endif // SYSTEMTAP_GRAPHWIDGET_H
