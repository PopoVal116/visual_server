#include "database.h"
#include <iostream>
#include <cstdio>

PGconn *conn = nullptr;
using namespace std;

bool connect_to_database()
{
    string conninfo = "host=" + string(DB_HOST) + " port=" + string(DB_PORT) +
                      " dbname=" + string(DB_NAME) + " user=" + string(DB_USER) +
                      " password=" + string(DB_PASSWORD);

    conn = PQconnectdb(conninfo.c_str());

    if (PQstatus(conn) != CONNECTION_OK)
    {
        cerr << "Ошибка подключения к БД: " << PQerrorMessage(conn) << endl;
        PQfinish(conn);
        conn = nullptr;
        return false;
    }

    cout << "Успешно подключено к БД!" << endl;
    return true;
}

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
        std::cerr << "Ошибка INSERT: " << PQerrorMessage(conn) << std::endl;
    }
    else
    {
        std::cout << "Сохранено в БД: PCI=" << pci
                  << " RSRP=" << rsrp << " RSSI=" << rssi
                  << " RSRQ=" << rsrq << " SINR=" << sinr << std::endl;
        PQclear(res);
    }
}