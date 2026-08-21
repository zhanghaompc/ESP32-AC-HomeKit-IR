#pragma once
#include <Arduino.h>

// 详细日志开关：想关掉详细日志时，把下面这行注释掉即可
#define DEBUG_LOG

#ifdef DEBUG_LOG
#define DBG(...) Serial.printf(__VA_ARGS__)
#else
#define DBG(...) ((void)0)
#endif
