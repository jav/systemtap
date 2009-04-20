#ifndef SYSTEMTAP_GRAPHWIDGET_H
#define SYSTEMTAP_GRAPHWIDGET_H

#include <string>
#include <vector>
#include <tr1/memory>

#include <gtkmm/drawingarea.h>
#include "GraphData.hxx"

namespace systemtap
{
  class CairoPlayButton;
  
  class GraphWidget : public Gtk::DrawingArea
  {
  public:
    GraphWidget();
    virtual ~GraphWidget();
    void addGraphData(std::tr1::shared_ptr<GraphData> data);
    void getExtents(double& left, double& right, double& top, double& bottom) const;
    void setExtents(double left, double right, double top, double bottom);
    double getLineWidth() { return _lineWidth; }
    void setLineWidth(double lineWidth) { _lineWidth = lineWidth; }
    bool getAutoScaling() const { return _autoScaling; }
    void setAutoScaling(bool val) { _autoScaling = val; }
    std::string getTitle() const { return _title; }
    void setTitle(const std::string& title) { _title = title; }
    std::string getXAxisText() const { return _xAxisText; }
    void setXAxisText(const std::string& text) { _xAxisText = text; }
    std::string getYAxisText() const { return _yAxisText; }
    void setYAxisText(const std::string& text) { _yAxisText = text; }
  protected:
    //Override default signal handler:
    virtual bool on_expose_event(GdkEventExpose* event);
    virtual bool on_motion_notify_event(GdkEventMotion* event);
    virtual bool on_button_press_event(GdkEventButton* event);
    virtual bool on_button_release_event(GdkEventButton* event);
    virtual bool on_scroll_event(GdkEventScroll* event);
    bool on_timeout();
    typedef std::vector<std::tr1::shared_ptr<GraphData> > DatasetList;
    DatasetList _datasets;
    double _left;
    double _right;
    double _top;
    double _bottom;
    double _lineWidth;
    bool _autoScaling;
    bool _autoScrolling;
    double _zoomFactor;
    bool _trackingDrag;
    double _dragOriginX;
    double _dragOriginY;
    double _dragOrigLeft;
    double _dragOrigRight;
    std::string _title;
    std::string _xAxisText;
    std::string _yAxisText;
    std::tr1::shared_ptr<CairoPlayButton> _playButton;
  };
}
#endif // SYSTEMTAP_GRAPHWIDGET_H
