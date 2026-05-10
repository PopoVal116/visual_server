#pragma once

#include <vector>
#include <string>
#include <deque>

extern double server_start_time;
constexpr size_t MAX_HISTORY = 300;
extern int g_HeatmapMetric;

struct DeviceData
{
    double lat = 0.0, lon = 0.0, alt = 0.0, time = 0.0, accuracy = 0.0;
    std::string cell_info = "нет данных";
    long long tx_bytes = 0, rx_bytes = 0;
    std::string top_apps = "недоступно";
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

struct MeasurementPoint
{
    double lat = 0.0;
    double lon = 0.0;
    double altitude = 0.0;

    double rsrp = -140.0;
    double rsrq = -30.0;
    double rssi = -140.0;
    double sinr = 0.0;

    int earfcn = 0;
    int pci = 0;
    long long timestamp = 0;
};

extern std::vector<MeasurementPoint> g_Measurements;

extern std::deque<SignalPoint> signal_history;

std::vector<std::string> split_string(const std::string &s, char delimiter);
extern double g_HeatmapBounds[4];