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

                        if (j.contains("cell"))
                        {

                            if (j["cell"].is_array() && !j["cell"].empty())
                            {
                                auto &cell = j["cell"][0];

                                m.rsrp = cell.value("rsrp", -140);
                                m.rsrq = cell.value("rsrq", -30);
                                m.rssi = cell.value("rssi", -140);
                                m.sinr = cell.value("sinr", 0.0);

                                m.earfcn = cell.value("earfcn", 0);
                                m.pci = cell.value("pci", 0);
                            }

                            else if (j["cell"].is_string())
                            {
                                string cellStr = j["cell"];

                                auto extractValue = [&](const string &key, double defaultVal)
                                {
                                    size_t pos = cellStr.find(key + "=");

                                    if (pos == string::npos)
                                        return defaultVal;

                                    pos += key.size() + 1;

                                    size_t end = cellStr.find_first_of(" }", pos);

                                    string val = cellStr.substr(pos, end - pos);

                                    try
                                    {
                                        return stod(val);
                                    }
                                    catch (...)
                                    {
                                        return defaultVal;
                                    }
                                };

                                m.rsrp = extractValue("rsrp", -140);
                                m.rsrq = extractValue("rsrq", -30);
                                m.rssi = extractValue("rssi", -140);
                            }
                        }
                        if (m.lat == 0.0 || m.lon == 0.0)
                            continue;

                        if (m.rsrp < -120 || m.rsrp > -40)
                            continue;

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