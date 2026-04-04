#pragma once
#include <Arduino.h>
#include <SPIFFS.h>

#define MAX_TIMER_TASKS 10

struct TimerTask
{
    int id;
    int hour;
    int minute;
    int temp;
    int mode;
    int speed;
    bool power;
    bool enabled;
    bool repeat;
    bool valid;
};

class TimerManager
{
private:
    TimerTask tasks[MAX_TIMER_TASKS];
    int taskCount;
    int nextId;
    int lastCheckedMinute;
    bool timeSynced;

    void saveTasks();
    void loadTasks();
    String getTaskJson(int index);

public:
    TimerManager();

    void begin();
    void loop();
    void syncTime();

    bool isTimeSynced() const;
    String getCurrentTime();
    int getHour();
    int getMinute();

    int addTask(int hour, int minute, int temp, int mode, int speed, bool power, bool repeat);
    bool deleteTask(int id);
    bool enableTask(int id, bool enabled);
    String getTaskList();
    int getTaskCount() const;
};
