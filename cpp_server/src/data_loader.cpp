#include "data_loader.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <vector>

using json = nlohmann::json;
using namespace std;

vector<MeasurementPoint> LoadMeasurements(const string &filename)
{
    ifstream file(filename);

    vector<MeasurementPoint> result;

    if (!file.is_open())
    {
        cout << "Failed to open file\n";
        return result;
    }

    string line;
    string objectText;

    int braceCount = 0;
    bool insideObject = false;

    while (getline(file, line))
    {
        for (char c : line)
        {
            if (c == '{')
            {
                braceCount++;

                if (!insideObject)
                    insideObject = true;
            }

            if (insideObject)
                objectText += c;

            if (c == '}')
            {
                braceCount--;

                if (braceCount == 0 && insideObject)
                {
                    try
                    {
                        json j = json::parse(objectText);

                        MeasurementPoint m{};

                        m.lat = j.value("latitude", 0.0);
                        m.lon = j.value("longitude", 0.0);
                        m.altitude = j.value("altitude", 0.0);

                        m.rsrp = -140;
                        m.rsrq = -30;
                        m.rssi = -140;
                        m.earfcn = 0;

                        if (j.contains("cell") &&
                            j["cell"].is_array() &&
                            !j["cell"].empty())
                        {
                            auto &cell = j["cell"][0];

                            m.rsrp = cell.value("rsrp", -140);
                            m.rsrq = cell.value("rsrq", -30);
                            m.rssi = cell.value("rssi", -140);
                            m.earfcn = cell.value("earfcn", 0);
                        }

                        result.push_back(m);
                    }
                    catch (const exception &e)
                    {
                        cout << "Parse error: " << e.what() << endl;
                    }

                    objectText.clear();
                    insideObject = false;
                }
            }
        }
    }

    cout << "Loaded measurements: " << result.size() << endl;

    return result;
}