#include "Application.hpp"

#include <SDL2/SDL.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <imgui.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "Core/DPIHandler.hpp"
#include "Core/Debug/Instrumentor.hpp"
#include "Core/Log.hpp"
#include "Core/Resources.hpp"
#include "Core/Window.hpp"
#include "Settings/Project.hpp"
#include "exprtk.hpp"
#include "funcs.hpp"

namespace App {

Application::Application(const std::string& title) {
  APP_PROFILE_FUNCTION();

  const unsigned int init_flags{SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER};
  if (SDL_Init(init_flags) != 0) {
    APP_ERROR("Error: %s\n", SDL_GetError());
    m_exit_status = ExitStatus::FAILURE;
  }

  m_window = std::make_unique<Window>(Window::Settings{title});
}

Application::~Application() {
  APP_PROFILE_FUNCTION();

  ImGui_ImplSDLRenderer2_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  SDL_Quit();
}

ExitStatus App::Application::run() {
  APP_PROFILE_FUNCTION();

  if (m_exit_status == ExitStatus::FAILURE) {
    return m_exit_status;
  }

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io{ImGui::GetIO()};

  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_DockingEnable |
                    ImGuiConfigFlags_ViewportsEnable;

  const std::string user_config_path{SDL_GetPrefPath(COMPANY_NAMESPACE.c_str(), APP_NAME.c_str())};
  APP_DEBUG("User config path: {}", user_config_path);

  // Absolute imgui.ini path to preserve settings independent of app location.
  static const std::string imgui_ini_filename{user_config_path + "imgui.ini"};
  io.IniFilename = imgui_ini_filename.c_str();

  // ImGUI font
  const float font_scaling_factor{DPIHandler::get_scale()};
  const float font_size{18.0F * font_scaling_factor};
  const std::string font_path{Resources::font_path("Manrope.ttf").generic_string()};

  if (Resources::exists(font_path)) {
    io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size);
    io.FontDefault = io.Fonts->AddFontFromFileTTF(font_path.c_str(), font_size);
  } else {
    APP_WARN("Could not find font file under: {}", font_path.c_str());
  }

  DPIHandler::set_global_font_scaling(&io);

  // Setup Platform/Renderer backends
  ImGui_ImplSDL2_InitForSDLRenderer(m_window->get_native_window(), m_window->get_native_renderer());
  ImGui_ImplSDLRenderer2_Init(m_window->get_native_renderer());

  m_running = true;
  while (m_running) {
    APP_PROFILE_SCOPE("MainLoop");

    SDL_Event event{};
    while (SDL_PollEvent(&event) == 1) {
      APP_PROFILE_SCOPE("EventPolling");

      ImGui_ImplSDL2_ProcessEvent(&event);

      if (event.type == SDL_QUIT) {
        stop();
      }

      if (event.type == SDL_WINDOWEVENT &&
          event.window.windowID == SDL_GetWindowID(m_window->get_native_window())) {
        on_event(event.window);
      }
    }

    // Start the Dear ImGui frame
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    if (!m_minimized) {
      const ImGuiViewport* viewport = ImGui::GetMainViewport();
      const ImVec2 base_pos = viewport->Pos;
      const ImVec2 base_size = viewport->Size;

      static char function[1024] = "r = 1 + 0.5*cos(theta)";
      static float zoom = 100.0f;

      // Left Pane (expression)
      {
        ImGui::SetNextWindowPos(base_pos);
        ImGui::SetNextWindowSize(ImVec2(base_size.x * 0.25f, base_size.y));
        ImGui::Begin("Left Pane", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        ImGui::InputTextMultiline("##search", function, sizeof(function), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));
        ImGui::SliderFloat("Graph Scale", &zoom, 10.0f, 500.0f, "%.1f");
        ImGui::End();
      }

      // Right Pane (Graphing Area)
      {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(base_pos.x + base_size.x * 0.25f, base_pos.y));
        ImGui::SetNextWindowSize(ImVec2(base_size.x * 0.75f, base_size.y));
        ImGui::Begin("Right Pane", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
        const auto canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);
        // --- PANNING IMPLEMENTATION BEGIN ---
        static bool isPanning = false;
        static ImVec2 lastMousePos = {0.0f, 0.0f};
        static ImVec2 originOffset = {0.0f, 0.0f};

        // Get mouse position
        ImVec2 mousePos = ImGui::GetMousePos();

        // Detect click start only when cursor is inside the graphing area
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            isPanning = true;
            lastMousePos = mousePos;
        }

        // Stop panning when mouse released
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            isPanning = false;
        }

        // While dragging, update origin offset
        if (isPanning && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 delta = ImVec2(mousePos.x - lastMousePos.x, mousePos.y - lastMousePos.y);
            originOffset.x += delta.x;
            originOffset.y += delta.y;
            lastMousePos = mousePos;
        }

        // Apply offset to origin
        const ImVec2 origin(
            canvas_p0.x + canvas_sz.x * 0.5f + originOffset.x,
            canvas_p0.y + canvas_sz.y * 0.5f + originOffset.y
        );
        // --- PANNING IMPLEMENTATION END ---

        float lineThickness = 6.0f;
        
        // --- Thin integer gridlines with adaptive spacing ---
        const ImU32 gridColor = IM_COL32(200, 200, 200, 255); // light gray
        const float gridThickness = 1.0f;                     // thin lines
        const float minPixelSpacing = 20.0f;                  // minimum spacing in pixels
        
        // Dynamic step based on zoom
        float step = 1.0f;
        while (step * zoom < minPixelSpacing)
        step *= 2.0f;
        
        
        // Vertical gridlines
        for (float x = 0; origin.x + x * zoom < canvas_p1.x; x += step) {
          if (x != 0) {
            draw_list->AddLine(ImVec2(origin.x + x * zoom, canvas_p0.y),
            ImVec2(origin.x + x * zoom, canvas_p1.y),
            gridColor, gridThickness);
            draw_list->AddLine(ImVec2(origin.x - x * zoom, canvas_p0.y),
            ImVec2(origin.x - x * zoom, canvas_p1.y),
            gridColor, gridThickness);
          }
        }
        
        // Horizontal gridlines
        for (float y = 0; origin.y + y * zoom < canvas_p1.y; y += step) {
          if (y != 0) {
            draw_list->AddLine(ImVec2(canvas_p0.x, origin.y + y * zoom),
            ImVec2(canvas_p1.x, origin.y + y * zoom),
            gridColor, gridThickness);
            draw_list->AddLine(ImVec2(canvas_p0.x, origin.y - y * zoom),
            ImVec2(canvas_p1.x, origin.y - y * zoom),
            gridColor, gridThickness);
          }
        }
        
        draw_list->AddLine(ImVec2(canvas_p0.x, origin.y), ImVec2(canvas_p1.x, origin.y), IM_COL32(0, 0, 0, 255), lineThickness);
        draw_list->AddLine(ImVec2(origin.x, canvas_p0.y), ImVec2(origin.x, canvas_p1.y), IM_COL32(0, 0, 0, 255), lineThickness);
        std::vector<ImVec2> points;

        // (f(t), g(t))
        std::string func_str(function);
        

        bool plotted = false;

        if (!func_str.empty() && func_str.front() == '(' && func_str.back() == ')') {
          const std::string inner = func_str.substr(1, func_str.size() - 2);
          // top-level comma separating f and g
          int depth = 0;
          size_t split_pos = std::string::npos;
          for (size_t i = 0; i < inner.size(); ++i) {
            char c = inner[i];
            if (c == '(')
              ++depth;
            else if (c == ')')
              --depth;
            else if (c == ',' && depth == 0) {
              split_pos = i;
              break;
            }
          }

          if (split_pos != std::string::npos) {
            std::string fx = trim(inner.substr(0, split_pos));
            std::string gx = trim(inner.substr(split_pos + 1));

            // Prepare exprtk 
            double t = 0.0;
            exprtk::symbol_table<double> sym_t;
            sym_t.add_constants();
            addConstants(sym_t);
            sym_t.add_variable("t", t);

            exprtk::expression<double> expr_fx;
            expr_fx.register_symbol_table(sym_t);
            exprtk::expression<double> expr_gx;
            expr_gx.register_symbol_table(sym_t);

            exprtk::parser<double> parser;
            bool ok_fx = parser.compile(fx, expr_fx);
            bool ok_gx = parser.compile(gx, expr_gx);

            if (ok_fx && ok_gx) {
              // iterate t  
              const double t_min = -10.0;
              const double t_max = 10.0;
              const double t_step = 0.02;  

              for (t = t_min; t <= t_max; t += t_step) {
               const double vx = expr_fx.value();
               const double vy = expr_gx.value();
              // The 'origin' variable already includes the pan offset.
                ImVec2 screen_pos(
                         origin.x + static_cast<float>(vx * zoom),
                          origin.y - static_cast<float>(vy * zoom)
                         );
                points.push_back(screen_pos);
              }

              // Draw  curve
              draw_list->AddPolyline(points.data(),
                  points.size(),
                  IM_COL32(64, 128, 199, 255),
                  ImDrawFlags_None,
                  lineThickness);
              plotted = true;
            }
          }
        }

        // check for inequality 
        if (!plotted && hasInequalityOperator(func_str)) {
          double x = 0.0, y = 0.0;
          exprtk::symbol_table<double> symbol_table;
          symbol_table.add_constants();
          addConstants(symbol_table);
          symbol_table.add_variable("x", x);
          symbol_table.add_variable("y", y);
          
          exprtk::expression<double> expression;
          expression.register_symbol_table(symbol_table);
          
          exprtk::parser<double> parser;
          
          if (parser.compile(func_str, expression)) {
            // grid parameters
            const double x_min = -canvas_sz.x / (2 * zoom);
            const double x_max = canvas_sz.x / (2 * zoom);
            const double y_min = -canvas_sz.y / (2 * zoom);
            const double y_max = canvas_sz.y / (2 * zoom);
            
            // adaptive step size with performance limit
            const double step = std::max(0.025, 1.5 / zoom);
            const ImU32 inequality_color = IM_COL32(100, 150, 255, 180);
            const float dot_size = std::max(1.5f, zoom / 60.0f);
            
            
            for (y = y_min; y <= y_max; y += step) {
              for (x = x_min; x <= x_max; x += step) {
                
                // if expression is true, plot the point
                if (expression.value() == 1.0) {
                  ImVec2 screen_pos(origin.x + static_cast<float>(x * zoom),
                                   origin.y - static_cast<float>(y * zoom));
                  draw_list->AddCircleFilled(screen_pos, dot_size, inequality_color);
                }
              }
            }
            
            plotted = true;
          }
        }

        // check for implicit form: f(x,y) = g(x,y)
        if (!plotted) {
          size_t equals_pos = findTopLevelEquals(func_str);      
          bool has_double_equals = hasEqualsEqualsOperator(func_str);

          if (equals_pos != std::string::npos || has_double_equals) {
          
            std::string implicit_expr;

            if (has_double_equals) {
              // Handle == operator
              std::string temp_str = func_str;
              int depth = 0;
              size_t eq_pos = std::string::npos;
              
              for (size_t i = 0; i < temp_str.size() - 1; ++i) {
                char c = temp_str[i];
                if (c == '(') ++depth;
                else if (c == ')') --depth;
                else if (depth == 0 && c == '=' && temp_str[i+1] == '=') {
                  eq_pos = i;
                  break;
                }
              }
              
              if (eq_pos != std::string::npos) {
                std::string lhs = trim(temp_str.substr(0, eq_pos));
                std::string rhs = trim(temp_str.substr(eq_pos + 2)); // +2 to skip ==
                implicit_expr = "(" + lhs + ") - (" + rhs + ")";
              }
            } else {
              // Handle = operator
              std::string lhs = trim(func_str.substr(0, equals_pos));
              std::string rhs = trim(func_str.substr(equals_pos + 1));
              implicit_expr = "(" + lhs + ") - (" + rhs + ")";
            }

            if (!implicit_expr.empty()) {
              
            // setup exprtk with x and y variables
            double x = 0.0, y = 0.0;
            exprtk::symbol_table<double> symbolTable;
            symbolTable.add_constants();
            addConstants(symbolTable);
            symbolTable.add_variable("x", x);
            symbolTable.add_variable("y", y);
            
            exprtk::expression<double> expression;
            expression.register_symbol_table(symbolTable);
            
            exprtk::parser<double> parser;
            bool compile_ok = parser.compile(implicit_expr, expression);
            
            if (compile_ok) {
              // grid parameters
              const double x_min = -canvas_sz.x / (2 * zoom);
              const double x_max = canvas_sz.x / (2 * zoom);
              const double y_min = -canvas_sz.y / (2 * zoom);
              const double y_max = canvas_sz.y / (2 * zoom);
              const double step = std::max(0.008, 1.0 / zoom); //dynamic step based on zoom level
              
              const ImU32 implicit_color = IM_COL32(64, 199, 128, 255);
              const float dot_radius = 2.5f;
              
              // scan horizontally for sign changes
              for (y = y_min; y <= y_max; y += step) {
                double prev_val = 0.0;
                bool first = true;
                
                for (x = x_min; x <= x_max; x += step) {
                  double curr_val = expression.value();
                  
                  if (!first && prev_val * curr_val < 0) {
                    // sign change detected
                    double t = prev_val / (prev_val - curr_val);
                    double x_zero = (x - step) + t * step;
                    double y_zero = y;
                    
                    // transform to screen coordinates and draw immediately
                    ImVec2 screen_pos(origin.x + static_cast<float>(x_zero * zoom),
                                     origin.y - static_cast<float>(y_zero * zoom));
                    draw_list->AddCircleFilled(screen_pos, dot_radius, implicit_color);
                  }
                  
                  prev_val = curr_val;
                  first = false;
                }
              }
              
              // vertical scan
              for (x = x_min; x <= x_max; x += step) {
                double prev_val = 0.0;
                bool first = true;
                
                for (y = y_min; y <= y_max; y += step) {
                  double curr_val = expression.value();
                  
                  if (!first && prev_val * curr_val < 0) {
                    // sign change detected
                    double t = prev_val / (prev_val - curr_val);
                    double x_zero = x;
                    double y_zero = (y - step) + t * step;
        
                    ImVec2 screen_pos(origin.x + static_cast<float>(x_zero * zoom),
                                     origin.y - static_cast<float>(y_zero * zoom));
                    draw_list->AddCircleFilled(screen_pos, dot_radius, implicit_color);
                  }
                  
                  prev_val = curr_val;
                  first = false;
                }
              }
              
                plotted = true;
              }
            }
          }
        }

        if (!plotted) {
          std::string func_str(function);
          bool is_polar = func_str.find("r=") != std::string::npos || func_str.find("r =") != std::string::npos;

          if (is_polar) {
            double theta;

            exprtk::symbol_table<double> symbolTable;
            symbolTable.add_constants();
            addConstants(symbolTable);
            symbolTable.add_variable("theta", theta);

            exprtk::expression<double> expression;
            expression.register_symbol_table(symbolTable);

            std::string polar_function = func_str;
            size_t eq_pos = func_str.find("r=");
            if (eq_pos == std::string::npos) {
              eq_pos = func_str.find("r =");
            }
            if (eq_pos != std::string::npos) {
              size_t start_pos = func_str.find("=", eq_pos) + 1;
              polar_function = func_str.substr(start_pos);
              polar_function.erase(0, polar_function.find_first_not_of(" \t"));
            }

            exprtk::parser<double> parser;
            if (parser.compile(polar_function, expression)) {
              const double theta_min = 0.0;
              const double theta_max = 4.0 * M_PI;  
              const double theta_step = 0.02;

              for (theta = theta_min; theta <= theta_max; theta += theta_step) {
                const double r = expression.value();
                
                const double x = r * cos(theta);
                const double y = r * sin(theta);

                ImVec2 screen_pos(origin.x + static_cast<float>(x * zoom),
                    origin.y - static_cast<float>(y * zoom));
                points.push_back(screen_pos);
              }

              draw_list->AddPolyline(points.data(),
                  points.size(),
                  IM_COL32(128, 64, 199, 255),
                  ImDrawFlags_None,
                  lineThickness);
            }
          } else {
            double x;

            exprtk::symbol_table<double> symbolTable;
            symbolTable.add_constants();
            addConstants(symbolTable);
            symbolTable.add_variable("x", x);

            exprtk::expression<double> expression;
            expression.register_symbol_table(symbolTable);

            exprtk::parser<double> parser;
            parser.compile(function, expression);

          // Calculate the visible x-range in world-space, accounting for the pan
          const float world_x_min = (-canvas_sz.x * 0.5f - originOffset.x) / zoom;
          const float world_x_max = ( canvas_sz.x * 0.5f - originOffset.x) / zoom;

          // Evaluate the function across the correct visible world-range
          for (x = world_x_min; x <= world_x_max; x += 0.05) {
            const double y = expression.value();
           // The 'origin' variable already includes the pan offset.
            ImVec2 screen_pos(
            origin.x + static_cast<float>(x * zoom),
            origin.y - static_cast<float>(y * zoom)
            );
            points.push_back(screen_pos);
          }

            draw_list->AddPolyline(points.data(),
                points.size(),
                IM_COL32(199, 68, 64, 255),
                ImDrawFlags_None,
                lineThickness);
          }
        }

        ImGui::End();
        ImGui::PopStyleColor();
      }
    }

    // Rendering
    ImGui::Render();

    SDL_RenderSetScale(m_window->get_native_renderer(),
        io.DisplayFramebufferScale.x,
        io.DisplayFramebufferScale.y);
    SDL_SetRenderDrawColor(m_window->get_native_renderer(), 100, 100, 100, 255);
    SDL_RenderClear(m_window->get_native_renderer());
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), m_window->get_native_renderer());
    SDL_RenderPresent(m_window->get_native_renderer());
  }

  return m_exit_status;
}

void App::Application::stop() {
  APP_PROFILE_FUNCTION();

  m_running = false;
}

void Application::on_event(const SDL_WindowEvent& event) {
  APP_PROFILE_FUNCTION();

  switch (event.event) {
    case SDL_WINDOWEVENT_CLOSE:
      return on_close();
    case SDL_WINDOWEVENT_MINIMIZED:
      return on_minimize();
    case SDL_WINDOWEVENT_SHOWN:
      return on_shown();
    default:
      // Do nothing otherwise
      return;
  }
}

void Application::on_minimize() {
  APP_PROFILE_FUNCTION();

  m_minimized = true;
}

void Application::on_shown() {
  APP_PROFILE_FUNCTION();

  m_minimized = false;
}

void Application::on_close() {
  APP_PROFILE_FUNCTION();

  stop();
}

}  // namespace App
