#include "map.h"
#include <thread>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <curl/curl.h>
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#include <iostream>

using namespace std;

map<string, TextureData> g_TileCache;
queue<TileJob> g_JobQueue;
mutex g_JobMutex;
mutex g_CacheMutex;

size_t WriteCallback(void *contents, size_t size, size_t nmemb, vector<uint8_t> *data)
{
    size_t totalSize = size * nmemb;
    data->insert(data->end(), (uint8_t *)contents, (uint8_t *)contents + totalSize);
    return totalSize;
}

double MercatorXToTileX(double mercatorX, int zoom)
{
    return (0.5 + mercatorX / 360.0) * (1 << zoom);
}

double MercatorYToTileY(double mercatorY, int zoom)
{
    return (0.5 - mercatorY / 360.0) * (1 << zoom);
}

double TileXToMercatorX(int tileX, int zoom)
{
    return (tileX / static_cast<double>(1 << zoom) - 0.5) * 360.0;
}

double TileYToMercatorY(int tileY, int zoom)
{
    return (0.5 - tileY / static_cast<double>(1 << zoom)) * 360.0;
}

double LatToMercatorY(double lat)
{
    double rad = lat * M_PI / 180.0;
    return log(tan(M_PI / 4.0 + rad / 2.0)) * 180.0 / M_PI;
}

int CalculateZoom(double minX, double maxX)
{
    double diff = maxX - minX;
    int zoom = 0;
    double threshold = 180.0;
    while (zoom < 19 && diff < threshold)
    {
        threshold /= 2.0;
        zoom++;
    }
    return max(1, min(18, zoom));
}

void ClearQueueAndReset()
{
    {
        lock_guard<mutex> lock(g_JobMutex);
        while (!g_JobQueue.empty())
            g_JobQueue.pop();
    }
    {
        lock_guard<mutex> lock(g_CacheMutex);
        for (auto &[k, v] : g_TileCache)
        {
            if (v.id != 0)
            {
                glDeleteTextures(1, &v.id);
                v.id = 0;
            }
            v.isLoading = false;
            v.rgbaBlob.clear();
        }
    }
}

void EnqueueTile(const string &tileId, int zoom, int x, int y)
{
    {
        lock_guard<mutex> lock(g_JobMutex);
        g_JobQueue.push({tileId, zoom, x, y});
    }
    {
        lock_guard<mutex> lock(g_CacheMutex);
        g_TileCache[tileId].isLoading = true;
    }
}

void FetchWorker()
{
    while (true)
    {
        TileJob job;
        {
            lock_guard<mutex> lock(g_JobMutex);
            if (g_JobQueue.empty())
            {
                this_thread::sleep_for(chrono::milliseconds(100));
                continue;
            }
            job = g_JobQueue.front();
            g_JobQueue.pop();
        }

        string url = "https://tile.openstreetmap.org/" +
                     to_string(job.zoom) + "/" +
                     to_string(job.x) + "/" +
                     to_string(job.y) + ".png";

        vector<uint8_t> pngData;
        CURL *curl = curl_easy_init();
        if (curl)
        {
            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &pngData);
            curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
            curl_easy_setopt(curl, CURLOPT_USERAGENT, "LocationServer/1.0");
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                cout << "CURL error: " << curl_easy_strerror(res) << endl;
                curl_easy_cleanup(curl);
                continue;
            }
            curl_easy_cleanup(curl);
        }
        else
        {
            cout << "Failed to create CURL handle" << endl;
            continue;
        }

        int width, height, channels;
        unsigned char *imageData = stbi_load_from_memory(
            pngData.data(), pngData.size(), &width, &height, &channels, 4);

        vector<uint8_t> rgbaData;
        if (imageData)
        {
            rgbaData.assign(imageData, imageData + (width * height * 4));
            stbi_image_free(imageData);
        }
        else
        {
            rgbaData.assign(256 * 256 * 4, 128);
            width = 256;
            height = 256;
        }

        {
            lock_guard<mutex> lock(g_CacheMutex);
            auto &tex = g_TileCache[job.id];
            tex.rgbaBlob = move(rgbaData);
            tex.width = width;
            tex.height = height;
            tex.isLoading = false;
        }
    }
}

void StartWorker()
{
    thread(FetchWorker).detach();
}

void RenderMap()
{
    static int frame = 0;
    frame++;

    ImPlotRect limits = ImPlot::GetPlotLimits();

    static int zoom = 5;
    int newZoom = CalculateZoom(limits.X.Min, limits.X.Max);

    if (newZoom != zoom)
    {
        zoom = newZoom;
        ClearQueueAndReset();
        cout << "Zoom changed to: " << zoom << endl;
    }

    int minX = max(0, (int)floor(MercatorXToTileX(limits.X.Min, zoom)));
    int minY = max(0, (int)floor(MercatorYToTileY(limits.Y.Max, zoom)));
    int maxX = min((1 << zoom) - 1, (int)floor(MercatorXToTileX(limits.X.Max, zoom)));
    int maxY = min((1 << zoom) - 1, (int)floor(MercatorYToTileY(limits.Y.Min, zoom)));

    if (frame % 60 == 0)
    {
        cout << "=== Tile Debug ===" << endl;
        cout << "Zoom: " << zoom << " Tiles: " << (maxX - minX + 1) * (maxY - minY + 1) << endl;
        cout << "Cache size: " << g_TileCache.size() << endl;

        int texCount = 0;
        for (auto &[id, tex] : g_TileCache)
        {
            if (tex.id != 0)
                texCount++;
        }
        cout << "Textures created: " << texCount << endl;
    }

    for (int x = minX; x <= maxX; ++x)
    {
        for (int y = minY; y <= maxY; ++y)
        {
            string tileId = to_string(zoom) + "/" + to_string(x) + "/" + to_string(y);

            unique_lock<mutex> lock(g_CacheMutex);
            auto &tex = g_TileCache[tileId];

            if (!tex.rgbaBlob.empty() && tex.id == 0)
            {
                cout << "Creating texture for " << tileId << " (" << tex.width << "x" << tex.height << ")" << endl;

                glGenTextures(1, &tex.id);
                if (tex.id != 0)
                {
                    glBindTexture(GL_TEXTURE_2D, tex.id);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tex.width, tex.height, 0,
                                 GL_RGBA, GL_UNSIGNED_BYTE, tex.rgbaBlob.data());
                    tex.rgbaBlob.clear();
                    cout << "Texture created, id=" << tex.id << endl;
                }
            }

            if (tex.id == 0 && !tex.isLoading)
            {

                lock.unlock();
                continue;
            }
            if (tex.id != 0)
            {
                ImPlotPoint minPoint{
                    TileXToMercatorX(x, zoom),
                    TileYToMercatorY(y + 1, zoom)};
                ImPlotPoint maxPoint{
                    TileXToMercatorX(x + 1, zoom),
                    TileYToMercatorY(y, zoom)};

                ImPlot::PlotImage(("##tile_" + tileId).c_str(),
                                  (ImTextureID)(intptr_t)tex.id,
                                  minPoint, maxPoint);
            }
        }
    }
}