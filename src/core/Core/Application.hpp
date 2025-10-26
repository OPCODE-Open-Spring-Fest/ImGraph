#pragma once

#include <SDL2/SDL.h>

#include <memory>
#include <string>
#include <vector>
#include <array>
#include <cstring>

#include "Core/Window.hpp"

// Forward declarations
struct ImDrawList;
struct ImVec2;

namespace App {

// Structure to hold data for a single expression
struct Expression {
  char text[1024] = "";
  std::array<float, 4> color = {0.25f, 0.5f, 0.78f, 1.0f}; // RGBA (default blue)
  bool enabled = true;
  
  Expression() = default;
  Expression(const char* expr, float r, float g, float b, float a = 1.0f) 
    : color{r, g, b, a}, enabled(true) {
#ifdef _MSC_VER
    strncpy_s(text, expr, sizeof(text) - 1);
#else
    strncpy(text, expr, sizeof(text) - 1);
#endif
    text[sizeof(text) - 1] = '\0';
  }
};

}

namespace App {

enum class ExitStatus : int { SUCCESS = 0, FAILURE = 1 };

class Application {
 public:
  explicit Application(const std::string& title);
  ~Application();

  Application(const Application&) = delete;
  Application(Application&&) = delete;
  Application& operator=(Application other) = delete;
  Application& operator=(Application&& other) = delete;

  ExitStatus run();
  void stop();

  void on_event(const SDL_WindowEvent& event);
  void on_minimize();
  void on_shown();
  void on_close();
  
  // Helper method to plot a single expression
  void plot_expression(const Expression& expr, ImDrawList* draw_list, const ImVec2& origin, 
                      const ImVec2& canvas_sz, float lineThickness);

 private:
  ExitStatus m_exit_status{ExitStatus::SUCCESS};
  std::unique_ptr<Window> m_window{nullptr};

  bool m_running{true};
  bool m_minimized{false};
  bool m_show_some_panel{true};
  bool m_show_debug_panel{false};
  bool m_show_demo_panel{false};
  
  // Multi-expression support
  std::vector<Expression> m_expressions;
  float m_zoom{100.0f};
  bool m_sidebar_visible{true};
  bool m_show_keyboard{false};
};

}  // namespace App
