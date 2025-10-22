#include "Application.hpp"

#include <SDL2/SDL.h>
#include <backends/imgui_impl_sdl2.h>
#include <backends/imgui_impl_sdlrenderer2.h>
#include <imgui.h>

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

      static char function[1024] = "tanh(x)";
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
        const ImVec2 origin(canvas_p0.x + canvas_sz.x * 0.5f, canvas_p0.y + canvas_sz.y * 0.5f);
        float lineThickness = 6.0f;
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

                
                ImVec2 screen_pos(origin.x + static_cast<float>(vx * zoom),
                    origin.y - static_cast<float>(vy * zoom));
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

        if (!plotted) {
          // Fallback to y = f(x) plotting using variable x
          double x;

          exprtk::symbol_table<double> symbolTable;
          symbolTable.add_constants();
          addConstants(symbolTable);
          symbolTable.add_variable("x", x);

          exprtk::expression<double> expression;
          expression.register_symbol_table(symbolTable);

          exprtk::parser<double> parser;
          parser.compile(function, expression);

          for (x = -canvas_sz.x / (2 * zoom); x < canvas_sz.x / (2 * zoom); x += 0.05) {
            const double y = expression.value();

            
            ImVec2 screen_pos(origin.x + x * zoom, origin.y - y * zoom);
            points.push_back(screen_pos);
          }

          draw_list->AddPolyline(points.data(),
              points.size(),
              IM_COL32(199, 68, 64, 255),
              ImDrawFlags_None,
              lineThickness);
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
