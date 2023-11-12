//#define DEBUG_ENABLE

#ifdef DEBUG_ENABLE
#define DEBUG(x) Serial.print(x)
#define DEBUGLN(x) Serial.println(x)
#else
#define DEBUG(x)
#define DEBUGLN(x)
#endif

#define EB_FAST_TIME 120
#define MENU_PARAMS_LEFT_OFFSET 89 // смещение вправо для рендеринга значений

#include <GyverOLED.h>
#include <EncButton.h>
#include <microDS18B20.h>
#include <TimerMs.h>
#include <EEManager.h>
#include "GyverPID.h"
#include "e:\Documents\Arduino\PWMrelay\src\PWMrelay.h"
#include "e:\Documents\Arduino\PWMrelay\src\PWMrelay.cpp"


#include "e:\Documents\Arduino\GyverOLEDMenu\src\GyverOLEDMenu.h"

EncButton eb(6, 7, 5, INPUT_PULLUP); // 6, 7 - энкодер; 5 - кнопка энкодера
MicroDS18B20<4> ds18B20;
GyverOLED<SSH1106_128x64> oled;

TimerMs getTmp(15000, 1);
TimerMs isSetTemp(500, 0); // если находимтся в режиме задания желаемой температуры
TimerMs tmDisplay; // таймер отключения дисплея
TimerMs tmSleepMode; // таймер отвечающий за вход и выход температуры сна

PWMrelay relay(8, HIGH, 60000);
OledMenu<12, GyverOLED<SSH1106_128x64>> menu(&oled);
GyverPID pid_regulator;

struct SParams {
  unsigned int k_p;
  float k_i;
  unsigned int k_d;
  float setTemp; // Заданная температура
  float sleepTemp; // Температура для сна
  float wakeUpTemp; // Температура после выхода из сна
  byte displayTimeout; // время отключения дисплея. 0 - не будет отключаться
  byte pid_preset; // заранее сконфигурированные предустановки пид 
  byte pid_i_limit; // Лимит накопления ошибки интегральной составляющей
};

// =================================================================================================================================

const float inc05 = 0.5;

SParams params;
float temp = 0; // Текущая тепература

boolean isShowDigSetT = true; // показывается в данный момент число задаваемой температуры (мигание)

unsigned int param_sleep_in = 0; // Через сколько минут включится температура для сна (SLEEP temp.)
unsigned int param_wake_up_in = 0; // Через сколько минут выключится температура для сна (SLEEP temp.)


EEManager mem_params(params);

void renderSetTemp(boolean isUpdate = false) {
  if (menu.isMenuShowing) {
    return;
  }

  if (isShowDigSetT) {
    oled.setCursorXY(69, 0);
    oled.textMode(BUF_REPLACE);
    oled.setScale(2);
    oled.print(params.setTemp);
  } else {
    oled.rect(69, 0, 128, 18, OLED_CLEAR);
  }

  oled.setCursorXY(31, 7);
  oled.textMode(BUF_ADD);
  oled.setScale(0);
  oled.print(F("задан."));  

  if (isUpdate) {
    oled.update();
  }
}

void renderCurrentTemp(boolean isUpdate = false) {
  if (menu.isMenuShowing) {
    return;
  }

  oled.setCursorXY(20, 34);
  oled.textMode(BUF_REPLACE);
  oled.setScale(3);
  oled.print(temp);

  oled.setCursorXY(20, 56);
  oled.textMode(BUF_ADD);
  oled.setScale(0);
  oled.print(F("текущая темпер."));  

  if (isUpdate) {
    oled.update();
  }
}

void setPidParams() {
  pid_regulator.setpoint = params.setTemp;
  pid_regulator.Kp = params.k_p;
  pid_regulator.Ki = params.k_i;
  pid_regulator.Kd = params.k_d;

  DEBUG("P: ");
  DEBUG(pid_regulator.Kp);
  DEBUG("; I: ");
  DEBUG(pid_regulator.Ki);
  DEBUG("; D: ");
  DEBUGLN(pid_regulator.Kd);
}

void displayOn() {
  tmDisplay.start();
  oled.setPower(true);
}

void resetParams() {
  params.pid_preset = 1;
  params.setTemp = 22.00;
  params.sleepTemp = 20.00;
  params.wakeUpTemp = 22.00;
  params.displayTimeout = 15;  
  param_sleep_in = 0;
  param_wake_up_in = 0;

  setPidPreset();
}

void setPidPreset() {
  switch (params.pid_preset) {
    case 1:
      params.k_p = 300;
      params.k_i = 1.5;
      params.k_d = 40000;
      params.pid_i_limit = 127;
      setPidParams();

      break;        

    case 2:
      params.k_p = 80;
      params.k_i = 3.00;
      params.k_d = 40000;
      params.pid_i_limit = 127;
      setPidParams();

      break;             
  }  
}

void showHomePage() {
  menu.showMenu(false);
  renderSetTemp();
  renderCurrentTemp(true);  
}


void setup() {
#ifdef DEBUG_ENABLE
  Serial.begin(9600);
#endif
  resetParams();

  mem_params.begin(0, 'h');

  tmDisplay.setTimerMode();
  tmDisplay.setTime(params.displayTimeout * 1000);
  tmDisplay.attach(onDisplayOff);

  tmSleepMode.setTimerMode();
  tmSleepMode.attach(onTimeSleepSakeUp);


  ds18B20.requestTemp();
  delay(1500);

  if (ds18B20.readTemp()) {
    temp = ds18B20.getTemp();
  }

  setPidParams();
  pid_regulator.setDt(15000);
  pid_regulator.input = temp;

  oled.init();
  Wire.setClock(400000L);
  oled.clear();
  oled.update();

  menu.onChange(onItemChange);
  menu.onPrintOverride(onItemPrintOverride);

  menu.addItem(PSTR("<- EXIT")); // 0
  menu.addItem(PSTR("POWER"), GM_N_BYTE(1), &(params.pid_preset), GM_N_BYTE(1), GM_N_BYTE(2)); // 1
  menu.addItem(PSTR("SLEEP IN"), GM_N_U_INT(5), &param_sleep_in, GM_N_U_INT(0), GM_N_U_INT(1440)); // 2
  menu.addItem(PSTR("WAKE UP IN"), GM_N_U_INT(5), &param_wake_up_in, GM_N_U_INT(0), GM_N_U_INT(2880)); // 3
  menu.addItem(PSTR("SLEEP temp."), &inc05, &(params.sleepTemp), GM_N_FLOAT(0), GM_N_FLOAT(30)); // 4
  menu.addItem(PSTR("WAKE UP temp."), &inc05, &(params.wakeUpTemp), GM_N_FLOAT(0), GM_N_FLOAT(30)); // 5

  menu.addItem(PSTR("PID-i limit"), GM_N_BYTE(1), &(params.pid_i_limit), GM_N_BYTE(1), GM_N_BYTE(255)); // 6
  menu.addItem(PSTR("PID-p"), GM_N_U_INT(1), &(params.k_p), GM_N_U_INT(0), GM_N_U_INT(1000)); // 7
  menu.addItem(PSTR("PID-i"), GM_N_FLOAT(0.1), &(params.k_i), GM_N_FLOAT(0), GM_N_FLOAT(50)); // 8
  menu.addItem(PSTR("PID-d"), GM_N_U_INT(50), &(params.k_d), GM_N_U_INT(0), GM_N_U_INT(65000)); // 9
  menu.addItem(PSTR("DISP OFF aft."), GM_N_BYTE(1), &(params.displayTimeout), GM_N_BYTE(0), GM_N_BYTE(255)); // 10
  menu.addItem(PSTR("RESET")); // 11


  eb.attach(onEncButton);

  getTmp.attach(onGetTmp);
  isSetTemp.attach(onDigBlink);

  renderSetTemp();
  renderCurrentTemp();
 
  oled.update();

  displayOn();
}

boolean onItemPrintOverride(const int index, const void* val, const byte valType) {
  switch (index) {
    case 1:
      switch(params.pid_preset) {
        case 1: oled.print(F("1.5kw."));
          break;
        case 2: oled.print(F("2.0kw."));
          break;          
      }
      return true;
    case 2:
    case 3:
      printTime((unsigned int*)val);
      return true;
  }

  return false;
}

void printTime(unsigned int* min) {
  unsigned int hours = *min / 60; // [hh]
  byte minutes = *min - (hours * 60); // [mm]

  if (hours < 10) {
    oled.print(0);
  }
  oled.print(hours);
  oled.print(":");
  
  if (minutes < 10) {
    oled.print(0);
  }    
  oled.print(minutes);    
}

void onDisplayOff() {
  if (params.displayTimeout == 0 || menu.isMenuShowing) {
    return;
  }

  oled.setPower(false);
}


void onTimeSleepSakeUp() {
  if (param_sleep_in != 0) {
    param_sleep_in = 0;

    params.setTemp = params.sleepTemp;
    tmSleepMode.setTime(param_wake_up_in * 60000);
    tmSleepMode.start();

    DEBUG(F("wake_up aft: "));
    DEBUGLN(param_wake_up_in * 60000);
  } else if (param_wake_up_in != 0) {
    param_wake_up_in = 0;

    params.setTemp = params.wakeUpTemp;
    DEBUGLN(F("wake_up"));
  }

  pid_regulator.setpoint = params.setTemp; 
  renderSetTemp(true);
}

void onDigBlink() {
  isShowDigSetT = !isShowDigSetT;
  renderSetTemp(true);
}


void onGetTmp() {
  if (isSetTemp.active()) {
    return;
  }

  if (ds18B20.readTemp()) {
    temp = ds18B20.getTemp();

    renderCurrentTemp(true);
  }


  pid_regulator.input = temp;

  byte PWM = pid_regulator.getResult();

  pid_regulator.integral = constrain(pid_regulator.integral, 0, params.pid_i_limit);

  relay.setPWM(PWM);

  DEBUG(F("t: "));
  DEBUG(temp); 

  DEBUG(F("; PWM: "));    
  DEBUG(PWM);  


  DEBUG(F("; integral: "));    
  DEBUGLN(pid_regulator.integral);                

  ds18B20.requestTemp();
}

void onItemChange(const int index, const void* val, const byte valType) {
  
  if (valType == VAL_ACTION) {
    switch (index) {
      case 0: // <- EXIT
        showHomePage();
        
        break;
      case 11: // RESET
        resetParams();
        setPidParams();
        tmDisplay.setTime(params.displayTimeout * 1000);
        mem_params.update();
        showHomePage();

        break; 
    }

    return;
  } 

  switch (index) {
    case 2: // SLEEP IN
      if (param_sleep_in == 0) {
        tmSleepMode.stop();
        break;
      }

      tmSleepMode.setTime(param_sleep_in * 60000);
      tmSleepMode.start();

      DEBUG(F("sleep aft: "));
      DEBUGLN(param_sleep_in * 60000);

      break;

    case 7: // PID-p
    case 8: // PID-i
    case 9: // PID-d
      setPidParams();

      break;
    case 1: // POWER
      setPidPreset();
      menu.refresh();

      break;      
    case 10: // DISP OFF aft.
      tmDisplay.setTime(params.displayTimeout * 1000);       
      tmDisplay.start();

      break;      
  }

  mem_params.update();
}

void onEncButton() {
  switch (eb.action()) {
    case EB_TURN:
      displayOn();
      
      if (eb.dir() == 1) {
        menu.selectPrev(eb.fast());
        
        if (isSetTemp.active()) {
          params.setTemp = params.setTemp - inc05;
          mem_params.update();
          pid_regulator.setpoint = params.setTemp;
        }

      } else {
        menu.selectNext(eb.fast());

        if (isSetTemp.active()) {
          params.setTemp = params.setTemp + inc05;
          mem_params.update();
          pid_regulator.setpoint = params.setTemp;
        }        
      }

      if (isSetTemp.active()) {
        isSetTemp.stop();
        isShowDigSetT = true;
        renderSetTemp(true);
        isSetTemp.start();
      } 

      break;

    case EB_CLICK:
      displayOn();

      if (isSetTemp.active()) {
        isSetTemp.stop();

        isShowDigSetT = true;
        renderSetTemp(true);

        return;
      }

      if (eb.getClicks() >= 3) {
        menu.showMenu(true);        
      } else {
        menu.toggleChangeSelected();
      }

      break;

    case EB_HOLD:
      if (menu.isMenuShowing) {
        return;
      }

      if (!isSetTemp.active()) {
        isSetTemp.start();
      }

      break;     
  }
}

void loop() {
  eb.tick();
  getTmp.tick();
  relay.tick();
  isSetTemp.tick();
  tmDisplay.tick();
  tmSleepMode.tick();

  mem_params.tick();

#ifdef DEBUG_ENABLE
  if (Serial.available() > 0) {
    char incoming = Serial.read();
    float value = Serial.parseFloat();
    switch (incoming) {
      case 'p': params.k_p = value;
        setPidParams();
        mem_params.update();
        break;
      case 'i': params.k_i = value;
        setPidParams();
        mem_params.update();      
        break;
      case 'd': params.k_d = value;
        setPidParams();
        mem_params.update();      
        break;
      case 't': temp = value;
        break;  
      case 'q': pid_regulator.integral = value;
        break;               
    }
  } 
#endif
}
