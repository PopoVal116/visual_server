#pragma once

#include <libpq-fe.h>

extern PGconn *conn;

inline const char *DB_HOST = "localhost";
inline const char *DB_PORT = "5432";
inline const char *DB_NAME = "visual_server_db";
inline const char *DB_USER = "postgres";
inline const char *DB_PASSWORD = "12345";

bool connect_to_database();
void save_cell_to_db(int pci, double rsrp, double rssi, double rsrq, double sinr,
                     double lat, double lon, double alt, double accuracy);