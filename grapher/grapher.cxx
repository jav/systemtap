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
#include <vector>

#include <signal.h>

#include <gtkmm.h>
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

// Waits for a gtk I/O signal, indicating that a child has died, then
// performs an action

class ChildDeathReader
{
public:
  struct Callback
  {
    virtual void childDied(int pid) {}
  };
  ChildDeathReader() : sigfd(-1) {}
  ChildDeathReader(int sigfd_) : sigfd(sigfd_) {}
  int getSigfd() { return sigfd; }
  void setSigfd(int sigfd_) { sigfd = sigfd_; }
  bool ioCallback(Glib::IOCondition ioCondition)
  {
    if ((ioCondition & Glib::IO_IN) == 0)
      return true;
    char buf;

    if (read(sigfd, &buf, 1) <= 0)
      return true;
    int status;
    while (wait(&status) != -1)
      ;
    return true;
  }
private:
  int sigfd;
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
protected:
  virtual void on_menu_file_quit();
  void addGraph();
  // menu support
  Glib::RefPtr<Gtk::UIManager> m_refUIManager;
  Glib::RefPtr<Gtk::ActionGroup> m_refActionGroup;

};

GrapherWindow::GrapherWindow()
{
  set_title("systemtap grapher");
  add(m_Box);


  //Create actions for menus and toolbars:
  m_refActionGroup = Gtk::ActionGroup::create();
  //File menu:
  m_refActionGroup->add(Gtk::Action::create("FileMenu", "File"));
  m_refActionGroup->add(Gtk::Action::create("AddGraph", "Add graph"),
                        sigc::mem_fun(*this, &GrapherWindow::addGraph));
  m_refActionGroup->add(Gtk::Action::create("FileQuit", Gtk::Stock::QUIT),
                        sigc::mem_fun(*this, &GrapherWindow::on_menu_file_quit));
  m_refUIManager = Gtk::UIManager::create();
  m_refUIManager->insert_action_group(m_refActionGroup);

  add_accel_group(m_refUIManager->get_accel_group());
  //Layout the actions in a menubar and toolbar:
  Glib::ustring ui_info =
    "<ui>"
    "  <menubar name='MenuBar'>"
    "    <menu action='FileMenu'>"
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

void GrapherWindow::childDied(int pid)
{
  hide();
}

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

// Depending on how args are passed, either launch stap directly or
// use the shell to parse arguments
class StapLauncher : public ChildDeathReader
{
public:
  StapLauncher() : _argv(0), _childPid(-1), _deathCallback(0), _stapParser(0) {}
  StapLauncher(char** argv)
    : _argv(argv), _childPid(-1), _deathCallback(0), _stapParser(0)
  {
  }
  StapLauncher(const string& stapArgs, const string& script,
               const string& scriptArgs)
    : _childPid(-1), _deathCallback(0), _stapParser(0)
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
  void setStapParser(StapParser *parser)
  {
    _stapParser = parser;
  }
  int launch();
  void cleanUp();
protected:
  char** _argv;
  string _stapArgs;
  string _script;
  string _scriptArgs;
  int _childPid;
  ChildDeathReader::Callback* _deathCallback;
  StapParser* _stapParser;
};

int StapLauncher::launch()
{
  int stapErrFd = -1;

  if (pipe(&signalPipe[0]) < 0)
    {
      std::perror("pipe");
      exit(1);
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
  if ((_childPid = fork()) == -1)
    {
      exit(1);
    }
  else if (_childPid)
    {
      dup2(pipefd[0], STDIN_FILENO);
      stapErrFd = pipefd[2];
      close(pipefd[0]);
      close(pipefd[1]);
      close(pipefd[3]);
    }
  else
    {
      dup2(pipefd[1], STDOUT_FILENO);
      dup2(pipefd[3], STDERR_FILENO);
      for (int i = 0; i < 4; ++i)
        close(pipefd[i]);
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
          execl("/bin/sh", "-c", argString.c_str(), static_cast<char*>(0));
        }
      _exit(1);
    }
  if (stapErrFd >= 0)
    {
      _stapParser->setErrFd(stapErrFd);
      Glib::signal_io().connect(sigc::mem_fun(*_stapParser,
                                              &StapParser::errIoCallback),
                                stapErrFd,
                                Glib::IO_IN);
    }
  setSigfd(signalPipe[0]);
  if (signalPipe[0] >= 0)
    {
      Glib::signal_io().connect(sigc::mem_fun(*this,
                                              &ChildDeathReader::ioCallback),
                                signalPipe[0], Glib::IO_IN);
    }
  Glib::signal_io().connect(sigc::mem_fun(*_stapParser,
                                          &StapParser::ioCallback),
                            STDIN_FILENO,
                            Glib::IO_IN | Glib::IO_HUP);
  return _childPid;
}

void StapLauncher::cleanUp()
{
  if (_childPid > 0)
    kill(_childPid, SIGTERM);
  int status;
  while (wait(&status) != -1)
    ;
  if (_deathCallback)
    {
      _deathCallback->childDied(_childPid);
      _childPid = -1;
    }
}

StapLauncher launcher;

int main(int argc, char** argv)
{
  Gtk::Main app(argc, argv);

  GrapherWindow win;

  win.set_title("Grapher");
  win.set_default_size(600, 200);

  StapParser stapParser(win, win.w);
  launcher.setStapParser(&stapParser);

  if (argc > 1)
    {
      launcher.setArgv(argv + 1);
      launcher.setDeathCallback(&win);
      launcher.launch();
    }
  else
    {
      Glib::signal_io().connect(sigc::mem_fun(stapParser,
                                              &StapParser::ioCallback),
                                STDIN_FILENO,
                                Glib::IO_IN | Glib::IO_HUP);
    }
  Gtk::Main::run(win);
  launcher.cleanUp();
  return 0;
}

void GrapherWindow::addGraph()
{
  w.addGraph();
  
}
