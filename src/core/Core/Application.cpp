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
  
  // Initialize with one default expression
  m_expressions.emplace_back("r = 1 + 0.5*cos(theta)", 0.78f, 0.27f, 0.25f);
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
      
      const float sidebar_width = m_sidebar_visible ? 240.0f : 50.0f;
      const float topbar_height = 50.0f;

      // Dark top bar (Desmos style)
      {
        ImGui::SetNextWindowPos(base_pos);
        ImGui::SetNextWindowSize(ImVec2(base_size.x, topbar_height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(15, 10));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.25f, 0.25f, 0.25f, 1.0f));
        ImGui::Begin("TopBar", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::Text("Untitled Graph");
        ImGui::PopStyleColor();
        
        ImGui::SameLine(200);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.55f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("Save", ImVec2(60, 30))) {}
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        
        ImGui::SameLine(base_size.x * 0.5f - 30);
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::Text("desmos");
        ImGui::PopStyleColor();
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::End();
      }

      // Sidebar
      if (m_sidebar_visible) {
        // Toolbar
        {
          ImGui::SetNextWindowPos(ImVec2(base_pos.x, base_pos.y + topbar_height));
          ImGui::SetNextWindowSize(ImVec2(240, 50));
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
          ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
          ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
          
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
          ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
          if (ImGui::Button("+", ImVec2(30, 30))) {
            static const std::array<std::array<float, 3>, 10> desmos_colors = {{
              {0.78f, 0.27f, 0.25f}, {0.25f, 0.5f, 0.78f}, {0.27f, 0.63f, 0.27f},
              {0.59f, 0.29f, 0.64f}, {0.93f, 0.49f, 0.18f}, {0.15f, 0.68f, 0.68f},
              {0.89f, 0.24f, 0.59f}, {0.47f, 0.33f, 0.28f}, {0.2f, 0.2f, 0.2f}, {0.0f, 0.5f, 0.25f}
            }};
            auto color = desmos_colors[m_expressions.size() % desmos_colors.size()];
            m_expressions.emplace_back("", color[0], color[1], color[2]);
          }
          ImGui::PopStyleVar();
          ImGui::PopStyleColor(3);
          
          ImGui::SameLine();
          ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
          if (ImGui::Button("<<", ImVec2(30, 30))) { m_sidebar_visible = false; }
          ImGui::PopStyleColor(3);
          
          ImGui::PopStyleColor();
          ImGui::PopStyleVar();
          ImGui::End();
        }
        
        // Expression list
        {
          ImGui::SetNextWindowPos(ImVec2(base_pos.x, base_pos.y + topbar_height + 50));
          ImGui::SetNextWindowSize(ImVec2(240, base_size.y - topbar_height - 50));
          ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
          ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 1));
          ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
          ImGui::Begin("Expressions", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
          
          for (size_t i = 0; i < m_expressions.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 8));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::BeginChild("ExprRow", ImVec2(-FLT_MIN, 50), true, ImGuiWindowFlags_NoScrollbar);
            
            ImGui::SetCursorPos(ImVec2(8, 15));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            ImGui::Text("%zu", i + 1);
            ImGui::PopStyleColor();
            
            // Draw color dot manually
            ImDrawList* draw_list_ui = ImGui::GetWindowDrawList();
            ImVec2 color_dot_pos = ImGui::GetWindowPos();
            color_dot_pos.x += 34;
            color_dot_pos.y += 25;
            const ImU32 color_circle = IM_COL32(
              static_cast<int>(m_expressions[i].color[0] * 255),
              static_cast<int>(m_expressions[i].color[1] * 255),
              static_cast<int>(m_expressions[i].color[2] * 255), 255
            );
            draw_list_ui->AddCircleFilled(color_dot_pos, 8.0f, color_circle);
            
            // Invisible color picker button over the dot
            ImGui::SetCursorPos(ImVec2(26, 17));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
            ImGui::SetNextItemWidth(16);
            ImGui::ColorEdit4("##color", m_expressions[i].color.data(), 
                             ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | 
                             ImGuiColorEditFlags_NoBorder | ImGuiColorEditFlags_NoTooltip | 
                             ImGuiColorEditFlags_AlphaPreviewHalf);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(2);
            
            // Expression text input
            ImGui::SetCursorPos(ImVec2(60, 13));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 6));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f, 0.2f, 0.2f, 1.0f));
            ImGui::SetNextItemWidth(120);
            ImGui::InputText("##expr", m_expressions[i].text, sizeof(m_expressions[i].text));
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar();
            
            ImGui::SetCursorPos(ImVec2(200, 13));
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
            if (ImGui::Button("x", ImVec2(25, 25))) {
              m_expressions.erase(m_expressions.begin() + i);
              ImGui::PopStyleColor(3);
              ImGui::EndChild();
              ImGui::PopStyleColor();
              ImGui::PopStyleVar();
              ImGui::PopID();
              break;
            }
            ImGui::PopStyleColor(3);
            
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
            ImGui::PopID();
          }
          
          ImGui::PopStyleColor();
          ImGui::PopStyleVar(2);
          ImGui::End();
        }
      } else {
        // Collapsed sidebar - just >> button with tooltip
        ImGui::SetNextWindowPos(ImVec2(base_pos.x, base_pos.y + topbar_height));
        ImGui::SetNextWindowSize(ImVec2(45, 45));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 5));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        ImGui::Begin("CollapsedBar", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        
        // Just >> button with border
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
        if (ImGui::Button(">>", ImVec2(33, 33))) {
          m_sidebar_visible = true;
        }
        // Tooltip on hover
        if (ImGui::IsItemHovered()) {
          ImGui::SetTooltip("Show List");
        }
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
        ImGui::End();
      }

      // Right Pane (Graphing Area) - Exact Desmos style
      {
        const float topbar_height = 50.0f;
        const float sidebar_width = m_sidebar_visible ? 240.0f : 40.0f;
        const float toolbar_width = 50.0f;
        
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
        ImGui::SetNextWindowPos(ImVec2(base_pos.x + sidebar_width, base_pos.y + topbar_height));
        ImGui::SetNextWindowSize(ImVec2(base_size.x - sidebar_width - toolbar_width, base_size.y - topbar_height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::Begin("Graph", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        const ImVec2 canvas_p0 = ImGui::GetCursorScreenPos();
        const ImVec2 canvas_sz = ImGui::GetContentRegionAvail();
        const auto canvas_p1 = ImVec2(canvas_p0.x + canvas_sz.x, canvas_p0.y + canvas_sz.y);
        const ImVec2 origin(canvas_p0.x + canvas_sz.x * 0.5f, canvas_p0.y + canvas_sz.y * 0.5f);
        
        // Draw fine grid (Desmos style - very light)
        const float grid_step = m_zoom;
        const ImU32 grid_color_fine = IM_COL32(245, 245, 245, 255);
        const ImU32 grid_color_major = IM_COL32(230, 230, 230, 255);
        
        // Fine vertical grid lines
        for (float x = fmodf(origin.x - canvas_p0.x, grid_step); x < canvas_sz.x; x += grid_step) {
          float abs_x = x + canvas_p0.x;
          draw_list->AddLine(ImVec2(abs_x, canvas_p0.y), ImVec2(abs_x, canvas_p1.y), grid_color_fine, 1.0f);
        }
        
        // Fine horizontal grid lines
        for (float y = fmodf(origin.y - canvas_p0.y, grid_step); y < canvas_sz.y; y += grid_step) {
          float abs_y = y + canvas_p0.y;
          draw_list->AddLine(ImVec2(canvas_p0.x, abs_y), ImVec2(canvas_p1.x, abs_y), grid_color_fine, 1.0f);
        }
        
        // Major grid lines every 5 units
        for (float x = fmodf(origin.x - canvas_p0.x, grid_step * 2); x < canvas_sz.x; x += grid_step * 2) {
          float abs_x = x + canvas_p0.x;
          draw_list->AddLine(ImVec2(abs_x, canvas_p0.y), ImVec2(abs_x, canvas_p1.y), grid_color_major, 1.0f);
        }
        
        for (float y = fmodf(origin.y - canvas_p0.y, grid_step * 2); y < canvas_sz.y; y += grid_step * 2) {
          float abs_y = y + canvas_p0.y;
          draw_list->AddLine(ImVec2(canvas_p0.x, abs_y), ImVec2(canvas_p1.x, abs_y), grid_color_major, 1.0f);
        }
        
        // Draw axes (dark, like Desmos)
        float axisThickness = 1.5f;
        const ImU32 axis_color = IM_COL32(0, 0, 0, 255);
        draw_list->AddLine(ImVec2(canvas_p0.x, origin.y), ImVec2(canvas_p1.x, origin.y), axis_color, axisThickness);
        draw_list->AddLine(ImVec2(origin.x, canvas_p0.y), ImVec2(origin.x, canvas_p1.y), axis_color, axisThickness);
        
        // Draw axis labels (exactly like Desmos)
        ImGui::PushFont(io.FontDefault);
        const ImU32 label_color = IM_COL32(130, 130, 130, 255);
        
        // X-axis labels (every 2 units for readability)
        for (float x = fmodf(origin.x - canvas_p0.x, grid_step * 2); x < canvas_sz.x; x += grid_step * 2) {
          float world_x = (x + canvas_p0.x - origin.x) / m_zoom;
          if (fabs(world_x) > 0.5f) {
            char label[16];
            snprintf(label, sizeof(label), "%.0f", world_x);
            ImVec2 text_size = ImGui::CalcTextSize(label);
            ImVec2 label_pos(x + canvas_p0.x - text_size.x * 0.5f, origin.y + 8);
            
            if (fabs(x + canvas_p0.x - origin.x) > 20) {
              draw_list->AddText(label_pos, label_color, label);
            }
          }
        }
        
        // Y-axis labels (every 2 units for readability)
        for (float y = fmodf(origin.y - canvas_p0.y, grid_step * 2); y < canvas_sz.y; y += grid_step * 2) {
          float world_y = -(y + canvas_p0.y - origin.y) / m_zoom;
          if (fabs(world_y) > 0.5f) {
            char label[16];
            snprintf(label, sizeof(label), "%.0f", world_y);
            ImVec2 text_size = ImGui::CalcTextSize(label);
            ImVec2 label_pos(origin.x - text_size.x - 8, y + canvas_p0.y - text_size.y * 0.5f);
            
            if (fabs(y + canvas_p0.y - origin.y) > 15) {
              draw_list->AddText(label_pos, label_color, label);
            }
          }
        }
        ImGui::PopFont();
        
        // Plot all expressions (Desmos style - smooth anti-aliased lines)
        float lineThickness = 2.5f;
        for (const auto& expr : m_expressions) {
          plot_expression(expr, draw_list, origin, canvas_sz, lineThickness);
        }
        
        // "powered by desmos" watermark (bottom right)
        ImGui::SetCursorPos(ImVec2(canvas_sz.x - 100, canvas_sz.y - 30));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 0.5f));
        ImGui::Text("powered by\n  desmos");
        ImGui::PopStyleColor();

        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::PopStyleColor();
      }
      
      // Right toolbar
      {
        const float topbar_height = 50.0f;
        ImGui::SetNextWindowPos(ImVec2(base_pos.x + base_size.x - 50, base_pos.y + topbar_height + 10));
        ImGui::SetNextWindowSize(ImVec2(50, 150));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(5, 10));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(1.0f, 1.0f, 1.0f, 0.0f));
        ImGui::Begin("RightTools", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
        
        // Pencil/draw tool
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("✎", ImVec2(35, 35))) {}
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        
        ImGui::Spacing();
        
        // Zoom in
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("+", ImVec2(35, 35))) {
          m_zoom = std::min(500.0f, m_zoom * 1.2f);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        
        ImGui::Spacing();
        
        // Zoom out
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 0.3f, 0.3f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
        if (ImGui::Button("−", ImVec2(35, 35))) {
          m_zoom = std::max(10.0f, m_zoom / 1.2f);
        }
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(3);
        
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
        ImGui::End();
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

void Application::plot_expression(const Expression& expr, ImDrawList* draw_list, 
                                  const ImVec2& origin, const ImVec2& canvas_sz, 
                                  float lineThickness) {
  if (!expr.enabled || strlen(expr.text) == 0) {
    return;
  }
  
  std::vector<ImVec2> points;
  std::string func_str(expr.text);
  const ImU32 color = IM_COL32(
    static_cast<int>(expr.color[0] * 255),
    static_cast<int>(expr.color[1] * 255),
    static_cast<int>(expr.color[2] * 255),
    static_cast<int>(expr.color[3] * 255)
  );
  
  bool plotted = false;

  // Check for parametric form: (f(t), g(t))
  if (!func_str.empty() && func_str.front() == '(' && func_str.back() == ')') {
    const std::string inner = func_str.substr(1, func_str.size() - 2);
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
        const double t_min = -10.0;
        const double t_max = 10.0;
        const double t_step = 0.02;

        for (t = t_min; t <= t_max; t += t_step) {
          const double vx = expr_fx.value();
          const double vy = expr_gx.value();

          ImVec2 screen_pos(origin.x + static_cast<float>(vx * m_zoom),
              origin.y - static_cast<float>(vy * m_zoom));
          points.push_back(screen_pos);
        }

        draw_list->AddPolyline(points.data(), points.size(), color,
            ImDrawFlags_None, lineThickness);
        plotted = true;
      }
    }
  }

  // Check for inequality
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
      const double x_min = -canvas_sz.x / (2 * m_zoom);
      const double x_max = canvas_sz.x / (2 * m_zoom);
      const double y_min = -canvas_sz.y / (2 * m_zoom);
      const double y_max = canvas_sz.y / (2 * m_zoom);
      
      const double step = std::max(0.025, 1.5 / m_zoom);
      const float dot_size = std::max(1.5f, m_zoom / 60.0f);
      
      for (y = y_min; y <= y_max; y += step) {
        for (x = x_min; x <= x_max; x += step) {
          if (expression.value() == 1.0) {
            ImVec2 screen_pos(origin.x + static_cast<float>(x * m_zoom),
                             origin.y - static_cast<float>(y * m_zoom));
            draw_list->AddCircleFilled(screen_pos, dot_size, color);
          }
        }
      }
      
      plotted = true;
    }
  }

  // Check for implicit form: f(x,y) = g(x,y)
  if (!plotted) {
    size_t equals_pos = findTopLevelEquals(func_str);
    bool has_double_equals = hasEqualsEqualsOperator(func_str);

    if (equals_pos != std::string::npos || has_double_equals) {
      std::string implicit_expr;

      if (has_double_equals) {
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
          std::string rhs = trim(temp_str.substr(eq_pos + 2));
          implicit_expr = "(" + lhs + ") - (" + rhs + ")";
        }
      } else {
        std::string lhs = trim(func_str.substr(0, equals_pos));
        std::string rhs = trim(func_str.substr(equals_pos + 1));
        implicit_expr = "(" + lhs + ") - (" + rhs + ")";
      }

      if (!implicit_expr.empty()) {
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
          const double x_min = -canvas_sz.x / (2 * m_zoom);
          const double x_max = canvas_sz.x / (2 * m_zoom);
          const double y_min = -canvas_sz.y / (2 * m_zoom);
          const double y_max = canvas_sz.y / (2 * m_zoom);
          const double step = std::max(0.008, 1.0 / m_zoom);
          
          const float dot_radius = 2.5f;
          
          // Horizontal scan
          for (y = y_min; y <= y_max; y += step) {
            double prev_val = 0.0;
            bool first = true;
            
            for (x = x_min; x <= x_max; x += step) {
              double curr_val = expression.value();
              
              if (!first && prev_val * curr_val < 0) {
                double t = prev_val / (prev_val - curr_val);
                double x_zero = (x - step) + t * step;
                double y_zero = y;
                
                ImVec2 screen_pos(origin.x + static_cast<float>(x_zero * m_zoom),
                                 origin.y - static_cast<float>(y_zero * m_zoom));
                draw_list->AddCircleFilled(screen_pos, dot_radius, color);
              }
              
              prev_val = curr_val;
              first = false;
            }
          }
          
          // Vertical scan
          for (x = x_min; x <= x_max; x += step) {
            double prev_val = 0.0;
            bool first = true;
            
            for (y = y_min; y <= y_max; y += step) {
              double curr_val = expression.value();
              
              if (!first && prev_val * curr_val < 0) {
                double t = prev_val / (prev_val - curr_val);
                double x_zero = x;
                double y_zero = (y - step) + t * step;
    
                ImVec2 screen_pos(origin.x + static_cast<float>(x_zero * m_zoom),
                                 origin.y - static_cast<float>(y_zero * m_zoom));
                draw_list->AddCircleFilled(screen_pos, dot_radius, color);
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

  // Check for polar or regular function
  if (!plotted) {
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

          ImVec2 screen_pos(origin.x + static_cast<float>(x * m_zoom),
              origin.y - static_cast<float>(y * m_zoom));
          points.push_back(screen_pos);
        }

        draw_list->AddPolyline(points.data(), points.size(), color,
            ImDrawFlags_None, lineThickness);
      }
    } else {
      // Regular y = f(x) function
      double x;

      exprtk::symbol_table<double> symbolTable;
      symbolTable.add_constants();
      addConstants(symbolTable);
      symbolTable.add_variable("x", x);

      exprtk::expression<double> expression;
      expression.register_symbol_table(symbolTable);

      exprtk::parser<double> parser;
      if (parser.compile(func_str, expression)) {
        for (x = -canvas_sz.x / (2 * m_zoom); x < canvas_sz.x / (2 * m_zoom); x += 0.05) {
          const double y = expression.value();

          ImVec2 screen_pos(origin.x + static_cast<float>(x * m_zoom), 
                           origin.y - static_cast<float>(y * m_zoom));
          points.push_back(screen_pos);
        }

        draw_list->AddPolyline(points.data(), points.size(), color,
            ImDrawFlags_None, lineThickness);
      }
    }
  }

  // Check for polar or regular function
  if (!plotted) {
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

          ImVec2 screen_pos(origin.x + static_cast<float>(x * m_zoom),
              origin.y - static_cast<float>(y * m_zoom));
          points.push_back(screen_pos);
        }

        draw_list->AddPolyline(points.data(), points.size(), color,
            ImDrawFlags_None, lineThickness);
      }
    } else {
      // Regular y = f(x) function
      double x;

      exprtk::symbol_table<double> symbolTable;
      symbolTable.add_constants();
      addConstants(symbolTable);
      symbolTable.add_variable("x", x);

      exprtk::expression<double> expression;
      expression.register_symbol_table(symbolTable);

      exprtk::parser<double> parser;
      if (parser.compile(func_str, expression)) {
        for (x = -canvas_sz.x / (2 * m_zoom); x < canvas_sz.x / (2 * m_zoom); x += 0.05) {
          const double y = expression.value();

          ImVec2 screen_pos(origin.x + static_cast<float>(x * m_zoom), 
                           origin.y - static_cast<float>(y * m_zoom));
          points.push_back(screen_pos);
        }

        draw_list->AddPolyline(points.data(), points.size(), color,
            ImDrawFlags_None, lineThickness);
      }
    }
  }
}

}  // namespace App
