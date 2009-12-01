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
  StapLauncher() : _argv(0), _childPid(-1), _deathCallback(0) {}
  StapLauncher(char** argv)
    : _argv(argv), _childPid(-1), _deathCallback(0)
  {
  }
  StapLauncher(const string& stapArgs, const string& script,
               const string& scriptArgs)
    : _childPid(-1), _deathCallback(0), _win(0), _widget(0)
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
  void setDeathCallback(ChildDeathReader::Callback* callback)
  {
    _deathCallback = callback;
  }
  void setWinParams(Gtk::Window* win, GraphWidget* widget)
  {
    _win = win;
    _widget = widget;
  }
  int launch();
  void cleanUp();
  tr1::shared_ptr<StapParser> makeStapParser()
  {
    tr1::shared_ptr<StapParser> result(new StapParser(_win, _widget));
    _parsers.push_back(ParserInstance(-1, result));
    return result;
  }
  pid_t reap()
  {
    pid_t pid = ChildDeathReader::reap();
    if (pid < 0)
      return pid;
    ParserList::iterator itr
      = find_if(_parsers.begin(), _parsers.end(),
                boost::bind(&ParserInstance::childPid, _1) == pid);
    if (itr != _parsers.end())
      itr->childPid = -1;
    return pid;
  }
  void killAll()
  {
    for (ParserList::iterator itr = _parsers.begin(), end = _parsers.end();
         itr != end;
         ++itr)
      {
        if (itr->childPid >= 0)
          kill(itr->childPid, SIGTERM);
      }
  }
protected:
  char** _argv;
  string _stapArgs;
  string _script;
  string _scriptArgs;
  int _childPid;
  ChildDeathReader::Callback* _deathCallback;
  Gtk::Window* _win;
  GraphWidget* _widget;
  struct ParserInstance
  {
    ParserInstance() : childPid(-1) {}
    ParserInstance(int childPid_, tr1::shared_ptr<StapParser> stapParser_)
      : childPid(childPid_), stapParser(stapParser_)
    {
    }
    pid_t childPid;
    tr1::shared_ptr<StapParser> stapParser;
  };
  typedef vector<ParserInstance> ParserList;
  ParserList _parsers;
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
    }
  struct sigaction action;
  action.sa_sigaction = handleChild;
  sigemptyset(&action.sa_mask);
  action.sa_flags = SA_SIGINFO | SA_NOCLDSTOP;
  sigaction(SIGCLD, &action, 0);
  int pipefd[4];
  if (pipe(&pipefd[0]) < 0)
    {
      std::perror("pipe");
      exit(1);
    }
  if (pipe(&pipefd[2]) < 0)
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
  tr1::shared_ptr<StapParser> sp(new StapParser(_win, _widget));
  _parsers.push_back(ParserInstance(childPid, sp));
  sp->setErrFd(pipefd[2]);
  sp->setInFd(pipefd[0]);
  Glib::signal_io().connect(sigc::mem_fun(sp.get(),
                                          &StapParser::errIoCallback),
                            pipefd[2],
                            Glib::IO_IN);
  Glib::signal_io().connect(sigc::mem_fun(sp.get(),
                                          &StapParser::ioCallback),
                            pipefd[0],
                            Glib::IO_IN | Glib::IO_HUP);
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
  for (ParserList::iterator itr = _parsers.begin(), end = _parsers.end();
       itr != end;
       ++itr)
    {
      if (itr->childPid > 0)
        kill(itr->childPid, SIGTERM);
      int status;
      pid_t killedPid = -1;
      if ((killedPid = wait(&status)) == -1)
        {
          std::perror("wait");
        }
      else if (killedPid != itr->childPid)
        {
          std::cerr << "wait: killed Pid " << killedPid << " != child Pid "
                    << itr->childPid << "\n";
        }
      else if (_deathCallback)
        _deathCallback->childDied(itr->childPid);
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

class GrapherWindow : public Gtk::Window, public ChildDeathReader::Callback
{
public:
  GrapherWindow();
  virtual ~GrapherWindow() {}
  Gtk::VBox m_Box;
  Gtk::ScrolledWindow scrolled;
  GraphWidget w;
  void childDied(int pid);
  void setGraphicalLauncher(GraphicalStapLauncher* launcher)
  {
    _graphicalLauncher = launcher;
  }
  GraphicalStapLauncher* getGraphicalLauncher() { return _graphicalLauncher; }
protected:
  virtual void on_menu_file_quit();
  virtual void on_menu_script_start();
  void addGraph();
  // menu support
  Glib::RefPtr<Gtk::UIManager> m_refUIManager;
  Glib::RefPtr<Gtk::ActionGroup> m_refActionGroup;
  GraphicalStapLauncher* _graphicalLauncher;

};

GrapherWindow::GrapherWindow()
{
  set_title("systemtap grapher");
  add(m_Box);


  //Create actions for menus and toolbars:
  m_refActionGroup = Gtk::ActionGroup::create();
  //File menu:
  m_refActionGroup->add(Gtk::Action::create("FileMenu", "File"));
  m_refActionGroup->add(Gtk::Action::create("StartScript", "Start script"),
                        sigc::mem_fun(*this,
                                      &GrapherWindow::on_menu_script_start));
  m_refActionGroup->add(Gtk::Action::create("AddGraph", "Add graph"),
                        sigc::mem_fun(*this, &GrapherWindow::addGraph));
  m_refActionGroup->add(Gtk::Action::create("FileQuit", Gtk::Stock::QUIT),
                        sigc::mem_fun(*this,
                                      &GrapherWindow::on_menu_file_quit));
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

void GrapherWindow::childDied(int pid)
{
  hide();
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
      launcher.setDeathCallback(&win);
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
