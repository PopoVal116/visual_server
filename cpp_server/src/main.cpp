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

using namespace std;

struct location
{
    float lat = 0;
    float lon = 0;
    float alt = 0;
    float time = 0;
    location() : lat(0), lon(0), alt(0), time(0) {}
};

void zmq_server(location *loc, mutex *mtx)
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
        socket.bind("tcp://0.0.0.0:5555");
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
            float lat, lon, alt, t;
            if (sscanf(data.c_str(), "%f,%f,%f,%f", &lat, &lon, &alt, &t) == 4)
            {
                lock_guard<mutex> lock(*mtx);
                loc->lat = lat;
                loc->lon = lon;
                loc->alt = alt;
                loc->time = t;
            }
            F << "  {\"id\":" << c << ",\"data\":\"" << data << "\"},\n";
            F.flush();
            string reply = "OK:" + to_string(c);
            zmq::message_t zmq_reply(reply.size());
            memcpy(zmq_reply.data(), reply.c_str(), reply.size());
            socket.send(zmq::buffer(reply), zmq::send_flags::none);
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
void run_gui(location *loc, mutex *mtx)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    SDL_Window *window = SDL_CreateWindow(
        "Location Server", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        800, 600, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
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

        ImGui::Begin("Location");
        location current_loc;
        {
            lock_guard<mutex> lock(*mtx);
            current_loc = *loc;
        }
        ImGui::Text("Latitude: %.6f", current_loc.lat);
        ImGui::Text("Longitude: %.6f", current_loc.lon);
        ImGui::Text("Altitude: %.2f", current_loc.alt);
        ImGui::Text("Time: %.0f", current_loc.time);
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

    location loc;
    mutex mtx;

    thread server_thread(zmq_server, &loc, &mtx);
    thread gui_thread(run_gui, &loc, &mtx);
    gui_thread.join();
    server_thread.join();

    return 0;
}
