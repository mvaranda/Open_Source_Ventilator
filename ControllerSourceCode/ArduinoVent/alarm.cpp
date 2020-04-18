
/*************************************************************
 * Open Ventilator
 * Copyright (C) 2020 - Marcelo Varanda
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 **************************************************************
*/


#include "alarm.h"
#include "log.h"
#include "hal.h"
#include "languages.h"

#define MAX_SOUND_DEFAULT                   3
#define MAX_SOUND_ALARM_LOW_PRESSURE        MAX_SOUND_DEFAULT
#define MAX_SOUND_ALARM_HIGH_PRESSURE       MAX_SOUND_DEFAULT
#define MAX_SOUND_ALARM_UNDER_SPEED         MAX_SOUND_DEFAULT

// #define SIM_HIGH_PRESSURE

typedef enum : uint8_t {
    ST_ALARM_OFF,
    ST_ALARM_ON,
} state_t;

typedef void (*muteFunc_t)(void);
typedef void (*goOffFunc_t)(void);

typedef struct alarm_vars_st {
state_t     state;
uint8_t     cnt_sound;         // num of times being sounded before become visual only
} alarm_vars_t;

typedef struct alarm_st {
    alarm_vars_t * vars;
    int8_t      max_sound;         // num max to be sounded. if -1 always will have sound alarm
    const char *  message;
    goOffFunc_t goOffAction;
    muteFunc_t  muteAction;
} alarm_t;

static Alarm * alarm;

inline bool isMuted (const alarm_t * a) {
    if (a->max_sound == -1)
        return false;
    if (a->vars->cnt_sound >= a->max_sound)
        return true;
    return false;
}

void alarmResetAll()
{
    alarm->internalAlarmResetAll();
}

void muteHighPressureAlarm()
{

}

void muteLowPressureAlarm()
{

}

// It looks complicated... but we want alarms to be in PROGMEM.
static  alarm_vars_t alarm_vars [] = {
                        // For:
    {ST_ALARM_OFF, 0},  //   HIGH_PRESSURE
    {ST_ALARM_OFF, 0},  //   LOW_PRESSURE
    {ST_ALARM_OFF, 0},  //   UNDER_SPEEP
    {ST_ALARM_OFF, 0},  //   FAST_CALIB_TO_START
    {ST_ALARM_OFF, 0},  //   FAST_CALIB_DONE
    {ST_ALARM_OFF, 0},  //   BAD_PRESS
};


#ifndef VENTSIM
  static const alarm_t alarms[] PROGMEM = {
#else
  static const alarm_t alarms[] = {
#endif
  {
        &alarm_vars[0],
        MAX_SOUND_ALARM_HIGH_PRESSURE,
        STR_ALARM_HIGH_PRESSURE,
        0,
        muteHighPressureAlarm
  },

  {
        &alarm_vars[1],
        MAX_SOUND_ALARM_LOW_PRESSURE,
        STR_ALARM_LOW_PRESSURE,
        0,
        muteLowPressureAlarm
  },
  
  {
        &alarm_vars[2],
        MAX_SOUND_ALARM_UNDER_SPEED,
        STR_ALARM_UNDER_SPEED,
        0,
        0
  },

    {
        &alarm_vars[3],
        MAX_SOUND_DEFAULT,
        STR_ALARM_FAST_CALIB_TO_START,
        0,
        0
    },

    {
        &alarm_vars[4],
        MAX_SOUND_DEFAULT,
        STR_ALARM_FAST_CALIB_DONE,
        0,
        0
    },
  
    {
        &alarm_vars[5],
        MAX_SOUND_DEFAULT,
        STR_ALARM_BAD_PRESS_SENSOR,
        0,
        0
    },


};
#define NUM_ALARMS  sizeof(alarms) / sizeof(alarm_t)

#ifndef VENTSIM
alarm_t * loadAlarmRecord(int idx) {
    static alarm_t alarm;
    int i;
    uint8_t * srcPtr = (uint8_t *) &alarms[idx];
    uint8_t * dstPtr = (uint8_t *) &alarm;

    for (i=0; i<sizeof(alarm_t); i++) {
        *dstPtr = pgm_read_byte_near(srcPtr);
        srcPtr++;
        dstPtr++;
    }
    return &alarm;
}
#else
alarm_t * loadAlarmRecord(int idx) {
    static alarm_t alarm;
    alarm = alarms[idx];
    return &alarm;
}
#endif

void Alarm::beepOnOff(bool on)
{
    if (on) {
        if (beepIsOn == false) {
            beepIsOn = true;
            halBeepAlarmOnOff(true);
        }
    }
    else {
        if (beepIsOn == true) {
            beepIsOn = false;
            halBeepAlarmOnOff(false);
        }
    }
}

void Alarm::internalAlarmResetAll()
{
    uint8_t i;
    const alarm_t * a;
    activeAlarmIdx = -1;
    for (i=0; i< NUM_ALARMS; i++) {
        a = loadAlarmRecord(i);
        a->vars->cnt_sound = 0;
        a->vars->state = ST_ALARM_OFF;
        a++;
    }
    beepOnOff(false);
    CEvent::post(EVT_ALARM_DISPLAY_OFF,0);
}

void Alarm::setNextAlarmIfAny(bool fromMute)
{
    uint8_t i;
    const alarm_t * a;

    if (activeAlarmIdx >= 0) {
        // tolerates as there is already an alarm. mute will take care of calling this func once again
        return;
    }

    for (i=0; i< NUM_ALARMS; i++) {
        a = loadAlarmRecord(i);

        if (a->vars->state == ST_ALARM_ON){
            activeAlarmIdx = i;
            if (a->goOffAction) { // call an action if a callback was defined
                a->goOffAction();
            }
            CEvent::post(EVT_ALARM_DISPLAY_ON, (char *) a->message);
            if (isMuted(a) == false) {
                if (fromMute)
                  beepOnOff(true);
            }
            return;
        }
    }
}

void Alarm::muteAlarmIfOn()
{
    if (activeAlarmIdx < 0)
        return;

    beepOnOff(false);

    const alarm_t * a = loadAlarmRecord(activeAlarmIdx);

    if (a->muteAction) { // call an action if a callback was defined
        a->muteAction();
    }
    a->vars->state = ST_ALARM_OFF;
    if ((a->max_sound != -1) && (a->vars->cnt_sound < a->max_sound))
        a->vars->cnt_sound++;

    activeAlarmIdx = -1;
    CEvent::post(EVT_ALARM_DISPLAY_OFF, 0);
    setNextAlarmIfAny(true);
}


void alarmInit()
{
    alarm = new Alarm();
}

void alarmLoop()
{

}

static void processAlarmEvent(const alarm_t * a)
{
  a->vars->state = ST_ALARM_ON;
  if (isMuted(a) == false) {
      alarm->beepOnOff(true);
  }
  alarm->setNextAlarmIfAny(false);
}


Alarm::Alarm ()
{

}

void Alarm::Loop()
{

}

propagate_t Alarm::onEvent(event_t * event)
{
    const alarm_t * a;
    int i;

    switch (event->type) {

      case EVT_ALARM:

        if (event->param.iParam < 0 || event->param.iParam >= ALARM_IDX_END) {
            LOG("Alarm with bad parameter");
            return PROPAGATE;
        }
        a = loadAlarmRecord( event->param.iParam);
        processAlarmEvent(a);
        break;

      case EVT_KEY_PRESS:
#ifdef SIM_HIGH_PRESSURE
        if (event->param.iParam == KEY_SET) {
          LOG("SIM High pressure Alarm event");
          CEvent::post(EVT_ALARM, EVT_ALARM_HIGH_PRESSURE);
        }
        else if (event->param.iParam == KEY_INCREMENT_PIN) {
          LOG("SIM Low pressure Alarm event");
          CEvent::post(EVT_ALARM, EVT_ALARM_LOW_PRESSURE);
        }
        else {
          LOG("mute event");
          muteAlarmIfOn();
          //return PROPAGATE_STOP;
        }
#else
        muteAlarmIfOn();
        return PROPAGATE;
#endif
      case EVT_KEY_RELEASE:
        break;

      default:
        return PROPAGATE;

    }

    return PROPAGATE;
}

Alarm::~Alarm()
{

}
