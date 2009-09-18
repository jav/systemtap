#include "Graph.hxx"

#include <sstream>
#include <iostream>
#include <iomanip>

namespace systemtap
{
  using namespace std;
  using namespace std::tr1;
  
  Graph::Graph(double x, double y)
    : _graphX(0), _graphY(0),
      _lineWidth(2), _autoScaling(true), _autoScrolling(true),
      _zoomFactor(1.0), _playButton(new CairoPlayButton),
      _left(0.0), _right(1.0), _top(5.0), _bottom(0.0)
  {
    setOrigin(x, y);
  }
  
  
  void Graph::draw(Cairo::RefPtr<Cairo::Context> cr)
  {
    
    if (_autoScaling)
      {
        // line separation
        int linesPossible = (int)(_graphWidth / (_lineWidth + 2.0));
        // Find latest time.
        double latestTime = 0.0;
        for (DatasetList::iterator ditr = _datasets.begin(),
               de = _datasets.end();
             ditr != de;
             ++ditr)
          {
            if (!(*ditr)->times.empty())
              {
                double lastDataTime = (*ditr)->times.back();
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
            GraphDataBase::TimeList& gtimes = (*ditr)->times;
            if (gtimes.size() <= 1)
              continue;
            double totalDiff = 0.0;
            for (GraphDataBase::TimeList::reverse_iterator ritr = gtimes.rbegin(),
                   re = gtimes.rend();
                 ritr + 1 != gtimes.rend();
                 ritr++)
              {
                double timeDiff = *ritr - *(ritr + 1);
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
    double horizScale = _zoomFactor * _graphWidth / ( _right - _left);
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
        shared_ptr<GraphDataBase> graphData = *itr;
        shared_ptr<GraphData<double> > realData
          = dynamic_pointer_cast<GraphData<double> >(*itr);
        shared_ptr<GraphData<string> > stringData
          = dynamic_pointer_cast<GraphData<string> >(*itr);
        cr->save();
        cr->translate(0.0, _graphHeight);
        cr->scale(1.0, -1.0);
        GraphDataBase::TimeList::iterator lower
          = std::lower_bound(graphData->times.begin(), graphData->times.end(),
                             _left);
        GraphDataBase::TimeList::iterator upper
          = std::upper_bound(graphData->times.begin(), graphData->times.end(),
                             _right);
        // event bar
        if (graphData->style == GraphDataBase::EVENT)
          {
            double eventHeight = _graphHeight * (graphData->scale / 100.0);
            cr->save();
            cr->set_line_width(3 * _lineWidth);
            cr->set_source_rgba(graphData->color[0], graphData->color[1],
                                graphData->color[2], .33);
            cr->move_to(0, eventHeight);
            cr->line_to(_graphWidth, eventHeight);
            cr->stroke();
            cr->restore();
          }
        for (GraphDataBase::TimeList::iterator ditr = lower, de = upper;
             ditr != de;
             ++ditr)
          {
            size_t dataIndex = ditr - graphData->times.begin();
            cr->set_source_rgba(graphData->color[0], graphData->color[1],
                                graphData->color[2], 1.0);
            if (graphData->style == GraphDataBase::BAR && realData)
              {
                cr->move_to((*ditr - _left) * horizScale, 0);
                cr->line_to((*ditr - _left) * horizScale,
                            realData->data[dataIndex] * _graphHeight
                            / graphData->scale);
                cr->stroke();
              }
            else if (graphData->style == GraphDataBase::DOT && realData)
              {
                cr->arc((*ditr - _left) * horizScale,
                        realData->data[dataIndex] * _graphHeight / graphData->scale,
                        _lineWidth / 2.0, 0.0, M_PI * 2.0);
                cr->fill();
              }
            else if (graphData->style == GraphDataBase::EVENT && stringData)
              {
                double eventHeight = _graphHeight * (graphData->scale / 100.0);
                cr->save();
                cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                                     Cairo::FONT_WEIGHT_NORMAL);
                cr->set_font_size(12.0);
                cr->save();
                cr->scale(1.0, -1.0);
                cr->move_to((*ditr - _left) * horizScale,
                            -eventHeight -3.0 * _lineWidth - 2.0);
                cr->show_text(stringData->data[dataIndex]);
                cr->restore();
                cr->rectangle((*ditr - _left) * horizScale - 1.5 * _lineWidth,
                              eventHeight - 1.5 * _lineWidth,
                              3.0 * _lineWidth, 3.0 * _lineWidth);
                cr->fill();
                cr->restore();
              }
          }
        cr->restore();
        cr->save();
        cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                             Cairo::FONT_WEIGHT_BOLD);
        cr->set_font_size(14.0);
        cr->set_source_rgba(1.0, 1.0, 1.0, .8);
    
        if (!graphData->title.empty())
          {
            cr->move_to(20.0, 20.0);
            cr->show_text(graphData->title);
          }
        if (!graphData->xAxisText.empty())
          {
            cr->move_to(10.0, _graphHeight - 5);
            cr->show_text(graphData->xAxisText);
          }
        if (!graphData->yAxisText.empty())
          {
            cr->save();
            cr->translate(10.0, _height - 10.0);
            cr->rotate(-M_PI / 2.0);
            cr->move_to(10.0, 0.0);
            cr->show_text(graphData->yAxisText);
            cr->restore();
          }
        cr->restore();
        
      }
    cr->restore();
    // Draw axes
    double diff = _right - _left;
    double majorUnit = pow(10.0, floor(log(diff) / log(10.0)));
    double startTime = ceil(_left / majorUnit) * majorUnit;
    cr->save();
    cr->set_source_rgba(1.0, 1.0, 1.0, .9);
    cr->set_line_cap(Cairo::LINE_CAP_BUTT);
    cr->set_line_width(_lineWidth);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(10.0);
    cr->move_to(20.0, 0.0);
    cr->line_to(20.0, _height);
    cr->move_to(20.0, _graphHeight);
    cr->line_to(_graphWidth, _graphHeight);
    cr->stroke();
    std::valarray<double> dash(1);
    dash[0] = _graphHeight / 10;
    cr->set_dash(dash, 0);
    double prevTextAdvance = 0;
    for (double tickVal = startTime; tickVal <= _right; tickVal += majorUnit)
      {
        double x = (tickVal - _left) * horizScale + 20.0;
        cr->move_to(x, 0.0);
        cr->line_to(x, _graphHeight);
        cr->move_to(x, _graphHeight - 5);
        std::ostringstream stream;
        stream << std::fixed << std::setprecision(0) << tickVal;
        Cairo::TextExtents extents;
        cr->get_text_extents(stream.str(), extents);
        // Room for this label?
        if (x + extents.x_bearing > prevTextAdvance)
          {
            cr->show_text(stream.str());
            prevTextAdvance = x + extents.x_advance;
          }
      }
    cr->stroke();
    cr->restore();
    
    if (!_autoScrolling)
      {
        _playButton->setVisible(true);
        _playButton->setOrigin(_graphWidth / 2 - 25, .875 * _graphHeight - 50);
        _playButton->draw(cr);
      }
    
  }

  void Graph::addGraphData(std::tr1::shared_ptr<GraphDataBase> data)
  {
    _datasets.push_back(data);
  }

  void Graph::getExtents(double& left, double& right, double& top,
                         double& bottom) const
  {
    left = _left;
    right = _right;
    top = _top;
    bottom = _bottom;
  }

  void Graph::setExtents(double left, double right, double top, double bottom)
  {
    _left = left;
    _right = right;
    _top = top;
    _bottom = bottom;
  }

  bool Graph::containsPoint(double x, double y)
  {
    return x >= _x0 && x < _x0 + _width && y >= _y0 && y < _y0 + _height;
  }
}
