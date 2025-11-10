#include "Application.hpp"
#include<vector>

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

#include "Core/expression.hpp"

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

  // All the expressions
  std::vector<Core::Expression> functions = {{{"tanh(x)"}, "#C74440", true}};

  double zoom = 100.0;

  //panning offset
  double offsetx=0;
  double offsety=0;

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

      if (event.type == SDL_MOUSEWHEEL) {
          const float zoom_speed = 1.1f;
          ImVec2 mousePos = ImGui::GetMousePos();

          // Keep previous zoom for compensation
          double prevZoom = zoom;

          if (event.wheel.y > 0) zoom *= zoom_speed;
          if (event.wheel.y < 0) zoom /= zoom_speed;
          zoom = std::clamp(zoom, 10.0, 1000.0);

          // Get viewport and origin (same as below in your render loop)
          const ImGuiViewport* viewport = ImGui::GetMainViewport();
          const ImVec2 base_pos = viewport->Pos;
          const ImVec2 base_size = viewport->Size;
          const ImVec2 origin(base_pos.x + base_size.x * 0.5f,
                              base_pos.y + base_size.y * 0.5f);

          // Convert mouse to world coordinates before zoom
          double worldX = (mousePos.x - origin.x - offsetx) / prevZoom;
          double worldY = (mousePos.y - origin.y - offsety) / -prevZoom;

          // Adjust offsets to keep cursor fixed
          offsetx = mousePos.x - origin.x - worldX * zoom;
          offsety = mousePos.y - origin.y + worldY * zoom;
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

      // Left Pane (expression)
      {
        ImGui::SetNextWindowPos(base_pos);
        ImGui::SetNextWindowSize(ImVec2(base_size.x * 0.25f, base_size.y));
        ImGui::Begin("Left Pane",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar);

        if (ImGui::Button("+ Add Function")) {
            functions.push_back({});
        }

        for (size_t i = 0; i < functions.size(); ++i) {
          std::string label = "##function" + std::to_string(i);
          ImGui::InputTextMultiline(label.c_str(),
              functions[i].expr.data(),
              functions[i].expr.size(),
              ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4));

          ImGui::Checkbox(("Visible##" + std::to_string(i)).c_str(), &functions[i].visible);

          float color[3];
          int r = 199, g = 68, b = 64;
          if (functions[i].color.size() == 7 && functions[i].color[0] == '#') {
              sscanf(functions[i].color.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
          }
          color[0] = r / 255.0f;
          color[1] = g / 255.0f;
          color[2] = b / 255.0f;

          if (ImGui::ColorEdit3(("Color##" + std::to_string(i)).c_str(), color)) {
              char hexColor[16];
              snprintf(hexColor, sizeof(hexColor), "#%02X%02X%02X",
                       int(color[0] * 255), int(color[1] * 255), int(color[2] * 255));
              functions[i].color = hexColor;
          }
        }

        ImGui::End();
      }

      // Right Pane (Graphing Area)
      {
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(base_pos.x + base_size.x * 0.25f, base_pos.y));
        ImGui::SetNextWindowSize(ImVec2(base_size.x * 0.75f, base_size.y));
        ImGui::Begin("Right Pane",
            nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                ImGuiWindowFlags_NoTitleBar);

        ImDrawList* draw_list = ImGui::GetWindowDrawList();

        // Define the graphing area within the window
        const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();     // Top-left corner of the window
        const ImVec2 canvas_sz = ImGui::GetContentRegionAvail();  // Size of the window
        const auto canvas_p1 =
            ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);  // Bottom-right

        // Define your coordinate system origin (e.g., center of the canvas)
        const ImVec2 origin(canvas_p0.x + canvas_sz.x * 0.5f, canvas_p0.y + canvas_sz.y * 0.5f);

        float lineThickness = 3.0f;

        //panning logic
        static bool isPanning = false;
        static ImVec2 panStart = ImVec2(0,0);

        //get mouse position and state
        ImGuiIO& io=ImGui::GetIO();
        ImVec2 mousePos = io.MousePos;

        // Check if mouse is over the canvas
        bool isHovered = ImGui::IsWindowHovered();

        // Left click pressed: start panning
        if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            isPanning = true;
            panStart = mousePos;
        }

        // Mouse released: stop panning
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            isPanning = false;
        }

        // If panning, update offset
        if (isPanning && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 dragDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            offsetx += dragDelta.x;
            offsety += dragDelta.y;
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        // 1. Draw Axes
        draw_list->AddLine(ImVec2(canvas_p0.x, origin.y+offsety),
            ImVec2(canvas_p1.x, origin.y+offsety),
            IM_COL32(0, 0, 0, 255),
            lineThickness);  // X-axis
        draw_list->AddLine(ImVec2(origin.x+offsetx, canvas_p0.y),
            ImVec2(origin.x+offsetx, canvas_p1.y),
            IM_COL32(0, 0, 0, 255),
            lineThickness);  // Y-axis

        // === Axis Markings (Ticks and Labels) ===
        {
            const ImU32 tickColor = IM_COL32(100, 100, 100, 255);
            const ImU32 textColor = IM_COL32(50, 50, 50, 255);
            const ImU32 gridColor = IM_COL32(200, 200, 200, 80);
            const float tickLength = 5.0f;  // pixel length of tick
            const float labelOffset = 15.0f;

            ImGuiIO& io = ImGui::GetIO();
            ImFont* font = io.FontDefault;

            // Step size for ticks (auto-adjust with zoom)
            double step = 1.0;
            double pixelsPerUnit = zoom;
            if (pixelsPerUnit > 400) step = 0.1;
            else if (pixelsPerUnit > 200) step = 0.25;
            else if (pixelsPerUnit > 100) step = 0.5;
            else if (pixelsPerUnit > 50) step = 1.0;
            else if (pixelsPerUnit > 20) step = 2.0;
            else if (pixelsPerUnit > 10) step = 5.0;
            else step = 10.0;

            // Determine world bounds
            const double xmin = (-canvas_sz.x / 2.0 - offsetx) / zoom;
            const double xmax = (canvas_sz.x / 2.0 - offsetx) / zoom;
            const double ymin = (-canvas_sz.y / 2.0 + offsety) / zoom;
            const double ymax = (canvas_sz.y / 2.0 + offsety) / zoom;

            // Round bounds to nearest multiple of step
            const double xstart = std::floor(xmin / step) * step;
            const double xend = std::ceil(xmax / step) * step;
            const double ystart = std::floor(ymin / step) * step;
            const double yend = std::ceil(ymax / step) * step;

            // X-axis ticks
            for (double xw = xstart; xw <= xend; xw += step) {
                float xScreen = origin.x + xw * zoom + offsetx;
                draw_list->AddLine(ImVec2(xScreen, origin.y + offsety - tickLength),
                                  ImVec2(xScreen, origin.y + offsety + tickLength),
                                  tickColor, 1.0f);

                if (std::fabs(xw) > 1e-6) { // skip labeling origin
                    char label[32];
                    snprintf(label, sizeof(label), "%.2f", xw);
                    draw_list->AddText(font, 13.0f,
                                      ImVec2(xScreen - 10, origin.y + offsety + labelOffset),
                                      textColor, label);
                }
            }

            // Y-axis ticks
            for (double yw = ystart; yw <= yend; yw += step) {
                float yScreen = origin.y - yw * zoom + offsety;
                draw_list->AddLine(ImVec2(origin.x + offsetx - tickLength, yScreen),
                                  ImVec2(origin.x + offsetx + tickLength, yScreen),
                                  tickColor, 1.0f);

                if (std::fabs(yw) > 1e-6) { // skip labeling origin
                    char label[32];
                    snprintf(label, sizeof(label), "%.2f", yw);
                    draw_list->AddText(font, 13.0f,
                                      ImVec2(origin.x + offsetx + labelOffset, yScreen - 10),
                                      textColor, label);
                }
            }

            // Vertical grid lines
            for (double xw = xstart; xw <= xend; xw += step) {
                float xScreen = origin.x + xw * zoom + offsetx;
                draw_list->AddLine(
                    ImVec2(xScreen, canvas_p0.y),
                    ImVec2(xScreen, canvas_p1.y),
                    gridColor,
                    1.0f
                );
            }

            // Horizontal grid lines
            for (double yw = ystart; yw <= yend; yw += step) {
                float yScreen = origin.y - yw * zoom + offsety;
                draw_list->AddLine(
                    ImVec2(canvas_p0.x, yScreen),
                    ImVec2(canvas_p1.x, yScreen),
                    gridColor,
                    1.0f
                );
            }
        }

        double x;

        exprtk::symbol_table<double> symbolTable;
        symbolTable.add_constants();
        addConstants(symbolTable);
        symbolTable.add_variable("x", x);

        exprtk::expression<double> expression;
        expression.register_symbol_table(symbolTable);

        exprtk::parser<double> parser;
        std::vector<exprtk::expression<double>> expressions(functions.size());

        for (size_t i = 0; i < functions.size(); ++i) {
            if (!functions[i].visible) continue;
            expressions[i].register_symbol_table(symbolTable);
            parser.compile(functions[i].expr.data(), expressions[i]);
        }

        for (size_t i = 0; i < functions.size(); ++i) {
            if (!functions[i].visible) continue;

            auto& expr = expressions[i];
            std::vector<ImVec2> points;
            points.reserve(static_cast<size_t>(canvas_sz.x / 2)); // small optimization

            // Compute X range based on panning offset
            const double xmin = (-canvas_sz.x / 2.0 - offsetx) / zoom;
            const double xmax = (canvas_sz.x / 2.0 - offsetx) / zoom;
            const double step = 0.05;  // smaller → smoother curve

            for (x = xmin; x < xmax; x += step) {
                const double y = expr.value();  // evaluate using current 'x' bound in symbolTable
                points.push_back(ImVec2(origin.x + (x * zoom) + offsetx,
                                        origin.y - (y * zoom) + offsety));
            }

            // Parse color (from hex string like "#C74440")
            int r = 199, g = 68, b = 64;
            if (functions[i].color.size() == 7 && functions[i].color[0] == '#') {
                sscanf(functions[i].color.c_str() + 1, "%02x%02x%02x", &r, &g, &b);
            }

            // Draw the function line
            if (!points.empty()) {
                draw_list->AddPolyline(points.data(), points.size(),
                                      IM_COL32(r, g, b, 255),
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
