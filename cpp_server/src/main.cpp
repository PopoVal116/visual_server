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
#include <iostream>
#include <sstream>
#include <vector>
#include <string>


using namespace std;

/*
struct location
{
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float time = 0;
    location() : lat(0), lon(0), alt(0), time(0) {}
};*/

struct DeviceData {
    double lat = 0, lon = 0, alt = 0, time = 0, accuracy = 0;
    string cell_info = "нет данных";
    long long tx_bytes = 0, rx_bytes = 0;
    string top_apps = "недоступно";
};

void zmq_server(/*location *loc*/ DeviceData *dev_data, mutex *mtx)
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
            zmq::recv_result_t result = socket.recv(request, zmq::recv_flags::none);
            if (!result)
            {
                cout << "Ошибка при получении данных" << endl;
                continue;
            }
            c++;
            string data(static_cast<char *>(request.data()), request.size());
            cout << "Пакет #" << c << ": " << data << endl;
            /*
            float lat, lon, alt, t;
            if (sscanf(data.c_str(), "%f,%f,%f,%f", &lat, &lon, &alt, &t) == 4)
            {
                lock_guard<mutex> lock(*mtx);
                loc->lat = lat;
                loc->lon = lon;
                loc->alt = alt;
                loc->time = t;
            }
            */
            double lat = 0, lon = 0, alt = 0, time = 0, accuracy = 0;
            char cell_buf[4096] = {0};
            long long tx = 0, rx = 0;
            char top_buf[2048] = {0};

            size_t loc_pos = data.find("LOC:");
            if (loc_pos != string::npos) { 
                int loc_parsed = sscanf(data.c_str() + loc_pos, "LOC:%lf,%lf,%lf,%lf,%lf;",
                            &lat, &lon, &alt, &time, &accuracy);
                if (loc_parsed == 5)
            {size_t cell_pos = data.find("CELL:");
                size_t traf_pos = data.find("TRAFFIC:");
                size_t top_pos  = data.find("TOP_APPS:");

                if (cell_pos != string::npos)
                {size_t start = cell_pos + 5;
                    size_t end = data.find(";", start);
                    if (end == string::npos) end = data.size();
                    string cell_str = data.substr(start, end - start);
                    strncpy(cell_buf, cell_str.c_str(), sizeof(cell_buf)-1);
                    cell_buf[sizeof(cell_buf)-1] = '\0';
                }

                if (traf_pos != string::npos)
                {
                    sscanf(data.c_str() + traf_pos + 8, "%lld,%lld", &tx, &rx);
                }

                if (top_pos != string::npos)
                {
                    size_t start = top_pos + 9;
                    size_t end = data.find(";", start);
                    if (end == string::npos) end = data.size();
                    string top_str = data.substr(start, end - start);
                    strncpy(top_buf, data.c_str() + start, sizeof(top_buf)-1);
                    top_buf[sizeof(top_buf)-1] = '\0';
                }

                lock_guard<mutex> lock(*mtx);
                dev_data->lat = lat;
                dev_data->lon = lon;
                dev_data->alt = alt;
                dev_data->time = time;
                dev_data->accuracy = accuracy;
                dev_data->cell_info = cell_buf;
                dev_data->tx_bytes = tx;
                dev_data->rx_bytes = rx;
                dev_data->top_apps = top_buf;
            }
            /*
            F << "  {\"id\":" << c << ",\"data\":\"" << data << "\"},\n";
            */
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
            zmq::message_t zmq_reply(reply.size());
            memcpy(zmq_reply.data(), reply.c_str(), reply.size());
            socket.send(zmq::buffer(reply), zmq::send_flags::none);
        }
    }
    }
    catch (const zmq::error_t &e)
    {
        cerr << "ZMQ ошибка: " << e.what() << endl;
    }
    catch (const exception &e)
    {
        cerr << "Ошибка: " << e.what() << endl;
    }
    F << "]";
    F.close();
}

vector<string> split_string(const string& s, char delimiter) {
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

void run_gui(/*location *loc*/ DeviceData *dev_data, mutex *mtx)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *window = SDL_CreateWindow(
        "Location Server", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        900, 700, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    SDL_GLContext gl_context = SDL_GL_CreateContext(window);

    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Включить Keyboard Controls

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
            {
                running = false;
            }
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

        ImGui::TextColored(ImVec4(1,1,0,1), "Geolocation:");
        ImGui::Text("Latitude:   %.6f", current.lat);
        ImGui::Text("Longitude:  %.6f", current.lon);
        ImGui::Text("Altitude:   %.2f m", current.alt);
        ImGui::Text("Time:       %.0f ms", current.time);
        ImGui::Text("Accuracy:   %.2f m", current.accuracy);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0,1,1,1), "Network Info:");
        ImGui::TextWrapped("%s", current.cell_info.c_str());

        ImGui::Separator();
        ImGui::TextColored(ImVec4(1,0.5,0,1), "TRAFFIC");
        ImGui::Text("TX bytes: %lld", current.tx_bytes);
        ImGui::Text("RX bytes: %lld", current.rx_bytes);

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.8,0.8,0.2,1), "TOP APPS");
        ImGui::TextWrapped("%s", current.top_apps.c_str());

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

int main(int argc, char *argv[])
{
    cout << "Запуск сервера..." << endl;

    //location loc;
    DeviceData dev_data;
    mutex mtx;

    thread server_thread(zmq_server, &dev_data, &mtx);
    thread gui_thread(run_gui, &dev_data, &mtx);
    gui_thread.join();
    server_thread.join();

    return 0;
}