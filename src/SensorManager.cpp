#include "SensorManager.h"
#include <Arduino.h>

extern float envTemperature;
extern float enHumidity;

SensorManager::SensorManager() {}

void SensorManager::begin()
{
    if (aht.begin())
    {
        sensorOnline = true;
        Serial.println("AHT20传感器初始化成功");
    }
    else
    {
        sensorOnline = false;
        Serial.println("AHT20传感器初始化失败");
    }
}

void SensorManager::loop()
{
    static unsigned long lastSensorRead = 0;
    static float lastTemp = 0;
    static float lastHum = 0;

    if (!sensorOnline)
    {
        return;
    }

    if (millis() - lastSensorRead < 2500)
    {
        return;
    }

    lastSensorRead = millis();

    sensors_event_t humidityEvent, tempEvent;
    aht.getEvent(&humidityEvent, &tempEvent);

    bool changed = false;

    if (!isnan(tempEvent.temperature))
    {
        temperature = tempEvent.temperature;
        envTemperature = temperature;
        if (abs(temperature - lastTemp) >= 0.2f)
        {
            changed = true;
            lastTemp = temperature;
        }
    }

    if (!isnan(humidityEvent.relative_humidity))
    {
        humidity = humidityEvent.relative_humidity;
        enHumidity = humidity;
        if (abs(humidity - lastHum) >= 0.5f)
        {
            changed = true;
            lastHum = humidity;
        }
    }

    if (changed)
    {
        Serial.printf("传感器读取: 温度=%.1f℃, 湿度=%.1f%%\n", temperature, humidity);
    }
}

float SensorManager::getTemperature() const
{
    return temperature;
}

float SensorManager::getHumidity() const
{
    return humidity;
}
