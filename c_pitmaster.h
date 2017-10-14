 /*************************************************** 
    Copyright (C) 2016  Steffen Ochs

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

    The AutotunePID elements of this program based on the work of 
    AUTHOR: Repetier
    PURPOSE: Repetier-Firmware/extruder.cpp

    HISTORY: Please refer Github History
    
 ****************************************************/

int pidMax = 100;      // Maximum (PWM) value, the heater should be set

//#define PM_DEBUG              // ENABLE SERIAL MQTT DEBUG MESSAGES

#ifdef PM_DEBUG
  #define PMPRINT(...)    Serial.print(__VA_ARGS__)
  #define PMPRINTLN(...)  Serial.println(__VA_ARGS__)
  #define PMPRINTP(...)   Serial.print(F(__VA_ARGS__))
  #define PMPRINTPLN(...) Serial.println(F(__VA_ARGS__))
  #define PMPRINTF(...)   Serial.printf(__VA_ARGS__)
  
#else
  #define PMPRINT(...)     //blank line
  #define PMPRINTLN(...)   //blank line 
  #define PMPRINTP(...)    //blank line
  #define PMPRINTPLN(...)  //blank line
  #define PMPRINTF(...)    //blank line
#endif


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Set Pitmaster Pin
void set_pitmaster(bool init) {
  
  pinMode(PITMASTER1, OUTPUT);
  digitalWrite(PITMASTER1, LOW);

  pinMode(PITMASTER2, OUTPUT);
  digitalWrite(PITMASTER2, LOW);

  if (sys.hwversion > 1) {
    pinMode(PITSUPPLY, OUTPUT);
    digitalWrite(PITSUPPLY, LOW);
  }

  if (init) {
    pitmaster.pid = 0;
    pitmaster.channel = 0;
    pitmaster.set = PITMASTERSETMIN;
    pitmaster.active = false;
    //pitmaster.resume = 0;
  }

  pitmaster.resume = 1;   // später wieder raus
  if (!pitmaster.resume) pitmaster.active = false; 

  pitmaster.value = 0;
  pitmaster.manual = false;
  pitmaster.event = false;
  pitmaster.msec = 0;
  pitmaster.pause = 1000;

  dutycycle.on = false;
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Set Default PID-Settings
void set_pid(byte index) {
  
  pidsize = 3; //3;
  pid[0] = {"SSR SousVide", 0, 0, 165, 0.591, 1000, 100, 0.08, 5, 0, 95, 0.9, 0, 0, 100, 0, 0, 0, 0};
  pid[1] = {"TITAN 50x50", 1, 1, 3.8, 0.01, 128, 6.2, 0.001, 5, 0, 95, 0.9, 0, 25, 100, 0, 0, 0, 0};
  pid[2] = {"Kamado 50x50", 2, 1, 7.0, 0.019, 630, 6.2, 0.001, 5, 0, 95, 0.9, 0, 25, 100, 0, 0, 0, 0};

  if (index)
    pid[2] = {"Servo", 3, 2, 7.0, 0.019, 630, 6.2, 0.001, 5, 0, 95, 0.9, 0, 0, 100, 0, 0, 0, 0};

}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// PID
float PID_Regler(){ 

  // see: http://rn-wissen.de/wiki/index.php/Regelungstechnik

  float x = ch[pitmaster.channel].temp;         // IST
  float w = pitmaster.set;                      // SOLL
  byte ii = pitmaster.pid;
  
  // PID Parameter
  float kp, ki, kd;
  if (x > (pid[ii].pswitch * w)) {
    kp = pid[ii].Kp;
    ki = pid[ii].Ki;
    kd = pid[ii].Kd;
  } else {
    kp = pid[ii].Kp_a;
    ki = pid[ii].Ki_a;
    kd = pid[ii].Kd_a;
  }

  // Abweichung bestimmen
  //float e = w - x;                          
  int diff = (w -x)*10;
  float e = diff/10.0;              // nur Temperaturunterschiede von >0.1°C beachten
  
  // Proportional-Anteil
  float p_out = kp * e;                     
  
  // Differential-Anteil
  float edif = (e - pid[ii].elast)/(pitmaster.pause/1000.0);   
  pid[ii].elast = e;
  float d_out = kd * edif;                  

  // Integral-Anteil
  // Anteil nur erweitert, falls Bregrenzung nicht bereits erreicht
  if ((p_out + d_out) < PITMAX) {
    pid[ii].esum += e * (pitmaster.pause/1000.0);             
  }
  // ANTI-WIND-UP (sonst Verzögerung)
  // Limits an Ki anpassen: Ki*limit muss y_limit ergeben können
  if (pid[ii].esum * ki > pid[ii].Ki_max) pid[ii].esum = pid[ii].Ki_max/ki;
  else if (pid[ii].esum * ki < pid[ii].Ki_min) pid[ii].esum = pid[ii].Ki_min/ki;
  float i_out = ki * pid[ii].esum;
                    
  // PID-Regler berechnen
  float y = p_out + i_out + d_out;  
  y = constrain(y,PITMIN,PITMAX);           // Auflösung am Ausgang ist begrenzt            
  
  PMPRINTLN("[PM]\tPID:" + String(y,1) + "\tp:" + String(p_out,1) + "\ti:" + String(i_out,2) + "\td:" + String(d_out,1));
  
  return y;
}


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// AUTOTUNE
void disableAllHeater() {
  // Anschlüsse ausschalten
  digitalWrite(PITMASTER1, LOW);
  pitmaster.active = 0;
  pitmaster.value = 0;
  pitmaster.event = false;
  pitmaster.msec = 0;
  
}

static inline float min(float a,float b)  {
        if(a < b) return a;
        return b;
}

static inline float max(float a,float b)  {
        if(a < b) return b;
        return a;
}

void stopautotune() {
  autotune.value = 0;
  autotune.initialized = false;
  question.typ = AUTOTUNE;
  drawQuestion(autotune.stop);
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// AUTOTUNE
void startautotunePID(int maxCyc, bool store, int over, long tlimit)  {
  
  autotune.cycles = 0;           // Durchläufe
  autotune.heating = true;      // Flag
  autotune.temp = pitmaster.set;
  autotune.maxCycles = constrain(maxCyc, 5, 20);
  autotune.storeValues = store;
  autotune.keepup = false;
  
  uint32_t tmi = millis();
  float tem = ch[pitmaster.channel].temp;

  // t0, t1, t2, t_high, bias, d, Kp, Ki, Kd, maxTemp, minTemp, pTemp, maxTP, tWP, TWP
  
  autotune.t0 = tmi;    // Zeitpunkt t0
  autotune.t1 = tmi;    // Zeitpunkt t1
  autotune.t2 = tmi;    // Zeitpunkt t2
  autotune.t_high = 0;
  autotune.bias = pidMax/2;         // Startwert = halber Wert
  autotune.d = pidMax/2;          // Startwert = halber Wert

  autotune.Kp = 0;
  autotune.Ki = 0; 
  autotune.Kd = 0;
  autotune.maxTemp = tem; 
  autotune.minTemp = tem;
  autotune.pTemp = tem;  // Startwert
  autotune.maxTP = 0.0;
  autotune.tWP = tmi;
  autotune.TWP = tem;

  autotune.overtemp = over; //40
  autotune.timelimit = tlimit; //(10L*60L*1000L*4L)
  
  PMPRINTPLN("[AT]\t Start!");
  
  disableAllHeater();         // switch off all heaters.
  autotune.value = pidMax;   // Aktor einschalten
  autotune.initialized = true;  
  pitmaster.active = true;
  autotune.start = tem;

  log_count = 0; //TEST
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// AUTOTUNE
float autotunePID() {

  /*
   Relay Feedback Test [Astrom & Hagglund, 1984
   Autotune Variation (ATV)
   see: https://www.google.de/?gws_rd=ssl#q=Relay+Feedback+PID
   
         
             t_high = t1-t2*
     +d       ____      ____      _
     bias--__|    |____|    |____|
     -d     t2*   t1   t2
   /                         t_low = t2-t1  
   
   */

  // Show start on OLED
  if (autotune.start) {
    
    question.typ = AUTOTUNE;
    drawQuestion(0);
    autotune.start = false;
  }

  float currentTemp = ch[pitmaster.channel].temp;
  unsigned long time = millis();

  if (autotune.cycles == 0) { 
    float TP = (currentTemp - autotune.pTemp)/ (float)pitmaster.pause;
    if (autotune.maxTP < TP) {
      autotune.maxTP = TP;
      autotune.tWP = time;
      autotune.TWP = (currentTemp + autotune.pTemp)/2.0;
      PMPRINTP("[AT]\tWendepunktbestimmung: ");
      PMPRINTLN(TP*1000);
    }
    autotune.pTemp = currentTemp;
  }
  
  autotune.maxTemp = max(autotune.maxTemp,currentTemp);
  autotune.minTemp = min(autotune.minTemp,currentTemp);
  
  // Soll während aufheizen ueberschritten --> switch heating off  
  if (autotune.heating == true && currentTemp > autotune.temp)  {
    if(time - autotune.t2 > 3000)  {    // warum Wartezeit und wieviel
                
      autotune.heating = false;
      autotune.value = (autotune.bias - autotune.d);     // Aktorwert
                
      autotune.t1 = time;
      autotune.t_high = autotune.t1 - autotune.t2;
      autotune.maxTemp = autotune.temp;
      PMPRINTPLN("[AT]\toverrun!");
    }
  }
  
  // Soll während abkühlen unterschritten --> switch heating on
  else if (autotune.heating == false && currentTemp < autotune.temp) {
    if(time - autotune.t1 > 3000)  {
                
      autotune.heating = true;
      autotune.t2 = time;
      autotune.t_low = autotune.t2 - autotune.t1; // half wave length
      PMPRINTPLN("[AT]\tfall below");

      if (autotune.cycles == 0) {

        // Bestimmung von Kp_a, Ki_a, Kd_a
        // Approximation der Strecke durch PT1Tt-Glied aus Sprungantwort

        uint32_t tWP1 = (autotune.TWP-autotune.minTemp)/autotune.maxTP;  // Zeitabschn. (ms) unter Wendetangente bis Ausgangstemperatur
        uint32_t tWP2 = (autotune.temp-autotune.TWP)/autotune.maxTP;     // Zeitabschn. (ms) oberhalb Wendetangente bis Zieltemperatur
        
        uint32_t Tt = (autotune.tWP - autotune.t0 - tWP1)/1000.0;      // Totzeit in sec
        uint32_t Tg = (tWP1 + tWP2)/1000.0;                            // Ausgleichszeit in sec

        float Ks = (autotune.temp - autotune.minTemp)/(autotune.bias + autotune.d);
        float Tn = 2 * Tt;            // Tn = 3.33 * Tt;
        float Tv = 0.5 * Tt;

        PMPRINTP("[AT]\tTt: ");
        PMPRINT(Tt);
        PMPRINTP(" s, Tg: ");
        PMPRINT(Tg);
        PMPRINTPLN(" s");

        autotune.Kp_a = 1.2/Ks * Tg/Tt;  // Kp_a = 0.9/Ks * Tg/Tt
        autotune.Ki_a = autotune.Kp_a/Tn;
        autotune.Kd_a = autotune.Kp_a*Tv;

        PMPRINTP("[AT]\tKp_a: ");
        PMPRINT(autotune.Kp_a);
        PMPRINTP(" Ki_a: ");
        PMPRINT(autotune.Ki_a);
        PMPRINTP(" Kd_a: ");
        PMPRINTLN(autotune.Kd_a);
        
        
      } else {

        // Anpassung Bias
        autotune.bias += (autotune.d*(autotune.t_high - autotune.t_low))/(autotune.t_low + autotune.t_high);
        autotune.bias = constrain(autotune.bias, 5 ,pidMax - 5);  // Arbeitsbereich zwischen 5% und 95%
                    
        if (autotune.bias > pidMax/2)  autotune.d = pidMax - autotune.bias;
        else autotune.d = autotune.bias;
        
        PMPRINTP("[AT]\tbias: ");
        PMPRINT(autotune.bias);
        PMPRINTP(" d: ");
        PMPRINTLN(autotune.d);

        if(autotune.cycles > 2)  {

        // Parameter according Ziegler & Nichols method: http://en.wikipedia.org/wiki/Ziegler%E2%80%93Nichols_method
        // Ku = kritische Verstaerkung = 4*d / (pi*A)
        // Tu = kritische Periodendauer = t_low + t_high = (t2 - t1) + (t1 - t2*) = t2 - t2*
        // A = Amplitude der Schwingung = maxTemp-minTemp/2 // Division fehlt in der Quelle

          float A = (autotune.maxTemp - autotune.minTemp)/2.0;
          float Ku = (4.0 * autotune.d) / (3.14159 * A);
          float Tu = ((float)(autotune.t_low + autotune.t_high)/1000.0);
          
          PMPRINTP("[AT]\tKu: ");
          PMPRINT(Ku);
          PMPRINTP(" Tu: ");
          PMPRINT(Tu);
          PMPRINTP(" A: ");
          PMPRINT(A);
          PMPRINTP(" t_max: ");
          PMPRINT(autotune.maxTemp);
          PMPRINTP(" t_min: ");
          PMPRINTLN(autotune.minTemp);

          float Tn = 0.5*Tu;
          float Tv = 0.125*Tu;
          
          autotune.Kp = 0.6*Ku;
          autotune.Ki = autotune.Kp/Tn;      // Ki = 1.2*Ku/Tu = Kp/Tn = Kp/(0.5*Tu) = 2*Kp/Tu
          autotune.Kd = autotune.Kp*Tv;      // Kd = 0.075*Ku*Tu = Kp*Tv = Kp*0.125*Tu
                        
          PMPRINTP("[AT]\tKp: ");
          PMPRINT(autotune.Kp);
          PMPRINTP(" Ki: ");
          PMPRINT(autotune.Ki);
          PMPRINTP(" Kd: ");
          PMPRINTLN(autotune.Kd);
            
        }
      }
      autotune.value = (autotune.bias + autotune.d);
      PMPRINTPLN("[AT]\tNext cycle");
      autotune.cycles++;
      autotune.minTemp = autotune.temp;
    }
  }
    
  if (currentTemp > (autotune.temp + autotune.overtemp))  {   // FEHLER
    IPRINTPLN("f:AT OVERTEMP");
    disableAllHeater();
    autotune.stop = 2;
    return 0;
  }
    
  if (((time - autotune.t1) + (time - autotune.t2)) > autotune.timelimit) {   // 20 Minutes
        
    IPRINTPLN("f:AT TIMEOUT");
    disableAllHeater();
    autotune.stop = 2;
    return 0;
  }
    
  if (autotune.cycles > autotune.maxCycles) {       // FINISH
            
    PMPRINTPLN("[AT]\tFinished!");
    disableAllHeater();
    autotune.stop = 1;
            
    if (autotune.storeValues)  {
      
      pid[pitmaster.pid].Kp = autotune.Kp;
      pid[pitmaster.pid].Ki = autotune.Ki;
      pid[pitmaster.pid].Kd = autotune.Kd;

      
      pid[pitmaster.pid].Kp_a = autotune.Kp_a;
      pid[pitmaster.pid].Ki_a = autotune.Ki_a;
      pid[pitmaster.pid].Kd_a = autotune.Kd_a;
      DPRINTLN("[AT]\tParameters saved!");
    }
    return 0;
  }
  PMPRINTLN("AT: " + String(autotune.value,1) + " %");
  return autotune.value;       
}



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Control - Pitmaster Pause
void check_pit_pause() {

  int aktor;
  if (dutycycle.on) aktor = dutycycle.aktor;
  else aktor = pid[pitmaster.pid].aktor;

  switch (aktor) {

    case 0: // SSR
      pitmaster.pause = 2000;    // 1/2 Hz, Netzsynchron
      break; 
      
    case 1: // FAN
      pitmaster.pause = 1000;    // 1 Hz
      break;
      
    case 2: // SERVO
      pitmaster.pause = 20;      // 50 Hz
      break;
  }
}

// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Control - Pitmaster 12V Supply
bool pitsupply(bool out) {
  
  if (out) {
    if (sys.pitsupply && sys.hwversion > 1) return HIGH;      // FAN
  } else if (sys.hwversion > 1) return HIGH;                  // SSR
  return LOW;
}


int myPitmaster() {

  // veränderte Taktzeit durch Anpassung der PitmasterPause
  // Puffertemp < Ofentemp und Ofentemp > Grenze
  if (ch[pitmaster.channel].temp < ch[3].temp && ch[3].temp > pitmaster.set)        // ch[3] = OFEN
    return 100;
  else
    return 0;
}


  #define SERVOPULSMIN 550  // 25 Grad    // 785
  #define SERVOPULSMAX 2190


// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Control - Manuell PWM
void pitmaster_control() {

  if (autotune.stop > 0) {
    if ((autotune.stop == 1) && autotune.storeValues) { // sauber beendet
      setconfig(ePIT,{});
      if (autotune.keepup) pitmaster.active = true;     // Pitmaster fortsetzen
    }
    stopautotune();
    autotune.stop = 0;
  }

  // DC-Test beenden
  if (dutycycle.on && (millis() - dutycycle.timer > 10000)) {
    if (dutycycle.saved == 0) {   // off
      pitmaster.active = false;
      pitmaster.manual = false;
    } else if (dutycycle.saved > 0) pitmaster.value = dutycycle.saved;  //manual
    else pitmaster.manual = false;  // auto
    dutycycle.on = false;
    PMPRINTLN("[DC]\tTest finished");
  }
  
  // ESP PWM funktioniert nur bis 10 Hz Trägerfrequenz stabil, daher eigene Taktung
  if (pitmaster.active == 1) {

    // Check Pitmaster Pause
    check_pit_pause();
  
    // Ende eines HIGH-Intervalls, wird durch pit_event nur einmal pro Intervall durchlaufen
    if ((millis() - pitmaster.last > pitmaster.msec) && pitmaster.event) {
      digitalWrite(PITMASTER1, LOW);
      pitmaster.event = false;
    }

    // neuen Stellwert bestimmen und ggf HIGH-Intervall einleiten
    if (millis() - pitmaster.last > pitmaster.pause) {
  
      float y;
      pitmaster.last = millis();

      // DUTY CYCLE PROCESS
      // bei Servo beide male zuerst in die Mitte
      if (dutycycle.on)  {
        if (!dutycycle.dc && (millis() - dutycycle.timer < 1000)) pitmaster.value = 50;   // Startanlauf
        else pitmaster.value = dutycycle.value;
        
        switch (dutycycle.aktor) {

          case 0:   //SSR
            pitmaster.msec = map(pitmaster.value,0,100,0,pitmaster.pause); 
            if (sys.hwversion > 1)  digitalWrite(PITSUPPLY, pitsupply(0));   // 12V Supply
            if (pitmaster.msec > 0) digitalWrite(PITMASTER1, HIGH);
            if (pitmaster.msec < pitmaster.pause) pitmaster.event = true; // außer bei 100% 
            break;

          case 1:   // FAN
            if (sys.hwversion > 1)  digitalWrite(PITSUPPLY, pitsupply(1));   // 12V Supply
            analogWrite(PITMASTER1,map(pitmaster.value,0,100,0,1024));
            break;

          case 2:   // SERVO
            int msec = map(pitmaster.value,0,100,SERVOPULSMIN,SERVOPULSMAX);
            if (sys.hwversion > 1)  digitalWrite(PITSUPPLY, LOW);   // keine 12V Supply
            noInterrupts();
            unsigned long time1 = micros();
            digitalWrite(PITMASTER1, HIGH);
            while (micros() - time1 < msec) {}
            digitalWrite(PITMASTER1, LOW);
            interrupts();
            break;
            
        }
        return;
      }
      else if (autotune.initialized)  pitmaster.value = autotunePID();
      else if (!pitmaster.manual)     pitmaster.value = PID_Regler();
      // falls manual wird value vorgegeben

      int _DCmin, _DCmax;
      
      // NORMAL PITMASTER PROCESS
      switch (pid[pitmaster.pid].aktor) {

        case 0:     // SSR
          _DCmin = map(pid[pitmaster.pid].DCmin,0,100,0,pitmaster.pause);
          _DCmax = map(pid[pitmaster.pid].DCmax,0,100,0,pitmaster.pause);
          pitmaster.msec = map(pitmaster.value,0,100,_DCmin,_DCmax); 
          if (sys.hwversion > 1)  digitalWrite(PITSUPPLY, pitsupply(0));   // 12V Supply
          if (pitmaster.msec > 0) digitalWrite(PITMASTER1, HIGH);
          if (pitmaster.msec < pitmaster.pause) pitmaster.event = true;  // außer bei 100%
          break;

        case 1:     // FAN
          _DCmin = map(pid[pitmaster.pid].DCmin,0,100,0,1024);
          _DCmax = map(pid[pitmaster.pid].DCmax,0,100,0,1024);
          if (sys.hwversion > 1)  digitalWrite(PITSUPPLY, pitsupply(1));   // 12V Supply
          if (pitmaster.value == 0) {   // bei 0 soll der Lüfter auch stehen
            analogWrite(PITMASTER1,0);
            pitmaster.timer0 = millis();  
          } else {
            if (millis() - pitmaster.timer0 < 1500)  {  // ein Zyklus
              analogWrite(PITMASTER1,map(30,0,100,_DCmin,_DCmax));   // BOOST
            } else
              analogWrite(PITMASTER1,map(pitmaster.value,0,100,_DCmin,_DCmax));
          }
          break;
        
        case 2:     // SERVO
          _DCmin = map(pid[pitmaster.pid].DCmin,0,100,SERVOPULSMIN,SERVOPULSMAX);
          _DCmax = map(pid[pitmaster.pid].DCmax,0,100,SERVOPULSMIN,SERVOPULSMAX);
          int msec = map(pitmaster.value,0,100,_DCmin,_DCmax);
          if (sys.hwversion > 1)  digitalWrite(PITSUPPLY, LOW);   // keine 12V Supply
          noInterrupts();
          unsigned long time1 = micros();
          digitalWrite(PITMASTER1, HIGH);
          while (micros() - time1 < msec) {}
          //delayMicroseconds(pitmaster.value);  // zu ungenau
          //unsigned long time2 = micros();
          digitalWrite(PITMASTER1, LOW);
          interrupts();
          break;
      }
    }
  } else {    // TURN OFF PITMASTER
    if (pid[pitmaster.pid].aktor == 1)  // FAN
      analogWrite(PITMASTER1, LOW);
    else digitalWrite(PITMASTER1, LOW); // SSR
    if (sys.hwversion > 1) digitalWrite(PITSUPPLY, LOW);   // 12V Supply
    pitmaster.value = 0;
    pitmaster.event = false;
    pitmaster.msec = 0;
  }
  
}



// ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Control Duty Cycle
void DC_control(bool dc, byte aktor, int val) {

  if (!dutycycle.on) {
    dutycycle.dc = dc;
    dutycycle.aktor = aktor;
    dutycycle.value = val;
    dutycycle.on = true;
    dutycycle.timer = millis();
    if (pitmaster.active) 
      if (pitmaster.manual) dutycycle.saved = pitmaster.value;  // bereits im manual Modus
      else if (autotune.initialized) {
        dutycycle.saved = 0; // autotune abbrechen
        autotune.stop = 2;
      }
      else dutycycle.saved = -1;   // bereits im auto Modus
    else dutycycle.saved = 0;
    pitmaster.active = true;
    pitmaster.manual = true;    // nur für die Anzeige
  }
}


// Autotune braucht einen Mindestsprung für die _a Werte




