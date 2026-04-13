#pragma once

#include "common.h"
#include <mutex>

void zmq_server(DeviceData *dev_data, std::mutex *mtx);