#pragma once
#include <Adafruit_AHTX0.h>

class SensorManager
{
public:
    SensorManager();
    void begin();
    void loop();
    float getTemperature() const;
    float getHumidity() const;

private:
    Adafruit_AHTX0 aht;
    float temperature = 0.0f;
    float humidity = 0.0f;
    bool sensorOnline = false;
};
