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
#include "map.h"
#include <curl/curl.h>
#include "heatmap.h"
#include <iostream>
#include <thread>
#include <stb_image.h>

using namespace std;

static GLuint g_HeatmapTexture = 0;
double g_HeatmapBounds[4] = {0};
static bool g_HeatmapGenerated = false;
static bool g_HeatmapLoading = false;
static thread g_HeatmapThread;

Color GetRSRPColor(double rsrp);
double ComputeIDW(double lat, double lon, const vector<MeasurementPoint> &points);

void GenerateHeatmapAsync(const vector<MeasurementPoint> &measurements)
{
    g_HeatmapLoading = true;
    cout << "Запуск генерации тепловой карты...\n";

    GenerateHeatmap(measurements, "heatmap.png");

    g_HeatmapGenerated = true;
    g_HeatmapLoading = false;

    cout << "Генерация тепловой карты завершена.\n";
}

void LoadHeatmapTexture()
{
    if (g_HeatmapTexture != 0)
    {
        glDeleteTextures(1, &g_HeatmapTexture);
        g_HeatmapTexture = 0;
    }

    int width, height, channels;
    unsigned char *imageData = stbi_load("heatmap.png", &width, &height, &channels, 4);

    if (imageData)
    {
        glGenTextures(1, &g_HeatmapTexture);
        glBindTexture(GL_TEXTURE_2D, g_HeatmapTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, imageData);
        stbi_image_free(imageData);
        cout << "Heatmap texture loaded, id=" << g_HeatmapTexture << endl;
    }
    else
    {
        cout << "Failed to load heatmap.png" << endl;
    }
}

void run_gui(DeviceData *dev_data, mutex *mtx)
{
    curl_global_init(CURL_GLOBAL_DEFAULT);
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
    StartWorker();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");

    if (!g_Measurements.empty() && !g_HeatmapGenerated && !g_HeatmapLoading)
    {
        cout << "Запуск генерации тепловой карты в фоновом потоке..." << endl;
        g_HeatmapThread = thread(GenerateHeatmapAsync, g_Measurements);
    }

    bool showHeatmap = true;
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
        if (g_HeatmapGenerated && g_HeatmapTexture == 0)
        {
            LoadHeatmapTexture();
        }
        ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);

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

        ImGui::Begin("Map");
        if (g_HeatmapLoading)
        {
            ImGui::TextColored(ImVec4(1, 1, 0, 1), "Generating heat map... Please wait");
        }

        ImGui::Checkbox("Show Heatmap", &showHeatmap);
        ImGui::SameLine();

        if (ImGui::Button("Regenerate Heatmap") && !g_HeatmapLoading)
        {
            if (g_HeatmapThread.joinable())
                g_HeatmapThread.join();
            g_HeatmapGenerated = false;
            g_HeatmapTexture = 0;
            g_HeatmapThread = thread(GenerateHeatmapAsync, g_Measurements);
        }

        double centerX = current.lon;
        double centerY = LatToMercatorY(current.lat);

        if (ImPlot::BeginPlot("##Map", ImVec2(-1, -1), ImPlotFlags_Equal))
        {
            static bool firstFrame = true;
            if (firstFrame && (current.lat != 0 || current.lon != 0))
            {
                ImPlot::SetupAxisLimits(ImAxis_X1, centerX - 10.0, centerX + 10.0, ImGuiCond_Once);
                ImPlot::SetupAxisLimits(ImAxis_Y1, centerY - 10.0, centerY + 10.0, ImGuiCond_Once);
                firstFrame = false;
            }

            ImPlot::SetupAxis(ImAxis_X1, "Longitude");
            ImPlot::SetupAxis(ImAxis_Y1, "Latitude");

            RenderMap();
            if (showHeatmap && g_HeatmapTexture != 0)
            {
                ImPlotPoint minPoint{g_HeatmapBounds[0], LatToMercatorY(g_HeatmapBounds[2])};
                ImPlotPoint maxPoint{g_HeatmapBounds[1], LatToMercatorY(g_HeatmapBounds[3])};

                ImPlot::PlotImage("Heatmap",
                                  (ImTextureID)(intptr_t)g_HeatmapTexture,
                                  minPoint, maxPoint,
                                  ImVec2(0, 0), ImVec2(1, 1),
                                  ImVec4(1, 1, 1, 0.93f));
            }

            double x = current.lon;
            double y = LatToMercatorY(current.lat);
            ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 3, ImVec4(1, 0, 0, 1));
            ImPlot::PlotScatter("Device", &x, &y, 1);

            if (!g_Measurements.empty())
            {
                static std::vector<double> xs, ys;
                xs.clear();
                ys.clear();
                xs.reserve(g_Measurements.size());
                ys.reserve(g_Measurements.size());

                for (const auto &p : g_Measurements)
                {
                    xs.push_back(p.lon);
                    ys.push_back(LatToMercatorY(p.lat));
                }

                ImPlot::SetNextMarkerStyle(ImPlotMarker_Circle, 1.5f,
                                           ImVec4(0.7f, 0.7f, 0.7f, 0.65f));

                ImPlot::PlotScatter("Measurements", xs.data(), ys.data(), (int)xs.size());
            }
            ImPlot::EndPlot();
        }

        ImGui::End();
        ImGui::Render();
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(window);
    }
    if (g_HeatmapThread.joinable())
        g_HeatmapThread.join();

    if (g_HeatmapTexture != 0)
    {
        glDeleteTextures(1, &g_HeatmapTexture);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}
