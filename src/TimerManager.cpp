#include "TimerManager.h"
#include "IrManager.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>

extern IrManager irManager;

const char *ntpServer = "ntp.aliyun.com";
const long gmtOffset_sec = 8 * 3600;
const int daylightOffset_sec = 0;

TimerManager::TimerManager() : taskCount(0), nextId(1), lastCheckedMinute(-1), timeSynced(false)
{
    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        tasks[i].valid = false;
    }
}

void TimerManager::begin()
{
    loadTasks();
    Serial.printf("定时管理器初始化完成，已加载 %d 个定时任务\n", taskCount);
}

void TimerManager::syncTime()
{
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("正在同步NTP时间...");
        configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

        struct tm timeinfo;
        if (getLocalTime(&timeinfo))
        {
            timeSynced = true;
            Serial.print("时间同步成功: ");
            Serial.println(getCurrentTime());
        }
        else
        {
            Serial.println("时间同步失败");
        }
    }
}

// 手机通过 BLE 校时，写入 ESP32 硬件 RTC，BLE 模式下不依赖 WiFi/NTP
bool TimerManager::setTimeFromPhone(const String &datetime)
{
    int year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (sscanf(datetime.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) != 6)
        return false;

    if (year < 2020 || year > 2100 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59)
        return false;

    // 固定东八区，保证 mktime 与 getLocalTime 的解释一致
    setenv("TZ", "CST-8", 1);
    tzset();

    struct tm tmInfo = {};
    tmInfo.tm_year = year - 1900;
    tmInfo.tm_mon = month - 1;
    tmInfo.tm_mday = day;
    tmInfo.tm_hour = hour;
    tmInfo.tm_min = minute;
    tmInfo.tm_sec = second;
    tmInfo.tm_isdst = 0;

    time_t t = mktime(&tmInfo);
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);

    timeSynced = true;
    Serial.printf("收到手机校时: %s\n", datetime.c_str());
    return true;
}

bool TimerManager::isTimeSynced() const
{
    return timeSynced;
}

String TimerManager::getCurrentTime()
{
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo))
    {
        return "未同步";
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    return String(buf);
}

int TimerManager::getHour()
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        return timeinfo.tm_hour;
    }
    return -1;
}

int TimerManager::getMinute()
{
    struct tm timeinfo;
    if (getLocalTime(&timeinfo))
    {
        return timeinfo.tm_min;
    }
    return -1;
}

void TimerManager::loop()
{
    if (!timeSynced)
    {
        static unsigned long lastSyncAttempt = 0;
        if (millis() - lastSyncAttempt > 10000)
        {
            lastSyncAttempt = millis();
            syncTime();
        }
        return;
    }

    int currentMinute = getMinute();
    int currentHour = getHour();

    if (currentMinute == -1 || currentHour == -1)
        return;

    if (currentMinute != lastCheckedMinute)
    {
        lastCheckedMinute = currentMinute;

        for (int i = 0; i < MAX_TIMER_TASKS; i++)
        {
            if (tasks[i].valid && tasks[i].enabled)
            {
                if (tasks[i].hour == currentHour && tasks[i].minute == currentMinute)
                {
                    Serial.printf("执行定时任务 #%d: %02d:%02d\n", tasks[i].id, tasks[i].hour, tasks[i].minute);
                    irManager.send(tasks[i].temp, tasks[i].speed, tasks[i].mode, tasks[i].power);

                    if (!tasks[i].repeat)
                    {
                        tasks[i].enabled = false;
                        Serial.printf("一次性任务 #%d 已禁用\n", tasks[i].id);
                    }
                    saveTasks();
                }
            }
        }
    }
}

int TimerManager::addTask(int hour, int minute, int temp, int mode, int speed, bool power, bool repeat)
{
    if (taskCount >= MAX_TIMER_TASKS)
    {
        Serial.println("定时任务已满");
        return -1;
    }

    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        if (!tasks[i].valid)
        {
            tasks[i].id = nextId++;
            tasks[i].hour = hour;
            tasks[i].minute = minute;
            tasks[i].temp = temp;
            tasks[i].mode = mode;
            tasks[i].speed = speed;
            tasks[i].power = power;
            tasks[i].enabled = true;
            tasks[i].repeat = repeat;
            tasks[i].valid = true;
            taskCount++;
            saveTasks();
            Serial.printf("添加定时任务 #%d: %02d:%02d 温度=%d 模式=%d 风速=%d 电源=%s 重复=%s\n",
                          tasks[i].id, hour, minute, temp, mode, speed,
                          power ? "开" : "关", repeat ? "是" : "否");
            return tasks[i].id;
        }
    }
    return -1;
}

// 原地更新任务（不删除重建），避免“重新添加把原任务删掉”的困惑
bool TimerManager::updateTask(int id, int hour, int minute, int temp, int mode, int speed, bool power, bool repeat)
{
    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        if (tasks[i].valid && tasks[i].id == id)
        {
            tasks[i].hour = hour;
            tasks[i].minute = minute;
            tasks[i].temp = temp;
            tasks[i].mode = mode;
            tasks[i].speed = speed;
            tasks[i].power = power;
            tasks[i].repeat = repeat;
            tasks[i].enabled = true;
            saveTasks();
            Serial.printf("更新定时任务 #%d: %02d:%02d 温度=%d 模式=%d 风速=%d 电源=%s 重复=%s\n",
                          tasks[i].id, hour, minute, temp, mode, speed,
                          power ? "开" : "关", repeat ? "是" : "否");
            return true;
        }
    }
    return false;
}

bool TimerManager::deleteTask(int id)
{
    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        if (tasks[i].valid && tasks[i].id == id)
        {
            tasks[i].valid = false;
            taskCount--;
            saveTasks();
            Serial.printf("删除定时任务 #%d\n", id);
            return true;
        }
    }
    return false;
}

bool TimerManager::enableTask(int id, bool enabled)
{
    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        if (tasks[i].valid && tasks[i].id == id)
        {
            tasks[i].enabled = enabled;
            saveTasks();
            Serial.printf("定时任务 #%d %s\n", id, enabled ? "已启用" : "已禁用");
            return true;
        }
    }
    return false;
}

String TimerManager::getTaskJson(int index)
{
    if (index < 0 || index >= MAX_TIMER_TASKS || !tasks[index].valid)
    {
        return "";
    }

    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"id\":%d,\"hour\":%d,\"minute\":%d,\"temp\":%d,\"mode\":%d,\"speed\":%d,\"power\":%s,\"enabled\":%s,\"repeat\":%s}",
             tasks[index].id, tasks[index].hour, tasks[index].minute,
             tasks[index].temp, tasks[index].mode, tasks[index].speed,
             tasks[index].power ? "true" : "false",
             tasks[index].enabled ? "true" : "false",
             tasks[index].repeat ? "true" : "false");
    return String(buf);
}

String TimerManager::getTaskList()
{
    String result = "[";
    bool first = true;

    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        if (tasks[i].valid)
        {
            if (!first)
                result += ",";
            first = false;
            result += getTaskJson(i);
        }
    }

    result += "]";
    return result;
}

int TimerManager::getTaskCount() const
{
    return taskCount;
}

void TimerManager::saveTasks()
{
    File file = SPIFFS.open("/timers.txt", "w");
    if (!file)
    {
        Serial.println("保存定时任务失败");
        return;
    }

    for (int i = 0; i < MAX_TIMER_TASKS; i++)
    {
        if (tasks[i].valid)
        {
            file.printf("%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
                        tasks[i].id, tasks[i].hour, tasks[i].minute,
                        tasks[i].temp, tasks[i].mode, tasks[i].speed,
                        tasks[i].power ? 1 : 0, tasks[i].enabled ? 1 : 0,
                        tasks[i].repeat ? 1 : 0);
        }
    }
    file.close();
    Serial.println("定时任务已保存");
}

void TimerManager::loadTasks()
{
    if (!SPIFFS.exists("/timers.txt"))
    {
        Serial.println("未找到定时任务文件");
        return;
    }

    File file = SPIFFS.open("/timers.txt", "r");
    if (!file)
    {
        Serial.println("打开定时任务文件失败");
        return;
    }

    taskCount = 0;
    nextId = 1;

    while (file.available())
    {
        String line = file.readStringUntil('\n');
        line.trim();
        if (line.length() == 0)
            continue;

        int vals[9];
        int idx = 0;
        int pos = 0;

        while (pos < (int)line.length() && idx < 9)
        {
            int comma = line.indexOf(',', pos);
            if (comma == -1)
                comma = line.length();
            vals[idx++] = line.substring(pos, comma).toInt();
            pos = comma + 1;
        }

        if (idx == 9)
        {
            for (int i = 0; i < MAX_TIMER_TASKS; i++)
            {
                if (!tasks[i].valid)
                {
                    tasks[i].id = vals[0];
                    tasks[i].hour = vals[1];
                    tasks[i].minute = vals[2];
                    tasks[i].temp = vals[3];
                    tasks[i].mode = vals[4];
                    tasks[i].speed = vals[5];
                    tasks[i].power = vals[6] == 1;
                    tasks[i].enabled = vals[7] == 1;
                    tasks[i].repeat = vals[8] == 1;
                    tasks[i].valid = true;
                    taskCount++;

                    if (tasks[i].id >= nextId)
                        nextId = tasks[i].id + 1;
                    break;
                }
            }
        }
    }
    file.close();
    Serial.printf("已加载 %d 个定时任务\n", taskCount);
}
