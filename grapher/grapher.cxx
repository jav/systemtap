// systemtap grapher
// Copyright (C) 2009 Red Hat Inc.
//
// This file is part of systemtap, and is free software.  You can
// redistribute it and/or modify it under the terms of the GNU General
// Public License (GPL); either version 2, or (at your option) any
// later version.

#include "GraphWidget.hxx"
#include "StapParser.hxx"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <memory>
#include <vector>

#include <signal.h>

#include <boost/bind.hpp>

#include <gtkmm.h>
#include <gtkmm/button.h>
#include <gtkmm/stock.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <gtkmm/scrolledwindow.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <getopt.h>

using namespace std;
using namespace tr1;

using namespace systemtap;

// magic for noticing that the child stap process has died.
int signalPipe[2] = {-1, -1};

extern "C"
{
  void handleChild(int signum, siginfo_t* info, void* context)
  {
    char buf[1];
    ssize_t err;
    buf[0] = 1;
    err = write(signalPipe[1], buf, 1);
  }
}

// Waits for a gtk I/O signal, indicating that a child has died, then
// performs an action

class ChildDeathReader
{
public:
  struct Callback
  {
    virtual ~Callback() {}
    virtual void childDied(int pid) {}
  };
  ChildDeathReader() : sigfd(-1) {}
  ChildDeathReader(int sigfd_) : sigfd(sigfd_) {}
  int getSigfd() { return sigfd; }
  void setSigfd(int sigfd_) { sigfd = sigfd_; }
  virtual pid_t reap()
  {
    pid_t pid;
    int status;
    if ((pid = waitpid(-1, &status, WNOHANG)) == -1)
      {
        std::perror("waitpid");
        return -1;
      }
    else
      {
        return pid;
      }
  }
  bool ioCallback(Glib::IOCondition ioCondition)
  {
    if ((ioCondition & Glib::IO_IN) == 0)
      return true;
    char buf;

    if (read(sigfd, &buf, 1) <= 0)
      return true;
    reap();
    return true;
  }
private:
  int sigfd;
};

// Depending on how args are passed, either launch stap directly or
// use the shell to parse arguments
class StapLauncher : public ChildDeathReader
{
public:
  StapLauncher() : _argv(0), _childPid(-1) {}
  StapLauncher(char** argv)
    : _argv(argv), _childPid(-1)
  {
  }
  StapLauncher(const string& stapArgs, const string& script,
               const string& scriptArgs)
    : _childPid(-1), _win(0), _widget(0)
  {
    setArgs(stapArgs, script, scriptArgs);
  }
  void setArgv(char** argv)
  {
    _argv = argv;
  }

  char** getArgv()
  {
    return _argv;
  }

  void setArgs(const string& stapArgs, const string& script,
               const string& scriptArgs)
  {
    _stapArgs = stapArgs;
    _script = script;
    _scriptArgs = scriptArgs;
  }

  void getArgs(string& stapArgs, string& script, string& scriptArgs)
  {
    stapArgs = _stapArgs;
    script = _script;
    scriptArgs = _scriptArgs;
  }

  void reset()
  {
    _argv = 0;
    _stapArgs.clear();
    _script.clear();
    _scriptArgs.clear();
  }

  void setWinParams(Gtk::Window* win, GraphWidget* widget)
  {
    _win = win;
    _widget = widget;
  }
  int launch();
  void cleanUp();
  shared_ptr<StapParser> makeStapParser()
  {
    shared_ptr<StapParser> result(new StapParser);
    parsers.push_back(result);
    parserListChangedSignal().emit();
    return result;
  }
private:
  struct pidPred
  {
    pidPred(pid_t pid_) : pid(pid_) {}
    bool operator()(const shared_ptr<StapParser>& parser) const
    {
      return parser->getPid() == pid;
    }
    pid_t pid;
  };
public:
  pid_t reap()
  {
    using namespace boost;
    pid_t pid = ChildDeathReader::reap();
    if (pid < 0)
      return pid;
    ParserList::iterator itr
      = find_if(parsers.begin(), parsers.end(), pidPred(pid));
    if (itr != parsers.end())
      {
        tr1::shared_ptr<StapProcess> sp = (*itr)->getProcess();
        if (sp)
          {
            sp->pid = -1;
            parserListChangedSignal().emit();
          }
      }
    childDiedSignal().emit(pid);
    return pid;
  }
  void killAll()
  {
    for (ParserList::iterator itr = parsers.begin(), end = parsers.end();
         itr != end;
         ++itr)
      {
        if ((*itr)->getPid() >= 0)
          kill((*itr)->getPid(), SIGTERM);
      }
  }
protected:
  char** _argv;
  string _stapArgs;
  string _script;
  string _scriptArgs;
  int _childPid;
  Gtk::Window* _win;
  GraphWidget* _widget;
};

int StapLauncher::launch()
{
  int childPid = -1;
  if (signalPipe[0] < 0)
    {
      if (pipe(&signalPipe[0]) < 0)
        {
          std::perror("pipe");
          exit(1);
        }
      setSigfd(signalPipe[0]);
      if (signalPipe[0] >= 0)
        {
          Glib::signal_io().connect(sigc::mem_fun(*this,
                                                  &ChildDeathReader::ioCallback),
                                    signalPipe[0], Glib::IO_IN);
        }
      struct sigaction action;
      action.sa_sigaction = handleChild;
      sigemptyset(&action.sa_mask);
      action.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;
      sigaction(SIGCLD, &action, 0);
    }
  int pipefd[4];
  if (pipe(&pipefd[0]) < 0 || pipe(&pipefd[2]) < 0)
    {
      std::perror("pipe");
      exit(1);
    }
  if ((childPid = fork()) == -1)
    {
      exit(1);
    }
  else if (childPid)
    {
      close(pipefd[1]);
      close(pipefd[3]);
    }
  else
    {
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[3], STDERR_FILENO);
      for_each(&pipefd[0], &pipefd[4], close);
      for_each(&signalPipe[0], &signalPipe[2], close);
      if (_argv)
        {
          char argv0[] = "stap";
          char** argvEnd = _argv;
          for (; *argvEnd; ++argvEnd)
            ;
          char** realArgv = new char*[argvEnd - _argv + 2];
          realArgv[0] = argv0;
          std::copy(_argv, argvEnd + 1, &realArgv[1]);
          execvp("stap", realArgv);
        }
      else
        {
          string argString = "stap" +  _stapArgs + " " + _script + " "
            + _scriptArgs;
          execl("/bin/sh", "sh", "-c", argString.c_str(),
                static_cast<char*>(0));
        }
      _exit(1);
    }
  tr1::shared_ptr<StapParser> sp(new StapParser);
  shared_ptr<StapProcess> proc(new StapProcess(childPid));
  if (_argv)
    proc->argv = _argv;
  else
    {
      proc->stapArgs = _stapArgs;
      proc->script = _script;
      proc->scriptArgs = _scriptArgs;
    }
  sp->setProcess(proc);
  parsers.push_back(sp);
  parserListChangedSignal().emit();
  sp->initIo(pipefd[0], pipefd[2]);
  return childPid;
}

void StapLauncher::cleanUp()
{
  struct sigaction action;
  action.sa_handler = SIG_DFL;
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGCLD, &action, 0);
  // Drain any outstanding signals
  close(signalPipe[1]);
  char buf;
  while (read(signalPipe[0], &buf, 1) > 0)
    reap();
  for (ParserList::iterator itr = parsers.begin(), end = parsers.end();
       itr != end;
       ++itr)
    {
      pid_t childPid = (*itr)->getPid();
      if (childPid > 0)
        kill(childPid, SIGTERM);
      int status;
      pid_t killedPid = -1;
      if ((killedPid = wait(&status)) == -1)
        {
          std::perror("wait");
        }
      else if (killedPid != childPid)
        {
          std::cerr << "wait: killed Pid " << killedPid << " != child Pid "
                    << childPid << "\n";
        }
      else
        {
          childDiedSignal().emit(childPid);
        }
    }
}

class GraphicalStapLauncher : public StapLauncher
{
public:
  GraphicalStapLauncher();
  bool runDialog();
  void onLaunch();
  void onLaunchCancel();
private:
  Glib::RefPtr<Gnome::Glade::Xml> _launchStapDialog;
  Gtk::Window* _scriptWindow;
  Gtk::FileChooserButton* _chooserButton;
  Gtk::Entry* _stapArgEntry;
  Gtk::Entry* _scriptArgEntry;
};

class ProcModelColumns  : public Gtk::TreeModelColumnRecord
{
public:
  ProcModelColumns()
  {
    add(_iconName);
    add(_scriptName);
    add(_proc);
  }
  Gtk::TreeModelColumn<Glib::ustring> _iconName;
  Gtk::TreeModelColumn<Glib::ustring> _scriptName;
  Gtk::TreeModelColumn<shared_ptr<StapProcess> > _proc;
};

// This should probably be a Gtk window, with the appropriate glade magic
class ProcWindow
{
public:
  ProcWindow();
  ProcModelColumns _modelColumns;
  Glib::RefPtr<Gnome::Glade::Xml> _xml;
  Gtk::Window* _window;
  Gtk::TreeView* _dataTreeView;
  Glib::RefPtr<Gtk::ListStore> _listStore;
  Glib::RefPtr<Gtk::TreeSelection> _listSelection;
  void onClose();
  void show();
  void hide();
  void onParserListChanged();
  void onSelectionChanged();
  void onKill();
private:
  bool _open;
  void refresh();
};

ProcWindow::ProcWindow()
  : _open(false)
{
  try
    {
      _xml = Gnome::Glade::Xml::create(PKGDATADIR "/processwindow.glade");
      _xml->get_widget("window1", _window);
      _xml->get_widget("treeview1", _dataTreeView);
      
    }
  catch (const Gnome::Glade::XmlError& ex )
    {
      std::cerr << ex.what() << std::endl;
      throw;
    }
  _listStore = Gtk::ListStore::create(_modelColumns);
  _dataTreeView->set_model(_listStore);  
  // Display a nice icon for the state of the process
  Gtk::CellRendererPixbuf* cell = Gtk::manage(new Gtk::CellRendererPixbuf);
  _dataTreeView->append_column("State", *cell);
  Gtk::TreeViewColumn* column = _dataTreeView->get_column(0);
  if (column)
    column->add_attribute(cell->property_icon_name(), _modelColumns._iconName);
  _dataTreeView->append_column("Script", _modelColumns._scriptName);
  Gtk::Button* button = 0;
  _xml->get_widget("button5", button);
  button->signal_clicked().connect(sigc::mem_fun(*this, &ProcWindow::onClose),
                                   false);
  _xml->get_widget("button1", button);
  button->signal_clicked().connect(sigc::mem_fun(*this, &ProcWindow::onKill),
                                   false);
  parserListChangedSignal()
    .connect(sigc::mem_fun(*this, &ProcWindow::onParserListChanged));
  _listSelection = _dataTreeView->get_selection();
  _listSelection->signal_changed()
    .connect(sigc::mem_fun(*this, &ProcWindow::onSelectionChanged));
  
}

void ProcWindow::onClose()
{
  _window->hide();
}

void ProcWindow::show()
{
  _open = true;
  refresh();
  _window->show();

}

void ProcWindow::hide()
{
  _open = false;
  _window->hide();
}

void ProcWindow::refresh()
{
  _listStore->clear();
  for (ParserList::iterator spitr = parsers.begin(), end = parsers.end();
       spitr != end;
       ++spitr)
    {
      shared_ptr<StapProcess> sp = (*spitr)->getProcess();
      if (sp)
        {
          Gtk::TreeModel::iterator litr = _listStore->append();
          Gtk::TreeModel::Row row = *litr;
          row[_modelColumns._iconName] = sp->pid >= 0 ? "gtk-yes" : "gtk-no";
          row[_modelColumns._scriptName] = sp->script;
          row[_modelColumns._proc] = sp;
        }
    }
}

void ProcWindow::onParserListChanged()
{
  if (_open)
    {
      refresh();
      _window->queue_draw();
    }
}

void ProcWindow::onSelectionChanged()
{
}

void ProcWindow::onKill()
{
  Gtk::TreeModel::iterator itr = _listSelection->get_selected();
  if (!itr)
    return;
  Gtk::TreeModel::Row row = *itr;
  shared_ptr<StapProcess> proc = row[_modelColumns._proc];
  if (proc->pid >= 0)
    kill(proc->pid, SIGTERM);
}

class GrapherWindow : public Gtk::Window, public ChildDeathReader::Callback
{
public:
  GrapherWindow();
  virtual ~GrapherWindow() {}
  Gtk::VBox m_Box;
  Gtk::ScrolledWindow scrolled;
  GraphWidget w;
  void setGraphicalLauncher(GraphicalStapLauncher* launcher)
  {
    _graphicalLauncher = launcher;
  }
  GraphicalStapLauncher* getGraphicalLauncher() { return _graphicalLauncher; }
protected:
  virtual void on_menu_file_quit();
  virtual void on_menu_script_start();
  virtual void on_menu_proc_window();
  void addGraph();
  // menu support
  Glib::RefPtr<Gtk::UIManager> m_refUIManager;
  Glib::RefPtr<Gtk::ActionGroup> m_refActionGroup;
  GraphicalStapLauncher* _graphicalLauncher;
  shared_ptr<ProcWindow> _procWindow;
};


GrapherWindow::GrapherWindow()
  : _procWindow(new ProcWindow)
{
  set_title("systemtap grapher");
  add(m_Box);

  
  //Create actions for menus and toolbars:
  m_refActionGroup = Gtk::ActionGroup::create();
  //File menu:
  m_refActionGroup->add(Gtk::Action::create("FileMenu", "File"));
  m_refActionGroup->add(Gtk::Action::create("StartScript", "Start script..."),
                        sigc::mem_fun(*this,
                                      &GrapherWindow::on_menu_script_start));
  m_refActionGroup->add(Gtk::Action::create("AddGraph", "Add graph"),
                        sigc::mem_fun(*this, &GrapherWindow::addGraph));
  m_refActionGroup->add(Gtk::Action::create("FileQuit", Gtk::Stock::QUIT),
                        sigc::mem_fun(*this,
                                      &GrapherWindow::on_menu_file_quit));
  // Window menu
  m_refActionGroup->add(Gtk::Action::create("WindowMenu", "Window"));
  m_refActionGroup->add(Gtk::Action::create("ProcessWindow",
                                            "Stap processes..."),
                        sigc::mem_fun(*this,
                                      &GrapherWindow::on_menu_proc_window));
  m_refUIManager = Gtk::UIManager::create();
  m_refUIManager->insert_action_group(m_refActionGroup);

  add_accel_group(m_refUIManager->get_accel_group());
  //Layout the actions in a menubar and toolbar:
  Glib::ustring ui_info =
    "<ui>"
    "  <menubar name='MenuBar'>"
    "    <menu action='FileMenu'>"
    "      <menuitem action='StartScript'/>"
    "      <menuitem action='AddGraph'/>"
    "      <menuitem action='FileQuit'/>"
    "    </menu>"
    "    <menu action='WindowMenu'>"
    "      <menuitem action='ProcessWindow'/>"
    "    </menu>"
    "  </menubar>"
    "</ui>";
  try
    {
      m_refUIManager->add_ui_from_string(ui_info);
    }
  catch(const Glib::Error& ex)
    {
      std::cerr << "building menus failed: " <<  ex.what();
    }
  Gtk::Widget* pMenubar = m_refUIManager->get_widget("/MenuBar");
  scrolled.add(w);
  if(pMenubar)
    m_Box.pack_start(*pMenubar, Gtk::PACK_SHRINK);
  m_Box.pack_start(scrolled, Gtk::PACK_EXPAND_WIDGET);
  scrolled.show();

  show_all_children();

}

void GrapherWindow::on_menu_file_quit()
{
  hide();
}

void GrapherWindow::on_menu_script_start()
{
  _graphicalLauncher->runDialog();
}


void GrapherWindow::on_menu_proc_window()
{
  _procWindow->show();
}

int main(int argc, char** argv)
{
  Gtk::Main app(argc, argv);
  GraphicalStapLauncher launcher;
  GrapherWindow win;

  win.set_title("Grapher");
  win.set_default_size(600, 250);
  launcher.setWinParams(&win, &win.w);

  win.setGraphicalLauncher(&launcher);
  
  if (argc == 2 && !std::strcmp(argv[1], "-"))
    {
      tr1::shared_ptr<StapParser> sp = launcher.makeStapParser();
      sp->setInFd(STDIN_FILENO);
      Glib::signal_io().connect(sigc::mem_fun(sp.get(),
                                              &StapParser::ioCallback),
                                STDIN_FILENO,
                                Glib::IO_IN | Glib::IO_HUP);
    }
  else if (argc > 1)
    {
      launcher.setArgv(argv + 1);
      launcher.launch();
    }
  Gtk::Main::run(win);
  launcher.cleanUp();
  return 0;
}

void GrapherWindow::addGraph()
{
  w.addGraph();
  
}

GraphicalStapLauncher::GraphicalStapLauncher()
{
  try
    {
      _launchStapDialog
        = Gnome::Glade::Xml::create(PKGDATADIR "/stap-start.glade");
      _launchStapDialog->get_widget("window1", _scriptWindow);
      _launchStapDialog->get_widget("scriptChooserButton", _chooserButton);
      _launchStapDialog->get_widget("stapEntry", _stapArgEntry);
      _launchStapDialog->get_widget("scriptEntry", _scriptArgEntry);
      Gtk::Button* button = 0;
      _launchStapDialog->get_widget("launchButton", button);
      button->signal_clicked()
        .connect(sigc::mem_fun(*this, &GraphicalStapLauncher::onLaunch), false);
      _launchStapDialog->get_widget("cancelButton", button);
      button->signal_clicked()
        .connect(sigc::mem_fun(*this, &GraphicalStapLauncher::onLaunchCancel),
                 false);
    }
  catch (const Gnome::Glade::XmlError& ex )
    {
      std::cerr << ex.what() << std::endl;
      throw;
    }
}

bool GraphicalStapLauncher::runDialog()
{
  _scriptWindow->show();
  return true;
}

void GraphicalStapLauncher::onLaunch()
{
  setArgs(_stapArgEntry->get_text(), _chooserButton->get_filename(),
          _scriptArgEntry->get_text());
  _scriptWindow->hide();
  launch();
}

void GraphicalStapLauncher::onLaunchCancel()
{
  _scriptWindow->hide();
}
