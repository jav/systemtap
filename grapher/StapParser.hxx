// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "GraphData.hxx"

#include <string>
#include <tr1/memory>

#include <unistd.h>

namespace systemtap
{
// arguments and script for a stap process
struct StapProcess
{
  StapProcess(pid_t pid_ = -1) : argv(0), pid(pid_) {}
  std::string stapArgs;
  std::string script;
  std::string scriptArgs;
  // arguments passed from a single array, like from the command line.
  char **argv;
  // -1 if the grapher is reading from stdin
  pid_t pid;
};
  
class StapParser
{
  std::string _buffer;
  typedef std::map<std::string, std::tr1::shared_ptr<GraphDataBase> > DataMap;
  DataMap _dataSets;
  CSVData _csv;
  int _errFd;
  int _inFd;
  unsigned char _lineEndChar;
  bool _catchHUP;
  std::tr1::shared_ptr<StapProcess> _process;
  sigc::connection _ioConnection;
  sigc::connection _errIoConnection;
public:
  StapParser()
    :  _errFd(-1), _inFd(-1), _lineEndChar('\n'), _catchHUP(false)
  {
  }
  void parseData(std::tr1::shared_ptr<GraphDataBase> gdata,
                 int64_t time, const std::string& dataString);
  bool ioCallback(Glib::IOCondition ioCondition);
  bool errIoCallback(Glib::IOCondition ioCondition);
  int getErrFd() const { return _errFd; }
  void setErrFd(int fd) { _errFd = fd; }
  int getInFd() const { return _inFd; }
  void setInFd(int fd) { _inFd = fd; }
  pid_t getPid() const
  {
    if (_process)
      return _process->pid;
    else
      return -1;
  }
  std::tr1::shared_ptr<StapProcess> getProcess() { return _process; }
  void setProcess(std::tr1::shared_ptr<StapProcess> process)
  {
    _process = process;
  }
  void initIo(int inFd, int errFd, bool catchHUP);
  void disconnect();
};

sigc::signal<void, pid_t>& childDiedSignal();

typedef std::vector<std::tr1::shared_ptr<StapParser> > ParserList;
extern ParserList parsers;

sigc::signal<void>& parserListChangedSignal();
}
