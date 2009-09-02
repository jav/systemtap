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
      add(_dataName);
      add(_graphData);
    }
    Gtk::TreeModelColumn<Glib::ustring> _dataName;
    Gtk::TreeModelColumn<std::tr1::shared_ptr<GraphDataBase> > _graphData;
  };
  
  class GraphWidget : public Gtk::DrawingArea
  {
  public:
    GraphWidget();
    virtual ~GraphWidget();
    void addGraphData(std::tr1::shared_ptr<GraphDataBase> data);
    void addGraph();

  protected:
    typedef std::vector<std::tr1::shared_ptr<Graph> > GraphList;
    GraphList _graphs;
    typedef std::vector<std::tr1::shared_ptr<GraphDataBase> > GraphDataList;
    GraphDataList _graphData;
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
    void onDataAdd();
    void onDataRemove();
    void onDataDialogOpen();
    DataModelColumns _dataColumns;
    Glib::RefPtr<Gtk::ListStore> _listStore;
  };
}
#endif // SYSTEMTAP_GRAPHWIDGET_H
