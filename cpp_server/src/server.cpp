#include "server.h"
#include <zmq.hpp>
#include <fstream>
#include <iostream>
#include <thread>
#include <cstring>
#include <cstdio>
#include "database.h"
#include <SDL2/SDL.h>

using namespace std;

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