#pragma once
#include <vector>
#include <string>
#include "data_loader.h"
struct Color
{
    int r, g, b, a;
};
Color GetRSRPColor(double rsrp);
double ComputeIDW(double lat, double lon, const std::vector<MeasurementPoint> &points);
void GenerateHeatmap(const std::vector<MeasurementPoint> &points, const std::string &outputFile);
unsigned int GenerateHeatmapTexture(const std::vector<MeasurementPoint> &points, int width = 512, int height = 512);