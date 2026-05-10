#pragma once
#include <vector>
#include <string>
#include "data_loader.h"
struct Color
{
    int r, g, b, a;
};
Color GetSignalColor(double value, int metric);
double GetMeasurementValue(const MeasurementPoint &p, int metric);
void GenerateHeatmap(const std::vector<MeasurementPoint> &points,
                     const std::string &outputFile,
                     int metric = 0);
