#include <iostream>

#include "imgui.h"
#include "implot.h"
#include <fstream>
#include <zmq.hpp>
#include <thread>
#include <mutex>
#include <GL/glew.h>
#include <SDL2/SDL.h>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include <sstream>
#include <vector>
#include <string>
#include <deque>
#include <libpq-fe.h>

using namespace std;
double server_start_time = 0.0;
PGconn *conn;
const char *DB_HOST = "localhost";
const char *DB_PORT = "5432";
const char *DB_NAME = "visual_server_db";
const char *DB_USER = "postgres";
const char *DB_PASSWORD = "12345";

vector<string> split_string(const string &s, char delimiter)
{
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

struct DeviceData
{
    double lat = 0, lon = 0, alt = 0, time = 0, accuracy = 0;
    string cell_info = "нет данных";
    long long tx_bytes = 0, rx_bytes = 0;
    string top_apps = "недоступно";
};

struct SignalPoint
{
    double time = 0.0;
    int pci = 0;
    double rsrp = 0.0;
    double rssi = 0.0;
    double rsrq = 0.0;
    double sinr = 0.0;
};

deque<SignalPoint> signal_history;
constexpr size_t MAX_HISTORY = 300;

void save_cell_to_db(int pci, double rsrp, double rssi, double rsrq, double sinr,
                     double lat, double lon, double alt, double accuracy)
{
    if (!conn || PQstatus(conn) != CONNECTION_OK)
        return;

    const char *paramValues[9];
    char pci_str[16], rsrp_str[32], rssi_str[32], rsrq_str[32], sinr_str[32];
    char lat_str[32], lon_str[32], alt_str[32], acc_str[32];

    snprintf(pci_str, sizeof(pci_str), "%d", pci);
    snprintf(rsrp_str, sizeof(rsrp_str), "%.2f", rsrp);
    snprintf(rssi_str, sizeof(rssi_str), "%.2f", rssi);
    snprintf(rsrq_str, sizeof(rsrq_str), "%.2f", rsrq);
    snprintf(sinr_str, sizeof(sinr_str), "%.2f", sinr);
    snprintf(lat_str, sizeof(lat_str), "%.6f", lat);
    snprintf(lon_str, sizeof(lon_str), "%.6f", lon);
    snprintf(alt_str, sizeof(alt_str), "%.2f", alt);
    snprintf(acc_str, sizeof(acc_str), "%.2f", accuracy);

    paramValues[0] = pci_str;
    paramValues[1] = rsrp_str;
    paramValues[2] = rssi_str;
    paramValues[3] = rsrq_str;
    paramValues[4] = sinr_str;
    paramValues[5] = lat_str;
    paramValues[6] = lon_str;
    paramValues[7] = alt_str;
    paramValues[8] = acc_str;

    const char *query = "INSERT INTO cell_measurements "
                        "(pci, rsrp, rssi, rsrq, sinr, lat, lon, alt, accuracy) "
                        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)";

    PGresult *res = PQexecParams(conn, query, 9, NULL, paramValues, NULL, NULL, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK)
    {
        cerr << "Ошибка INSERT: " << PQerrorMessage(conn) << endl;
    }
    else
    {
        cout << "Сохранено в БД: PCI=" << pci << " RSRP=" << rsrp << " RSSI=" << rssi << " RSRQ=" << rsrq << " SINR=" << sinr << endl;

        PQclear(res);
    }
}

void zmq_server(DeviceData *dev_data, mutex *mtx)
{
    ofstream F("/tmp/vsserv.json");
    if (!F)
    {
        cout << "Ошибка открытия файла" << endl;
        return;
    }
    F << "[\n";

    try
    {
        zmq::context_t context(1);
        zmq::socket_t socket(context, ZMQ_REP);
        socket.bind("tcp://*:5555");
        int c = 0;
        cout << "Сервер запущен" << endl;

        while (true)
        {
            zmq::message_t request;
            if (!socket.recv(request, zmq::recv_flags::none))
                continue;

            c++;
            string data(static_cast<char *>(request.data()), request.size());
            cout << "Пакет #" << c << ": " << data << endl;
            double lat = 0, lon = 0, alt = 0, time = 0, accuracy = 0;
            long long tx = 0, rx = 0;
            char cell_buf[4096] = {0};
            char top_buf[2048] = {0};

            size_t loc_pos = data.find("LOC:");
            if (loc_pos != string::npos)
                sscanf(data.c_str() + loc_pos, "LOC:%lf,%lf,%lf,%lf,%lf;", &lat, &lon, &alt, &time, &accuracy);

            size_t traf_pos = data.find("TRAFFIC:");
            if (traf_pos != string::npos)
                sscanf(data.c_str() + traf_pos + 8, "%lld,%lld", &tx, &rx);

            size_t top_pos = data.find("TOP_APPS:");
            if (top_pos != string::npos)
            {
                size_t start = top_pos + 9;
                size_t end = data.find(";", start);
                if (end == string::npos)
                    end = data.size();
                string top_str = data.substr(start, end - start);
                strncpy(top_buf, top_str.c_str(), sizeof(top_buf) - 1);
                top_buf[sizeof(top_buf) - 1] = '\0';
            }

            size_t cell_pos = data.find("CELL:");
            if (cell_pos != string::npos)
            {
                size_t cell_end = data.find("TRAFFIC:", cell_pos);
                if (cell_end == string::npos)
                    cell_end = data.size();

                string cell_str = data.substr(cell_pos + 5, cell_end - cell_pos - 5);
                strncpy(cell_buf, cell_str.c_str(), sizeof(cell_buf) - 1);
                cell_buf[sizeof(cell_buf) - 1] = '\0';
                vector<string> cell_blocks = split_string(cell_str, '#');

                int added_count = 0;

                for (const auto &block : cell_blocks)
                {
                    if (added_count >= 3)
                        break;

                    if (block.find("PCI=") == string::npos)
                        continue;

                    int pci = -1;
                    double rsrp = 0.0, rssi = 0.0, rsrq = 0.0, sinr = 0.0;

                    sscanf(block.c_str() + block.find("PCI="), "PCI=%d", &pci);
                    if (pci <= 0)
                        continue;

                    if (block.find("RSRP=") != string::npos)
                        sscanf(block.c_str() + block.find("RSRP="), "RSRP=%lf", &rsrp);
                    if (block.find("RSRQ=") != string::npos)
                        sscanf(block.c_str() + block.find("RSRQ="), "RSRQ=%lf", &rsrq);
                    if (block.find("RSSI=") != string::npos)
                        sscanf(block.c_str() + block.find("RSSI="), "RSSI=%lf", &rssi);
                    if (block.find("RSSNR=") != string::npos)
                        sscanf(block.c_str() + block.find("RSSNR="), "RSSNR=%lf", &sinr);

                    if (rsrp != 0 || rsrq != 0)
                    {
                        save_cell_to_db(pci, rsrp, rssi, rsrq, sinr, lat, lon, alt, accuracy);

                        {
                            lock_guard<mutex> lock(*mtx);
                            double current_time = ((double)SDL_GetTicks() / 1000.0) - server_start_time;
                            signal_history.push_back({current_time, pci, rsrp, rssi, rsrq, sinr});

                            while (signal_history.size() > MAX_HISTORY)
                                signal_history.pop_front();
                        }

                        cout << "Добавлена точка: PCI=" << pci
                             << " RSRP=" << rsrp << " RSRQ=" << rsrq << endl;

                        added_count++;
                    }
                }
            }

            {
                lock_guard<mutex> lock(*mtx);
                dev_data->lat = lat;
                dev_data->lon = lon;
                dev_data->alt = alt;
                dev_data->time = time;
                dev_data->accuracy = accuracy;
                dev_data->tx_bytes = tx;
                dev_data->rx_bytes = rx;
                dev_data->cell_info = cell_buf;
                dev_data->top_apps = top_buf;
            }

            F << "  {\"id\":" << c << ","
              << "\"raw\":\"" << data << "\","
              << "\"lat\":" << lat << ","
              << "\"lon\":" << lon << ","
              << "\"alt\":" << alt << ","
              << "\"time\":" << time << ","
              << "\"accuracy\":" << accuracy << ","
              << "\"cell\":\"" << cell_buf << "\","
              << "\"tx_bytes\":" << tx << ","
              << "\"rx_bytes\":" << rx << ","
              << "\"top_apps\":\"" << top_buf << "\""
              << "},\n";

            F.flush();

            string reply = "OK:" + to_string(c);
            socket.send(zmq::buffer(reply), zmq::send_flags::none);
        }
    }
    catch (const exception &e)
    {
        cerr << "Ошибка: " << e.what() << endl;
    }

    F << "]";
    F.close();
}

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

bool connect_to_database()
{
    string conninfo = "host=" + string(DB_HOST) + " port=" + string(DB_PORT) + " dbname=" + string(DB_NAME) + " user=" + string(DB_USER) + " password=" + string(DB_PASSWORD);

    conn = PQconnectdb(conninfo.c_str());

    if (PQstatus(conn) != CONNECTION_OK)
    {
        cerr << "Ошибка подключения к БД: " << PQerrorMessage(conn) << endl;
        PQfinish(conn);
        return false;
    }

    cout << "Успешно подключено к БД!" << endl;
    return true;
}

int main(int argc, char *argv[])
{
    cout << "Запуск сервера..." << endl;
    if (!connect_to_database())
    {
        return 1;
    }
    DeviceData dev_data;
    mutex mtx;

    thread server_thread(zmq_server, &dev_data, &mtx);
    thread gui_thread(run_gui, &dev_data, &mtx);
    gui_thread.join();
    server_thread.join();

    PQfinish(conn);
    return 0;
}