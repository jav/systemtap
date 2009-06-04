#include "GraphWidget.hxx"
#include "StapParser.hxx"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <map>

#include <gtkmm.h>
#include <gtkmm/stock.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

using namespace systemtap;

class GrapherWindow : public Gtk::Window
{
public:
  GrapherWindow();
  virtual ~GrapherWindow() {}
  Gtk::VBox m_Box;
  GraphWidget w;
protected:
  virtual void on_menu_file_quit();
  // menu support
  Glib::RefPtr<Gtk::UIManager> m_refUIManager;
  Glib::RefPtr<Gtk::ActionGroup> m_refActionGroup;

};

GrapherWindow::GrapherWindow()
{
  set_title("systemtap grapher");
  add(m_Box);
  w.setExtents(0.0, 1.0, 5.0, 0.0);
  w.setLineWidth(2);



  //Create actions for menus and toolbars:
  m_refActionGroup = Gtk::ActionGroup::create();
  //File menu:
  m_refActionGroup->add(Gtk::Action::create("FileMenu", "File"));
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
  if(pMenubar)
    m_Box.pack_start(*pMenubar, Gtk::PACK_SHRINK);
  m_Box.pack_start(w, Gtk::PACK_EXPAND_WIDGET);
  w.show();

  show_all_children();

}
void GrapherWindow::on_menu_file_quit()
{
  hide();
}

int main(int argc, char** argv)
{
  Gtk::Main app(argc, argv);

  GrapherWindow win;

  win.set_title("Grapher");
  win.set_default_size(600, 200);

  StapParser stapParser(win, win.w);

  int childPid = -1;
  if (argc > 1)
    {
      int pipefd[2];
      if (pipe(pipefd) < 0)
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
          dup2(pipefd[0], 0);
          close(pipefd[0]);
        }
      else
        {
          dup2(pipefd[1], 1);
          close(pipefd[1]);
          execlp("stap", "stap", argv[1], static_cast<char*>(0));
          exit(1);
          return 1;
        }
     }
   Glib::signal_io().connect(sigc::mem_fun(stapParser,
                                           &StapParser::ioCallback),
                             0,
                             Glib::IO_IN);
   Gtk::Main::run(win);
   if (childPid > 0)
   kill(childPid, SIGTERM);
   int status;
   while (wait(&status) != -1)
     ;
   return 0;
}
