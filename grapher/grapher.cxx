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

struct ChildInfo
 {
   pid_t pid;
   int waitInfo;
};

extern "C"
 {
   void handleChild(int signum, siginfo_t* info, void* context)
   {
     struct ChildInfo childInfo;
     ssize_t err;

     // Loop doing waitpid because the SIGCHLD signal isn't queued;
     // multiple signals might get lost. If we get an error because
     // there are no zombie children (return value <= 0 because of
     // WNOHANG or no children exist at all), assume that an earlier
     // invocation of handleChild already cleaned them up.
     while ((childInfo.pid = waitpid(-1, &childInfo.waitInfo, WNOHANG)))
       {
         if (childInfo.pid < 0 && errno != ECHILD)
           {
             char errbuf[256];
             strerror_r(errno, errbuf, sizeof(errbuf));
             err = write(STDERR_FILENO, errbuf, strlen(errbuf));
             err = write(STDERR_FILENO, "\n", 1);
             (void) err; /* XXX: notused */
             return;
           }
         else if (childInfo.pid > 0)
           {
             err = write(signalPipe[1], &childInfo, sizeof(childInfo));
             (void) err; /* XXX: notused */
           }
         else
           return;
       }
   }
}

// Waits for a gtk I/O signal, indicating that a child has died, then
// performs an action

class ChildDeathReader
 {
 public:
   ChildDeathReader() : sigfd(-1) {}
   ChildDeathReader(int sigfd_) : sigfd(sigfd_) {}
   int getSigfd() { return sigfd; }
   void setSigfd(int sigfd_) { sigfd = sigfd_; }

   bool ioCallback(Glib::IOCondition ioCondition)
   {
     if ((ioCondition & Glib::IO_IN) == 0)
       return true;
     ChildInfo info;
     if (read(sigfd, &info, sizeof(info)) < static_cast<ssize_t>(sizeof(info)))
       cerr << "couldn't read ChildInfo from signal handler\n";
     else
       childDiedSignal().emit(info.pid);
     return true;
   }
private:
  int sigfd;
};

struct PidPred
{
  PidPred(pid_t pid_) : pid(pid_) {}
  bool operator()(const shared_ptr<StapParser>& parser) const
  {
    return parser->getPid() == pid;
  }
  pid_t pid;
};

// Depending on how args are passed, either launch stap directly or
// use the shell to parse arguments
class StapLauncher : public ChildDeathReader
{
public:
  StapLauncher() : _argv(0), _childPid(-1)
  {
    childDiedSignal().connect(sigc::mem_fun(*this, &StapLauncher::onChildDied));
  }
  StapLauncher(char** argv)
    : _argv(argv), _childPid(-1)
  {
    childDiedSignal().connect(sigc::mem_fun(*this, &StapLauncher::onChildDied));
  }
  StapLauncher(const string& stapArgs, const string& script,
               const string& scriptArgs)
    : _childPid(-1)
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
    _argv = 0;
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

  int launch();
  int launchUsingParser(shared_ptr<StapParser> parser);
  shared_ptr<StapParser> makeStapParser()
  {
    shared_ptr<StapParser> result(new StapParser);
    parsers.push_back(result);
    parserListChangedSignal().emit();
    return result;
  }
public:
  void onChildDied(pid_t pid)
  {
    ParserList::iterator itr
      = find_if(parsers.begin(), parsers.end(), PidPred(pid));
    if (itr != parsers.end())
      {
        (*itr)->disconnect();
        tr1::shared_ptr<StapProcess> sp = (*itr)->getProcess();
        if (sp)
          {
            sp->pid = -1;
            parserListChangedSignal().emit();
          }
      }
  }
  void killAll()
  {
    ParserList parsersCopy(parsers.begin(), parsers.end());
    for (ParserList::iterator itr = parsersCopy.begin(),
           end = parsersCopy.end();
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
};

int StapLauncher::launch()
{
  tr1::shared_ptr<StapParser> sp(new StapParser);
  shared_ptr<StapProcess> proc(new StapProcess(-1));
  if (_argv)
    proc->argv = _argv;
  else
    {
      proc->stapArgs = _stapArgs;
      proc->script = _script;
      proc->scriptArgs = _scriptArgs;
    }
  sp->setProcess(proc);
  pid_t childPid = launchUsingParser(sp);
  if (childPid >= 0)
    {
      parsers.push_back(sp);
      parserListChangedSignal().emit();
    }
  return childPid;
}

int StapLauncher::launchUsingParser(shared_ptr<StapParser> sp)
{
  shared_ptr<StapProcess> proc = sp->getProcess();
  if (!proc)
    {
      cerr << "Can't launch parser with null process structure\n";
      return -1;
    }
  
  proc->pid = -1;
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
  if ((proc->pid = fork()) == -1)
    {
      exit(1);
    }
  else if (proc->pid)
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
      if (proc->argv)
        {
          char argv0[] = "stap";
          char** argvEnd = proc->argv;
          for (; *argvEnd; ++argvEnd)
            ;
          char** realArgv = new char*[argvEnd - proc->argv + 2];
          realArgv[0] = argv0;
          std::copy(_argv, argvEnd + 1, &realArgv[1]);
          execvp("stap", realArgv);
        }
      else
        {
          string argString = "stap" +  proc->stapArgs + " " + proc->script + " "
            + proc->scriptArgs;
          execl("/bin/sh", "sh", "-c", argString.c_str(),
                static_cast<char*>(0));
        }
      _exit(1);
    }
  sp->initIo(pipefd[0], pipefd[2], false);
  return proc->pid;
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

GraphicalStapLauncher *graphicalLauncher = 0;

class ProcModelColumns  : public Gtk::TreeModelColumnRecord
{
public:
  ProcModelColumns()
  {
    add(_iconName);
    add(_scriptName);
    add(_parser);
  }
  Gtk::TreeModelColumn<Glib::ustring> _iconName;
  Gtk::TreeModelColumn<Glib::ustring> _scriptName;
  Gtk::TreeModelColumn<shared_ptr<StapParser> > _parser;
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
  Gtk::Button* _killButton;
  Gtk::Button* _restartButton;
  Gtk::Label* _stapArgsLabel;
  Gtk::Label* _scriptArgsLabel;
  Glib::RefPtr<Gtk::ListStore> _listStore;
  Glib::RefPtr<Gtk::TreeSelection> _listSelection;
  void onClose();
  void show();
  void hide();
  void onParserListChanged();
  void onSelectionChanged();
  void onKill();
  void onRestart();
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
  _xml->get_widget("button1", _killButton);
  _killButton->signal_clicked()
    .connect(sigc::mem_fun(*this, &ProcWindow::onKill), false);
  _killButton->set_sensitive(false);
  _xml->get_widget("button2", _restartButton);
  _restartButton->signal_clicked()
    .connect(sigc::mem_fun(*this, &ProcWindow::onRestart), false);
  _restartButton->set_sensitive(false);
  parserListChangedSignal()
    .connect(sigc::mem_fun(*this, &ProcWindow::onParserListChanged));
  _listSelection = _dataTreeView->get_selection();
  _listSelection->signal_changed()
    .connect(sigc::mem_fun(*this, &ProcWindow::onSelectionChanged));
  _xml->get_widget("label7", _stapArgsLabel);
  _xml->get_widget("label8", _scriptArgsLabel);
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
  // If a process is already selected, try to leave it selected after
  // the list is reconstructed.
  shared_ptr<StapParser> selectedParser;
  {
    Gtk::TreeModel::iterator itr = _listSelection->get_selected();
    if (itr)
      {
        Gtk::TreeModel::Row row = *itr;
        selectedParser = row[_modelColumns._parser];
      }
  }
  _listStore->clear();
  for (ParserList::iterator spitr = parsers.begin(), end = parsers.end();
       spitr != end;
       ++spitr)
    {
      shared_ptr<StapParser> parser = *spitr;
      shared_ptr<StapProcess> sp = parser->getProcess();
      Gtk::TreeModel::iterator litr = _listStore->append();
      Gtk::TreeModel::Row row = *litr;
      if (sp)
        {
          row[_modelColumns._iconName] = sp->pid >= 0 ? "gtk-yes" : "gtk-no";
          row[_modelColumns._scriptName] = sp->script;
        }
      else
        {
          row[_modelColumns._iconName] = "gtk-yes";
          row[_modelColumns._scriptName] = "standard input";
        }
      row[_modelColumns._parser] = parser;
    }
  if (selectedParser)
    {
      Gtk::TreeModel::Children children = _listStore->children();
      for (Gtk::TreeModel::Children::const_iterator itr = children.begin(),
             end = children.end();
           itr != end;
           ++itr)
        {
          Gtk::TreeModel::Row row = *itr;
          if (row[_modelColumns._parser] == selectedParser)
            {
              _listSelection->select(row);
              break;
            }
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
  Gtk::TreeModel::iterator itr = _listSelection->get_selected();
  shared_ptr<StapParser> parser;
  shared_ptr<StapProcess> proc;
  if (itr)
    {
      Gtk::TreeModel::Row row = *itr;
      parser = row[_modelColumns._parser];
      proc = parser->getProcess();
    }
  if (proc)
    {
      bool procRunning = proc->pid >= 0;
      _killButton->set_sensitive(procRunning);
      _restartButton->set_sensitive(!procRunning);
      _stapArgsLabel->set_text(proc->stapArgs);
      _scriptArgsLabel->set_text(proc->scriptArgs);
    }
  else
    {
      _killButton->set_sensitive(false);
      _restartButton->set_sensitive(false);
      _stapArgsLabel->set_text("");
      _scriptArgsLabel->set_text("");
    }
}

void ProcWindow::onKill()
{
  Gtk::TreeModel::iterator itr = _listSelection->get_selected();
  if (!itr)
    return;
  Gtk::TreeModel::Row row = *itr;
  shared_ptr<StapParser> parser = row[_modelColumns._parser]; 
  shared_ptr<StapProcess> proc = parser->getProcess();
  if (proc && proc->pid >= 0)
    kill(proc->pid, SIGTERM);
}

void ProcWindow::onRestart()
{
  Gtk::TreeModel::iterator itr = _listSelection->get_selected();
  if (!itr)
    return;
  Gtk::TreeModel::Row row = *itr;
  shared_ptr<StapParser> parser = row[_modelColumns._parser]; 
  shared_ptr<StapProcess> proc = parser->getProcess();
  if (!proc)
    return;
  if (graphicalLauncher->launchUsingParser(parser) > 0)
    parserListChangedSignal().emit();
}

class GrapherWindow : public Gtk::Window
{
public:
  GrapherWindow();
  virtual ~GrapherWindow() {}
  Gtk::VBox m_Box;
  Gtk::ScrolledWindow scrolled;
  GraphWidget w;
protected:
  virtual void on_menu_file_quit();
  virtual void on_menu_script_start();
  virtual void on_menu_proc_window();
  void addGraph();
  void onParserListChanged();
  // menu support
  Glib::RefPtr<Gtk::UIManager> m_refUIManager;
  Glib::RefPtr<Gtk::ActionGroup> m_refActionGroup;
  shared_ptr<ProcWindow> _procWindow;
  bool _quitting;
};


GrapherWindow::GrapherWindow()
  : _procWindow(new ProcWindow), _quitting(false)
{
  set_title("systemtap grapher");
  add(m_Box);

  parserListChangedSignal()
    .connect(sigc::mem_fun(*this, &GrapherWindow::onParserListChanged));
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
  using namespace boost;
  _quitting = true;
  if (find_if(parsers.begin(), parsers.end(), !bind<bool>(PidPred(-1), _1))
      != parsers.end())
    graphicalLauncher->killAll();
  else
    hide();
}

void GrapherWindow::on_menu_script_start()
{
  graphicalLauncher->runDialog();
}


void GrapherWindow::on_menu_proc_window()
{
  _procWindow->show();
}

void GrapherWindow::onParserListChanged()
{
  using namespace boost;
  if (_quitting
      && (find_if(parsers.begin(), parsers.end(), !bind<bool>(PidPred(-1), _1))
          == parsers.end()))
    hide();
}

int main(int argc, char** argv)
{
  Gtk::Main app(argc, argv);
  graphicalLauncher = new GraphicalStapLauncher;
  GrapherWindow win;

  win.set_title("Grapher");
  win.set_default_size(600, 250);

  if (argc == 2 && !std::strcmp(argv[1], "-"))
    {
      tr1::shared_ptr<StapParser> sp = graphicalLauncher->makeStapParser();
      sp->initIo(STDIN_FILENO, -1, true);
    }
  else if (argc > 1)
    {
      graphicalLauncher->setArgv(argv + 1);
      graphicalLauncher->launch();
    }
  Gtk::Main::run(win);
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
