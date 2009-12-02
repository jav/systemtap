#include "Graph.hxx"

#include <sstream>
#include <iostream>
#include <iomanip>

namespace systemtap
{
  using namespace std;
  using namespace std::tr1;
  
  Graph::Graph(double x, double y)
    : _width(600), _height(200), _graphX(0), _graphY(0),
      _graphWidth(580), _graphHeight(180),
      _lineWidth(2), _autoScaling(true), _autoScrolling(true),
      _zoomFactor(1.0), _xOffset(20.0), _yOffset(0.0),
      _playButton(new CairoPlayButton),
      _left(0), _right(1), _top(5.0), _bottom(0.0)
  {
    setOrigin(x, y);
    _graphX = x;
    _graphY = y;
  }
  
  
  void Graph::draw(Cairo::RefPtr<Cairo::Context> cr)
  {
    
    if (_autoScaling)
      {
        // line separation
        int linesPossible = (int)(_graphWidth / (_lineWidth + 2.0));
        // Find latest time.
        int64_t latestTime = 0;
        for (DatasetList::iterator ditr = _datasets.begin(),
               de = _datasets.end();
             ditr != de;
             ++ditr)
          {
            if (!(*ditr)->times.empty())
              {
                int64_t lastDataTime = (*ditr)->times.back();
                if (lastDataTime > latestTime)
                  latestTime = lastDataTime;
              }
          }
        int64_t minDiff = 0;
        int64_t maxTotal = 0;
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
                int64_t timeDiff = *ritr - *(ritr + 1);
                if (timeDiff < minDiff || (timeDiff != 0 && minDiff == 0))
                  minDiff = timeDiff;
                if (minDiff != 0
                    && ((totalDiff + timeDiff) / minDiff + 1) > linesPossible)
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
          _left = _right - 1;
      }
    cr->save();
    double horizScale
        = _zoomFactor * _graphWidth / static_cast<double>(_right - _left);
    cr->translate(_xOffset, _yOffset);
    cr->set_line_width(_lineWidth);

    for (DatasetList::iterator itr = _datasets.begin(), e = _datasets.end();
         itr != e;
         ++itr)
      {
        shared_ptr<GraphDataBase> graphData = *itr;
        cr->save();
        cr->translate(0.0, _graphHeight);
        cr->scale(1.0, -1.0);
        graphData->style->draw(graphData, this, cr);
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
    double diff = static_cast<double>(_right - _left);
    int64_t majorUnit
        = static_cast<int64_t>(pow(10.0, floor(log(diff) / log(10.0))));
    int64_t startTime = (_left / majorUnit) * majorUnit;
    cr->save();
    cr->set_source_rgba(1.0, 1.0, 1.0, .9);
    cr->set_line_cap(Cairo::LINE_CAP_BUTT);
    cr->set_line_width(_lineWidth);
    cr->select_font_face("Sans", Cairo::FONT_SLANT_NORMAL,
                         Cairo::FONT_WEIGHT_NORMAL);
    cr->set_font_size(10.0);
    cr->move_to(_xOffset, _yOffset);
    cr->line_to(_xOffset, _height);
    cr->move_to(_xOffset, _graphHeight);
    cr->line_to(_graphWidth, _graphHeight);
    cr->stroke();
    std::valarray<double> dash(1);
    dash[0] = _graphHeight / 10;
    cr->set_dash(dash, 0);
    double prevTextAdvance = 0;
    for (int64_t tickVal = startTime; tickVal <= _right; tickVal += majorUnit)
      {
        double x = (tickVal - _left) * horizScale + _xOffset;
        cr->move_to(x, _yOffset);
        cr->line_to(x, _graphHeight);
        std::ostringstream stream;
        stream << tickVal;
        Cairo::TextExtents extents;
        cr->get_text_extents(stream.str(), extents);
        // Room for this label?
        if (x + extents.x_bearing > prevTextAdvance)
          {
            cr->move_to(x, _graphHeight + 5 + extents.height);
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

  void Graph::addGraphData(shared_ptr<GraphDataBase> data)
  {
    _datasets.push_back(data);
  }

  void Graph::getExtents(int64_t& left, int64_t& right, double& top,
                         double& bottom) const
  {
    left = _left;
    right = _right;
    top = _top;
    bottom = _bottom;
  }

  void Graph::setExtents(int64_t left, int64_t right, double top, double bottom)
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

  int64_t Graph::getTimeAtPoint(double x)
  {
    return (_left
            + (_right - _left) * ((x - _xOffset)/(_zoomFactor * _graphWidth)));
  }
}
