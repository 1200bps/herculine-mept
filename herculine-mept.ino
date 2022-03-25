/*


                Fresh out of the closet, from the makers of many
                     questionable decisions, please enjoy...


                                 Herculine I
                           a GPS-timed MEPT beacon
                             for FSKCW and DFCW
                               de Brynn NN7NB


     VK3EDW's FSKCW MEPT sketch was used as a starting point to write this,
     and much  of the code  (especially  the  Morse character  decoder)  is
     calqued from it. His sketch:
     https://github.com/vk3edw/QRSS-MEPT-VK3EDW/
     Many thanks to John for putting his code online!

     This sketch is usable as-is as a basic QRSS beacon, and should make a
     good skeleton for building more complex beacon projects.

     Dependencies:
       * Etherkit Si5351 library
       * TinyGPS++



     I'd  like to  thank all  my elmers,  all my exes,  deschloroketamine,
     Judge Schreber's  becoming-woman,  and my  fellow radio  amateurs and
     superfluous men around the globe. This couldn't have happened without
     them--for better and for worse!

     --. .-..    . ...    --. ..- -..    -.. -..-
     --... ....-    . .

*/

#include <si5351.h>
#include <Wire.h>
#include <TinyGPS++.h>
#include <SoftwareSerial.h>





// FOR DEBUGGING:
// When this is set to true, GPS time checks are bypassed and the
// beacon begins its transmit cycle immediately on power-up.
// Set this to false before you pur your beacon on-air!
bool debugForceTransmit = false;





// Configuration variables--change these to suit your application:

// Mode selection: true for FSKCW, false for DFCW.
// This determines the initial mode upon power-up,
// but other logic elsewhere may switch modes at
// any time. By default, the sketch alternates
// modes every other 10-minute frame.
bool modeFSKCW = false;

// DFCW dit, FSKCW 'space' or key-up (Hz)
unsigned long long int freqSpace = 7039750;
// DFCW dah, FSKCW 'mark' or key-down (Hz)
// Set the bandwidth of your signal here; standard is 5Hz
unsigned long long int freqMark = freqSpace + 5ULL;
// Oscillator is parked here while not beaconing;
// this improves thermal stability vs. disabling
// the clock output between beacon frames
unsigned long long int freqStdby = 40000000;

// Si5351A eference oscillator freq (Hz);
// set to 0 (default) or 25MHz for most Si5351 boards (Adafruit,
// Etherkit, Raduino), or 27MHz on the QRP Labs board
#define FREQ_REF_OSC 27005000
// Reference crystal calibration value (parts per billion)
#define PPB_CAL 181000
// The standard RX frame length for QRSS is 10 minutes.
// The beacon will start transmitting this many
// minutes and seconds past the zeroth second of
// minutes ending in zero (when the time is XX:X0:00)
#define FRAME_OFFSET_MIN 1
#define FRAME_OFFSET_SEC 30

// FSKCW parameters
//
// Length of one dit (milliseconds); standard is 6000
#define LEN_DIT_FSKCW 6000
// Length of one dah (milliseconds)
#define LEN_DAH_FSKCW 18000

// DFCW parameters
//
// One DFCW symbol = 3/4 key-down + 1/4 no carrier
// Length of one quarter-symbol (milliseconds); standard is 2500
#define LEN_QTR_SYM_DFCW 2500
// Length of space between letters, *not* including
// the no-carrier at the end of the preceding symbol:
#define LEN_SPC_DFCW 7500

// Messages to transmit every beacon cycle:
char messageFSKCW[ ] = "|N0CALL_";
char messageDFCW[ ]  = "N0CALL";
/*
     Special characters:
        ' ' --> 6-symbol space
        '|' --> 4-symbol space
        '_' --> 1-symbol space (good for DFCW)
     By the way--if you wish to add non-callsign data here (e.g. sensor data),
     consider encoding it using abbreviated numbers a/k/a "cut numbers:"
         1 --> A     6 --> B
         2 --> U     7 --> G
         3 --> W     8 --> D
         4 --> V     9 --> N
         5 --> S     0 --> T
     Just be mindful of how long it takes to transmit your message!
*/





// Peripherals:

// Lit while sending message, may be used to key external PA or TX/RX switch
#define LED_PTT 4
// Lit whenever 'key is down' (FSK mark or DFCW symbol high)
// This is NOT a PTT line
#define LED_MARK 13
// Lit when we parse our first valid time packet from the GPS (stays on)
#define LED_GPS 12

// Serial pins to GPS (TX pin is unused)
#define SS_RX 8
#define SS_TX 9

Si5351 si5351;  // Connected to Arduino's hardware UART. On Nano: A4 --> SDA, A5 --> SCL
SoftwareSerial ss(SS_RX, SS_TX);
TinyGPSPlus gps;





// Morse encoding table:
// maps from ASCII chars to an encoded format, which become Morse
// characters after some arithmetical voodoo later on.
// TODO: understand this better, find out where it came from

// The generic struct
struct t_mtab {
  char c, pattern;
};
// The character table.
// If necessary, comment out unused characters to reduce dynamic memory usage.
struct t_mtab morsetab[] = {
  //{'.', 106},
  //{',', 115},
  //{'?', 76},
  //{'/', 41},
  {'A', 6},
  {'B', 17},
  {'C', 21},
  //{'D', 9},
  //{'E', 2},
  //{'F', 20},
  //{'G', 11},
  //{'H', 16},
  //{'I', 4},
  //{'J', 30},
  //{'K', 13},
  {'L', 18},
  //{'M', 7},
  {'N', 5},
  //{'O', 15},
  //{'P', 22},
  //{'Q', 27},
  //{'R', 10},
  //{'S', 8},
  //{'T', 3},
  //{'U', 12},
  //{'V', 24},
  //{'W', 14},
  //{'X', 25},
  //{'Y', 29},
  //{'Z', 19},
  //{'1', 62},
  //{'2', 60},
  //{'3', 56},
  //{'4', 48},
  //{'5', 32},
  //{'6', 33},
  {'7', 35},
  //{'8', 39},
  //{'9', 47},
  {'0', 63}
};

#define N_MORSE (sizeof(morsetab)/sizeof(morsetab[0]))





// Global variables:
bool gpsConnection;  // Have we received any valid NMEA packets yet?





void setup()
{

  Serial.begin(9600);  // Si5351A and serial console
  ss.begin(9600);      // GPS

  pinMode(LED_MARK, OUTPUT);
  digitalWrite(LED_MARK, LOW);

  pinMode(LED_PTT, OUTPUT);
  digitalWrite(LED_PTT, LOW);

  pinMode(LED_GPS, OUTPUT);
  digitalWrite(LED_GPS, LOW);

  // Convert Hz to hundredths of Hz:
  freqSpace = freqSpace * 100ULL;
  freqMark  = freqMark  * 100ULL;
  freqStdby = freqStdby * 100ULL;

  si5351.init(SI5351_CRYSTAL_LOAD_8PF, FREQ_REF_OSC, PPB_CAL);
  si5351.drive_strength(SI5351_CLK0, SI5351_DRIVE_8MA);  // 8mA is approx. 10dBm into a 50 ohm load

  // Let oscillator start warming up:
  si5351.set_freq(freqStdby, SI5351_CLK0);
  si5351.output_enable(SI5351_CLK0, 1);

  if (debugForceTransmit == false) {
    Serial.print("setup() complete, delay for GPS:\n");
    delay(5000);
  }

  gpsConnection = true;  // This will be set to false right away if we're not getting NMEA packets
}





// TX a dah or a dit when sendMsg() asks us to:

void dahFSKCW()
{

  Serial.print("dah ");
  si5351.set_freq(freqMark, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);
  delay(LEN_DAH_FSKCW);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);
  si5351.set_freq(freqSpace, SI5351_CLK0);
  si5351.output_enable(SI5351_CLK0, 1);
  delay(LEN_DIT_FSKCW);
}
void ditFSKCW()
{

  Serial.print("dit ");
  si5351.set_freq(freqMark, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);
  delay(LEN_DIT_FSKCW);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);
  si5351.set_freq(freqSpace, SI5351_CLK0);
  si5351.output_enable(SI5351_CLK0, 1);
  delay(LEN_DIT_FSKCW);
}

void dahDFCW()
{

  Serial.print("dah ");
  si5351.set_freq(freqMark, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);
  delay(LEN_QTR_SYM_DFCW * 3);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);  // Disable clock output for inter-symbol gap
  delay(LEN_QTR_SYM_DFCW * 1);
}
void ditDFCW()
{

  Serial.print("dit ");
  si5351.set_freq(freqSpace, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);
  delay(LEN_QTR_SYM_DFCW * 3);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);  // Disable clock output for inter-symbol gap
  delay(LEN_QTR_SYM_DFCW * 1);
}





// Decode individual characters into dits and dahs, transmit them:

void sendFSKCW(char c)
{

  int i;
  if (c == ' ') {
    delay(6 * LEN_DIT_FSKCW);
    return;
  }
  if (c == '|') {
    delay(4 * LEN_DIT_FSKCW);
    return;
  }
  if (c == '_') {
    delay(1 * LEN_DIT_FSKCW);
    return;
  }
  for (i = 0; i < N_MORSE; i++) {
    if (morsetab[i].c == c) {
      unsigned char p = morsetab[i].pattern;
      while (p != 1) {
        if (p & 1)
          dahFSKCW();
        else
          ditFSKCW();
        p = p / 2;
      }
      Serial.println();
      delay(2 * LEN_DIT_FSKCW);
      return;
    }
  }
}

void sendDFCW(char c)
{

  int i;
  if (c == ' ') {
    delay(24 * LEN_QTR_SYM_DFCW);
    return;
  }
  if (c == '|') {
    delay(16 * LEN_QTR_SYM_DFCW);
    return;
  }
  if (c == '_') {
    delay(4 * LEN_QTR_SYM_DFCW);
    return;
  }
  for (i = 0; i < N_MORSE; i++) {
    if (morsetab[i].c == c) {
      unsigned char p = morsetab[i].pattern;
      while (p != 1) {
        if (p & 1)
          dahDFCW();
        else
          ditDFCW();
        p = p / 2;
      }
      Serial.println();
      delay(LEN_SPC_DFCW);
      return;
    }
  }
}





// Iterate through message, send chars to the appropriate decoding function for the selected mode:

void sendMsg(char *str)
{

  if (modeFSKCW == true) {
    while (*str) {
      sendFSKCW(*str++);
    }
  }
  else {
    while (*str) {
      sendDFCW(*str++);
    }
  }
}





// Prepare 5351/PA for TX, pass message to sendMsg, then go back into standby after TX:

void sendData()
{

  si5351.set_freq(freqSpace, SI5351_CLK0);
  si5351.output_enable(SI5351_CLK0, 1);
  digitalWrite(LED_PTT, HIGH);
  Serial.println("External PA is keyed");
  delay(10);  // Time for external PA or TX/RX switch to activate

  if (modeFSKCW == true) {
    sendMsg(messageFSKCW);
  }
  else {
    sendMsg(messageDFCW);
  }

  digitalWrite(LED_PTT, LOW);
  Serial.println("Unkeyed external PA");
  delay(50);
  si5351.set_freq(freqStdby, SI5351_CLK0);  // We don't want to feed the standby frequency into
  si5351.output_enable(SI5351_CLK0, 1);     // the PA while it's active
}





// This function determines timing of transmission frames.
// Double-check that your messages are shorter than the gap between TX windows!
bool gpsTxGate()
{

  // Transmit every 10m, or when debugForceTransmit == true
  if (gps.time.minute() % 10 == FRAME_OFFSET_MIN && gps.time.second() == FRAME_OFFSET_SEC)
    return true;
  else if (debugForceTransmit == true)
    return true;
  else
    return false;
}





// Mode-switching logic.
// By default, this switches modes after every transmitted
// frame; you could replace this with a counter, some logic
// to change modes based on the GPS time or date, a toggle
// switch, or disable mode-switching altogether.
void modeSwitch()
{
  if (true) {
    modeFSKCW = !modeFSKCW;
  }
}





// This runs more or less every time we receive a new NMEA packet
// from the GPS; any GPS-related logic, regular console messages, or
// low-frequency input polling (e.g. a voltage or temperature
// sensor for telemetry) could go here.
void gpsLoop()
{

  // Print time in HHMMSSCC format every 5 seconds
  // TODO: zero pad, convert to HH:MM:SS
  if (gps.time.second() % 5 == 0)
    Serial.println(gps.time.value());

  // Transmit at the appropriate time
  if (gpsTxGate() == true) {
    Serial.println("Time to transmit!");
    sendData();
    Serial.println("Done transmitting");
    modeSwitch();
  }

  delay(250);
}







void loop()
{
  
  // Pass NMEA packets to TinyGPS++ for processing
  while (ss.available() > 0)
    gps.encode(ss.read());

  // Check GPS connection on power-up, inform user if we're not getting data,
  // or skip check if debugForceTransmit == false
  // TODO: monitor connection continuously, check for valid GPS fix (>=4 satellites)
  if (gps.charsProcessed() < 10 && millis() > 10000 && gpsConnection == true && !debugForceTransmit) {
    Serial.println("Not receiving packets from GPS;\ncheck connections!");
    gpsConnection = false;
    digitalWrite(LED_GPS, LOW);
  }

  // If we're getting and decoding valid NMEA packets:
  if (gps.time.isUpdated()) {
    gpsConnection = true;
    digitalWrite(LED_GPS, HIGH);
    gpsLoop();
  }
}
