#include "LedManager.h"

LedManager::LedManager() {}

void LedManager::begin()
{
    FastLED.addLeds<WS2812B, LED_PIN, GRB>(leds, NUM_LEDS);
    FastLED.setBrightness(50);
    off();
}

void LedManager::setColor(CRGB color)
{
    blinking = false;
    leds[0] = color;
    FastLED.show();
}

void LedManager::blinkGreen()
{
    if (blinking && blinkColor == CRGB::Green)
        return; // 已在闪绿色，不重置计时
    blinking = true;
    blinkColor = CRGB::Green;
    blinkState = true;
    lastBlinkTime = millis();
    leds[0] = blinkColor;
    FastLED.show();
}

void LedManager::blinkBlue()
{
    if (blinking && blinkColor == CRGB::Blue)
        return;
    blinking = true;
    blinkColor = CRGB::Blue;
    blinkState = true;
    lastBlinkTime = millis();
    leds[0] = blinkColor;
    FastLED.show();
}

void LedManager::blinkRed()
{
    if (blinking && blinkColor == CRGB::Red)
        return;
    blinking = true;
    blinkColor = CRGB::Red;
    blinkState = true;
    lastBlinkTime = millis();
    leds[0] = blinkColor;
    FastLED.show();
}

void LedManager::blinkPurple()
{
    if (blinking && blinkColor == CRGB::Purple)
        return;
    blinking = true;
    blinkColor = CRGB::Purple;
  blinkState = true;
  lastBlinkTime = millis();
  leds[0] = blinkColor;
  FastLED.show();
}

void LedManager::blinkWhite()
{
    if (blinking && blinkColor == CRGB::White)
        return;
    blinking = true;
    blinkColor = CRGB::White;
  blinkState = true;
  lastBlinkTime = millis();
  leds[0] = blinkColor;
  FastLED.show();
}

void LedManager::stopBlink()
{
    blinking = false;
}

void LedManager::off()
{
    blinking = false;
    leds[0] = CRGB::Black;
    FastLED.show();
}

void LedManager::update()
{
    if (!blinking)
        return;

    if (millis() - lastBlinkTime >= blinkInterval)
    {
        lastBlinkTime = millis();
        blinkState = !blinkState;
        leds[0] = blinkState ? blinkColor : CRGB::Black;
        FastLED.show();
    }
}
