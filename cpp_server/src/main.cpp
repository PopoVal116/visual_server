#include <iostream>
#include <thread>
#include <mutex>
#include "database.h"
#include "server.h"
#include "gui.h"
#include "common.h"
#include <curl/curl.h>

using namespace std;

int main()
{
    cout << "Запуск сервера..." << endl;

    if (!connect_to_database())
    {
        cerr << "Не удалось подключиться к базе данных. Выход." << endl;
        return 1;
    }

    DeviceData dev_data;
    mutex mtx;

    thread server_thread(zmq_server, &dev_data, &mtx);
    thread gui_thread(run_gui, &dev_data, &mtx);

    gui_thread.join();
    server_thread.join();

    if (conn)
        PQfinish(conn);

    curl_global_cleanup();

    cout << "Сервер завершил работу." << endl;
    return 0;
}