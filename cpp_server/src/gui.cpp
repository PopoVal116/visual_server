#include "gui.h"
#include "imgui.h"
#include "implot.h"
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"
#include <vector>
#include <algorithm>
#include <cmath>

using namespace std;

void run_gui(DeviceData *dev_data, mutex *mtx)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    server_start_time = (double)SDL_GetTicks() / 1000.0;

    SDL_Window *window = SDL_CreateWindow(
        "Location Server",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        900, 700,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);

    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    glewExperimental = GL_TRUE;
    glewInit();

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    bool running = true;
    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        ImGui::Begin("Phone Data");
        DeviceData current;
        {
            lock_guard<mutex> lock(*mtx);
            current = *dev_data;
        }
        ImGui::TextColored(ImVec4(1, 1, 0, 1), "Geolocation:");
        ImGui::Text("Latitude:   %.6f", current.lat);
        ImGui::Text("Longitude:  %.6f", current.lon);
        ImGui::Text("Altitude:   %.2f m", current.alt);
        ImGui::Text("Accuracy:   %.2f m", current.accuracy);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Network Info:");
        ImGui::TextWrapped("%s", current.cell_info.c_str());

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, 0.5, 0, 1), "TRAFFIC");
        ImGui::Text("TX bytes: %lld", current.tx_bytes);
        ImGui::Text("RX bytes: %lld", current.rx_bytes);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8, 0.8, 0.2, 1), "TOP APPS");
        ImGui::TextWrapped("%s", current.top_apps.c_str());
        ImGui::End();

        ImGui::Begin("Signal Strength");

        deque<SignalPoint> history_copy;
        {
            lock_guard<mutex> lock(*mtx);
            history_copy = signal_history;
        }

        if (history_copy.empty())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Нет данных сигнала");
        }
        else
        {
            vector<int> unique_pcis;
            for (const auto &p : history_copy)
            {
                if (p.pci != 0 &&
                    find(unique_pcis.begin(), unique_pcis.end(), p.pci) == unique_pcis.end())
                {
                    unique_pcis.push_back(p.pci);
                }
            }

            vector<double> t;
            for (const auto &p : history_copy)
                t.push_back(p.time);

            if (!t.empty())
            {
                double t_now = t.back();

                if (ImPlot::BeginPlot("RSRP", ImVec2(-1, 180)))
                {
                    ImPlot::SetupAxes("Time (s)", "RSRP (dBm)");
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -140, -40, ImGuiCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_X1, t_now - 60.0, t_now, ImGuiCond_Once);

                    for (int pci : unique_pcis)
                    {
                        vector<double> pci_t, pci_vals;
                        for (size_t i = 0; i < history_copy.size(); ++i)
                        {
                            if (history_copy[i].pci == pci)
                            {
                                pci_t.push_back(t[i]);
                                pci_vals.push_back(history_copy[i].rsrp);
                            }
                        }
                        if (!pci_t.empty())
                        {
                            ImVec4 color = ImVec4(
                                fmod(pci * 0.31f, 1.0f),
                                fmod(pci * 0.73f, 1.0f),
                                fmod(pci * 0.59f, 1.0f),
                                1.0f);
                            ImPlot::SetNextLineStyle(color, 2.5f);
                            ImPlot::PlotLine(("PCI " + to_string(pci)).c_str(),
                                             pci_t.data(), pci_vals.data(), (int)pci_t.size());
                        }
                    }
                    ImPlot::EndPlot();
                }

                if (ImPlot::BeginPlot("RSSI", ImVec2(-1, 180)))
                {
                    ImPlot::SetupAxes("Time (s)", "RSSI (dBm)");
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -120, -40, ImGuiCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_X1, t_now - 60.0, t_now, ImGuiCond_Once);

                    for (int pci : unique_pcis)
                    {
                        vector<double> pci_t, pci_vals;
                        for (size_t i = 0; i < history_copy.size(); ++i)
                        {
                            if (history_copy[i].pci == pci)
                            {
                                pci_t.push_back(t[i]);
                                pci_vals.push_back(history_copy[i].rssi);
                            }
                        }
                        if (!pci_t.empty())
                        {
                            ImVec4 color = ImVec4(
                                fmod(pci * 0.41f, 1.0f),
                                fmod(pci * 0.63f, 1.0f),
                                fmod(pci * 0.79f, 1.0f),
                                1.0f);
                            ImPlot::SetNextLineStyle(color, 2.5f);
                            ImPlot::PlotLine(("PCI " + to_string(pci)).c_str(),
                                             pci_t.data(), pci_vals.data(), (int)pci_t.size());
                        }
                    }
                    ImPlot::EndPlot();
                }

                if (ImPlot::BeginPlot("RSRQ", ImVec2(-1, 180)))
                {
                    ImPlot::SetupAxes("Time (s)", "RSRQ (dB)");
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -30, 0, ImGuiCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_X1, t_now - 60.0, t_now, ImGuiCond_Once);

                    for (int pci : unique_pcis)
                    {
                        vector<double> pci_t, pci_vals;
                        for (size_t i = 0; i < history_copy.size(); ++i)
                        {
                            if (history_copy[i].pci == pci)
                            {
                                pci_t.push_back(t[i]);
                                pci_vals.push_back(history_copy[i].rsrq);
                            }
                        }
                        if (!pci_t.empty())
                        {
                            ImVec4 color = ImVec4(
                                fmod(pci * 0.27f, 1.0f),
                                fmod(pci * 0.85f, 1.0f),
                                fmod(pci * 0.45f, 1.0f),
                                1.0f);
                            ImPlot::SetNextLineStyle(color, 2.5f);
                            ImPlot::PlotLine(("PCI " + to_string(pci)).c_str(),
                                             pci_t.data(), pci_vals.data(), (int)pci_t.size());
                        }
                    }
                    ImPlot::EndPlot();
                }

                if (ImPlot::BeginPlot("SINR", ImVec2(-1, 180)))
                {
                    ImPlot::SetupAxes("Time (s)", "SINR (dB)");
                    ImPlot::SetupAxisLimits(ImAxis_Y1, -10, 30, ImGuiCond_Once);
                    ImPlot::SetupAxisLimits(ImAxis_X1, t_now - 60.0, t_now, ImGuiCond_Once);

                    for (int pci : unique_pcis)
                    {
                        vector<double> pci_t, pci_vals;
                        for (size_t i = 0; i < history_copy.size(); ++i)
                        {
                            if (history_copy[i].pci == pci)
                            {
                                pci_t.push_back(t[i]);
                                pci_vals.push_back(history_copy[i].sinr);
                            }
                        }
                        if (!pci_t.empty())
                        {
                            ImVec4 color = ImVec4(
                                fmod(pci * 0.19f, 1.0f),
                                fmod(pci * 0.55f, 1.0f),
                                fmod(pci * 0.91f, 1.0f),
                                1.0f);
                            ImPlot::SetNextLineStyle(color, 2.5f);
                            ImPlot::PlotLine(("PCI " + to_string(pci)).c_str(),
                                             pci_t.data(), pci_vals.data(), (int)pci_t.size());
                        }
                    }
                    ImPlot::EndPlot();
                }
            }
        }

        ImGui::End();
        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
