// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "GraphData.hxx"

#include <string>
namespace systemtap
{
class StapParser
{
  std::string _buffer;
  typedef std::map<std::string, std::tr1::shared_ptr<GraphDataBase> > DataMap;
  DataMap _dataSets;
  CSVData _csv;
  Gtk::Window* _win;
  int _errFd;
  int _inFd;
  unsigned char _lineEndChar;
public:
  StapParser(Gtk::Window* win)
      : _win(win), _errFd(-1), _inFd(-1), _lineEndChar('\n')
  {
  }
  void parseData(std::tr1::shared_ptr<GraphDataBase> gdata,
                 int64_t time, const std::string& dataString);
  bool ioCallback(Glib::IOCondition ioCondition);
  bool errIoCallback(Glib::IOCondition ioCondition);
  int getErrFd() { return _errFd; }
  void setErrFd(int fd) { _errFd = fd; }
  int getInFd() { return _inFd; }
  void setInFd(int fd) { _inFd = fd; }
};
}
