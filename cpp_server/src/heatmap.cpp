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

Color gradientColor(Color c1, Color c2, double ratio)
{
    return {
        static_cast<int>(c1.r + (c2.r - c1.r) * ratio),
        static_cast<int>(c1.g + (c2.g - c1.g) * ratio),
        static_cast<int>(c1.b + (c2.b - c1.b) * ratio),
        static_cast<int>(c1.a + (c2.a - c1.a) * ratio)};
}

Color GetRSRPColor(double rsrp)
{
    if (rsrp >= -80)
        return {0, 255, 0, 180};

    if (rsrp >= -90)
        return {255, 255, 0, 180};

    if (rsrp >= -100)
        return {255, 165, 0, 180};

    if (rsrp > -115)
        return {255, 0, 0, 180};

    return {100, 100, 100, 100};
}

double ComputeIDW(
    double lat,
    double lon,
    const vector<MeasurementPoint> &points)
{
    double weightedSum = 0.0;
    double weightTotal = 0.0;

    const double power = 2.0;
    const double maxDistanceDeg = 0.0003;

    for (const auto &p : points)
    {
        double dx = lat - p.lat;
        double dy = lon - p.lon;

        double dist = sqrt(dx * dx + dy * dy);

        if (dist > maxDistanceDeg)
            continue;

        if (dist < 0.00001)
            dist = 0.00001;

        double value = p.rsrp;

        if (value < -120 || value > -40)
            continue;
        double weight = 1.0 / pow(dist, power);

        weightedSum += value * weight;
        weightTotal += weight;
    }

    if (weightTotal == 0.0)
        return -140;

    return weightedSum / weightTotal;
}

void GenerateHeatmap(const vector<MeasurementPoint> &points, const string &outputFile)
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

    double latMargin = (maxLat - minLat) * 0.38;
    double lonMargin = (maxLon - minLon) * 0.38;

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

    cout << "Генерация тепловой карты...\n";

    for (int y = 0; y < height; y++)
    {
        if (y % 100 == 0)
            cout << "Progress: " << (y * 100 / height) << "%\n";

        for (int x = 0; x < width; x++)
        {
            double lon = minLon + (x / (double)width) * (maxLon - minLon);
            double lat = maxLat - (y / (double)height) * (maxLat - minLat);

            double weightedSum = 0.0;
            double weightTotal = 0.0;

            for (const auto &p : points)
            {
                double dx = lat - p.lat;
                double dy = lon - p.lon;
                double dist = sqrt(dx * dx + dy * dy);

                if (dist > maxDistDeg)
                    continue;

                double weight = 1.0 / pow(dist + 0.00001, power);
                weightedSum += p.rsrp * weight;
                weightTotal += weight;
            }

            double rsrp = (weightTotal > 1e-8) ? weightedSum / weightTotal : -140.0;

            Color c = GetRSRPColor(rsrp);

            if (rsrp > -82)
                c.a = 255;
            else if (rsrp > -88)
                c.a = 252;
            else if (rsrp > -94)
                c.a = 240;
            else if (rsrp > -100)
                c.a = 215;
            else if (rsrp > -107)
                c.a = 175;
            else if (rsrp > -114)
                c.a = 130;
            else
                c.a = 70;

            int idx = (y * width + x) * 4;
            image[idx + 0] = c.r;
            image[idx + 1] = c.g;
            image[idx + 2] = c.b;
            image[idx + 3] = c.a;
        }
    }

    auto lightBlur = [&](const vector<unsigned char> &src)
    {
        vector<unsigned char> dst(width * height * 4, 0);
        for (int py = 1; py < height - 1; py++)
        {
            for (int px = 1; px < width - 1; px++)
            {
                int idx = (py * width + px) * 4;
                for (int c = 0; c < 4; c++)
                {
                    int s = src[((py - 1) * width + (px - 1)) * 4 + c] * 1 +
                            src[((py - 1) * width + px) * 4 + c] * 2 +
                            src[((py - 1) * width + (px + 1)) * 4 + c] * 1 +
                            src[(py * width + (px - 1)) * 4 + c] * 2 +
                            src[idx + c] * 4 +
                            src[(py * width + (px + 1)) * 4 + c] * 2 +
                            src[((py + 1) * width + (px - 1)) * 4 + c] * 1 +
                            src[((py + 1) * width + px) * 4 + c] * 2 +
                            src[((py + 1) * width + (px + 1)) * 4 + c] * 1;
                    dst[idx + c] = s / 16;
                }
            }
        }
        return dst;
    };

    auto blurred = lightBlur(image);

    stbi_write_png(outputFile.c_str(), width, height, 4, blurred.data(), width * 4);
    cout << "Heatmap успешно сохранена!\n";
}

unsigned int
GenerateHeatmapTexture(
    const vector<MeasurementPoint> &points,
    int width, int height)
{
    if (points.empty())
        return 0;
    double minLat = 999, maxLat = -999;
    double minLon = 999, maxLon = -999;

    for (const auto &p : points)
    {
        minLat = min(minLat, p.lat);
        maxLat = max(maxLat, p.lat);
        minLon = min(minLon, p.lon);
        maxLon = max(maxLon, p.lon);
    }

    double latMargin = (maxLat - minLat) * 0.1;
    double lonMargin = (maxLon - minLon) * 0.1;
    minLat -= latMargin;
    maxLat += latMargin;
    minLon -= lonMargin;
    maxLon += lonMargin;

    vector<unsigned char> image(width * height * 4);

    for (int y = 0; y < height; y++)
    {
        for (int x = 0; x < width; x++)
        {
            double lon = minLon + (x / (double)width) * (maxLon - minLon);
            double lat = maxLat - (y / (double)height) * (maxLat - minLat);

            double rsrp = ComputeIDW(lat, lon, points);
            Color c = GetRSRPColor(rsrp);

            int idx = (y * width + x) * 4;
            image[idx + 0] = c.r;
            image[idx + 1] = c.g;
            image[idx + 2] = c.b;
            image[idx + 3] = c.a;
        }
    }

    GLuint textureId;
    glGenTextures(1, &textureId);
    glBindTexture(GL_TEXTURE_2D, textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, image.data());

    stbi_write_png("heatmap.png", width, height, 4, image.data(), width * 4);
    cout << "Heatmap texture created and saved as heatmap.png" << endl;

    return textureId;
}