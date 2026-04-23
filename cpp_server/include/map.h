#pragma once

#include <map>
#include <queue>
#include <string>
#include <vector>
#include <mutex>
#include <GL/glew.h>
#include <implot.h>

using namespace std;

double MercatorXToTileX(double mercatorX, int zoom);
double MercatorYToTileY(double mercatorY, int zoom);
double TileXToMercatorX(int tileX, int zoom);
double TileYToMercatorY(int tileY, int zoom);
double LatToMercatorY(double lat);

struct TileJob
{
    string id;
    int zoom;
    int x;
    int y;
};

struct TextureData
{
    GLuint id = 0;
    bool isLoading = false;
    vector<uint8_t> rgbaBlob;
    int width = 0;
    int height = 0;
};

extern map<string, TextureData> g_TileCache;
extern queue<TileJob> g_JobQueue;

extern mutex g_JobMutex;
extern mutex g_CacheMutex;

void StartWorker();
void FetchWorker();

void ClearQueueAndReset();
void EnqueueTile(const string &tileId, int zoom, int x, int y);
int CalculateZoom(double minX, double maxX);
void RenderMap();