#include "heatmap.h"
#include <cmath>
#include <algorithm>
#include <vector>
#include <iostream>
#include <stb_image.h>
#include "stb_image_write.h"
#include <GL/glew.h>
#include "map.h"

using namespace std;

double GetMeasurementValue(const MeasurementPoint &p, int metric)
{
    switch (metric)
    {
    case 0:
        return p.rsrp;
    case 1:
        return p.rsrq;
    case 2:
        return p.rssi;
    case 3:
        return p.altitude;
    default:
        return p.rsrp;
    }
}

Color GetSignalColor(double value, int metric)
{
    if (metric == 0)
    {
        if (value >= -80)
            return {0, 255, 0, 235};
        if (value >= -90)
            return {255, 255, 0, 235};
        if (value >= -100)
            return {255, 165, 0, 235};
        if (value > -115)
            return {255, 0, 0, 235};
        return {100, 100, 100, 100};
    }
    else if (metric == 1)
    {
        if (value >= -10)
            return {0, 255, 0, 235};
        if (value >= -15)
            return {255, 255, 0, 235};
        if (value >= -20)
            return {255, 165, 0, 235};
        if (value >= -25)
            return {255, 0, 0, 235};
        return {100, 100, 100, 100};
    }
    else if (metric == 2)
    {
        if (value >= -65)
            return {0, 255, 0, 235};
        if (value >= -75)
            return {255, 255, 0, 235};
        if (value >= -85)
            return {255, 165, 0, 235};
        if (value >= -95)
            return {255, 0, 0, 235};
        return {100, 100, 100, 100};
    }
    else
    {
        if (value < 0 || value > 3000)
            return {100, 100, 100, 100};

        if (value <= 200)
            return {0, 180, 0, 230};
        if (value <= 500)
            return {80, 255, 80, 230};
        if (value <= 800)
            return {255, 255, 0, 230};
        if (value <= 1200)
            return {205, 130, 0, 235};
        return {139, 69, 19, 240};
    }
}

void GenerateHeatmap(const vector<MeasurementPoint> &points,
                     const string &outputFile,
                     int metric)
{
    if (points.empty())
        return;

    int width = 1000;
    int height = 1000;

    vector<unsigned char> image(width * height * 4, 0);

    double minLat = 999, maxLat = -999, minLon = 999, maxLon = -999;
    for (const auto &p : points)
    {
        minLat = min(minLat, p.lat);
        maxLat = max(maxLat, p.lat);
        minLon = min(minLon, p.lon);
        maxLon = max(maxLon, p.lon);
    }

    double latMargin = (maxLat - minLat) * 0.05;
    double lonMargin = (maxLon - minLon) * 0.05;

    minLat -= latMargin;
    maxLat += latMargin;
    minLon -= lonMargin;
    maxLon += lonMargin;

    extern double g_HeatmapBounds[4];
    g_HeatmapBounds[0] = minLon;
    g_HeatmapBounds[1] = maxLon;
    g_HeatmapBounds[2] = minLat;
    g_HeatmapBounds[3] = maxLat;

    const double power = 2.4;
    const double maxDistDeg = 0.0022;

    cout << "Генерация тепловой карты (Metric = " << metric << ")\n";

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            double lon = minLon + (x / (double)width) * (maxLon - minLon);
            double mercMin = LatToMercatorY(minLat);
            double mercMax = LatToMercatorY(maxLat);

            double mercY =
                mercMax - (y / (double)(height - 1)) * (mercMax - mercMin);

            double lat =
                atan(sinh(mercY * M_PI / 180.0)) * 180.0 / M_PI;

            double weightedSum = 0.0;
            double weightTotal = 0.0;

            for (const auto &p : points)
            {
                double dx = lat - p.lat;
                double dy = lon - p.lon;
                double dist = sqrt(dx * dx + dy * dy);

                if (dist > maxDistDeg)
                    continue;

                double val = GetMeasurementValue(p, metric);
                double weight = 1.0 / pow(dist + 0.00001, power);

                weightedSum += val * weight;
                weightTotal += weight;
            }

            double value = (weightTotal > 1e-8) ? weightedSum / weightTotal : -140.0;
            Color c = GetSignalColor(value, metric);

            int idx = (y * width + x) * 4;
            image[idx + 0] = c.r;
            image[idx + 1] = c.g;
            image[idx + 2] = c.b;
            image[idx + 3] = c.a;
        }
    }

    stbi_write_png(outputFile.c_str(), width, height, 4, image.data(), width * 4);
}
