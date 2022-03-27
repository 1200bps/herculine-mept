/*


               Fresh out of the closet, from the makers of many
                    questionable decisions, please enjoy...


                                Herculine I
                          a GPS-timed MEPT beacon
                            for FSKCW and DFCW
                              de Brynn NN7NB


    VK3EDW's FSKCW MEPT sketch was used as a starting point to write this,
    and much of the code (especially the Morse character decoder) is
    calqued from it. His sketch:
    https://github.com/vk3edw/QRSS-MEPT-VK3EDW/
    Many thanks to John for putting his code online!

    This sketch is usable as-is as a basic QRSS beacon, and should make a
    good skeleton for building more complex beacon projects.

    Dependencies:
      * Etherkit Si5351 library
      * TinyGPS++



    I'd like to thank all my elmers, all my exes, and my community of
    fellow radio amateurs and superfluous men around the globe. This
    couldn't have happened without them--for better and for worse!

    This sketch is postcardware: if you find it useful, feel free to mail
    a postcard, voided QSL, or a photo of your finished project. Beyond
    that, this sketch is presented with no explicit license.

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

// Message to transmit every beacon cycle.
// This, plus any extra data you add, may be up to 20 characters long
// (but realistically, you should keep it shorter).
char baseMessage[ ]  = "NN7NB ";
//   Supported punctuation:
//      '.' ',' '?' '/'
//   Special characters:
//      ' ' --> 6-symbol space in FSKCW, 2-symbol space in DFCW
//      '|' --> 4-symbol space in both modes
//      '_' --> 1-symbol space in both modes
//   By the way--if you wish to add non-callsign data here (e.g. sensor data),
//   consider encoding it using abbreviated numbers a/k/a "cut numbers:"
//       1 --> A     6 --> B
//       2 --> U     7 --> G
//       3 --> W     8 --> D
//       4 --> V     9 --> N
//       5 --> S     0 --> T
//   Later on we create a struct that maps from ASCII numerals to their
//   respective cut numbers.

// Mode selection: true for FSKCW, false for DFCW.
// This determines the initial mode upon power-up,
// but other logic elsewhere may switch modes at
// any time. By default, the sketch alternates
// modes every other 10-minute frame.
bool modeFSKCW = false;

// Frequency definitions.
// An unsigned long (after multiplying by 100 in setup()) can hold up to ~42MHz;
// change these to unsigned long longs for use on 6m and above
// DFCW dit, FSKCW 'space' or key-up (Hz)
unsigned long int freqSpace = 7039750;
// DFCW dah, FSKCW 'mark' or key-down (Hz)
// Set the bandwidth of your signal here; standard is 5Hz
unsigned long int freqMark = freqSpace + 5UL;
// Oscillator is parked here while not beaconing;
// this improves thermal stability vs. disabling
// the clock output between beacon frames
unsigned long int freqStdby = 40000000;

// Si5351A eference oscillator freq (Hz);
// set to 0 (default) or 25MHz for most Si5351 boards (Adafruit,
// Etherkit, Raduino), or 27MHz on the QRP Labs board
#define FREQ_REF_OSC 27005000
// Reference crystal calibration value (parts per billion)
#define PPB_CAL 181000
// The standard RX frame length for QRSS is 10 minutes.
// The beacon will start transmitting this many minutes
// past the zeroth second of minutes ending in zero
// (when the time is XX:X0:00)
#define FRAME_OFFSET_MIN 2

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
// TODO: make it easier to change the DFCW on/off ratio





// Peripherals:

// This line is held HIGH during the beacon's TX cycle, and is used
// to key an external PA or TX/RX switch
#define PTT_OUT 4

// Lit while sending message (and while the external PA or TX/RX switch is triggered)
#define LED_PTT 11
// Lit while 'key is down' (FSK mark or DFCW symbol high)
// This is NOT a PTT line
#define LED_MARK 13
// Lit once we've parsed our first valid time packet from the GPS (stays on)
#define LED_GPS 12

// 1PPS input from GPS; not currently used
// (Why pin D7? In case we do something with PinChangeInterrupt later;
// there are 3 interrupt vectors--each shared by D0-D7, D8-D13, A0-A5)
//#define PPS_GPS 7

// Serial pins to GPS (TX pin isn't used by the GPS, could perhaps be used for another peripheral?)
#define SS_RX 8
#define SS_TX 9

Si5351 si5351;  // Connected to Arduino's hardware UART. On Nano: A4 --> SDA, A5 --> SCL
SoftwareSerial ss(SS_RX, SS_TX);
TinyGPSPlus gps;





// Morse encoding table:
// maps from ASCII chars to an encoded format, which become Morse
// characters after some arithmetical voodoo later on.
// TODO: understand this better, find out where it came from
struct t_mtab {
  char c, pattern;
};
// If necessary, comment out unused characters to reduce dynamic memory usage
struct t_mtab morsetab[] = {
  //{'.', 106},
  //{',', 115},
  //{'?', 76},
  {'/', 41},
  {'A', 6},
  {'B', 17},
  {'C', 21},
  {'D', 9},
  //{'E', 2},
  //{'F', 20},
  {'G', 11},
  //{'H', 16},
  //{'I', 4},
  //{'J', 30},
  //{'K', 13},
  {'L', 18},
  {'M', 7},
  {'N', 5},
  //{'O', 15},
  //{'P', 22},
  //{'Q', 27},
  //{'R', 10},
  {'S', 8},
  {'T', 3},
  {'U', 12},
  {'V', 24},
  {'W', 14},
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

// Cut number table:
// maps from ASCII numerals to 'cut numbers' (which are ASCII letters)
struct t_cutnr {
  char numeral, cutNumber;
};
struct t_cutnr cutnrtab[] = {
  {'1', 'A'},
  {'2', 'U'},
  {'3', 'W'},
  {'4', 'V'},
  {'5', 'S'},
  {'6', 'B'},
  {'7', 'G'},
  {'8', 'D'},
  {'9', 'N'},
  {'0', 'T'}
};
#define N_CUTNR (sizeof(cutnrtab)/sizeof(cutnrtab[0]))





// Other global variables:
bool gpsConnection;  // Have we received any valid NMEA packets yet?
char txMessage[21];  // Transmitted message, reinitialized after every frame,
                     // filled with updated data before the next frame





void setup()
{

  Serial.begin(9600);  // Si5351A and serial console
  ss.begin(9600);      // GPS

  pinMode(PTT_OUT, OUTPUT);
  digitalWrite(PTT_OUT, LOW);

  pinMode(LED_PTT, OUTPUT);
  digitalWrite(LED_PTT, LOW);

  pinMode(LED_MARK, OUTPUT);
  digitalWrite(LED_MARK, LOW);

  pinMode(LED_GPS, OUTPUT);
  digitalWrite(LED_GPS, LOW);

  // Convert Hz to hundredths of Hz:
  freqSpace = freqSpace * 100UL;
  freqMark  = freqMark  * 100UL;
  freqStdby = freqStdby * 100UL;

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
  delay(LEN_DAH_FSKCW);
  digitalWrite(LED_MARK, LOW);
  si5351.set_freq(freqSpace, SI5351_CLK0);
  delay(LEN_DIT_FSKCW);
}
void ditFSKCW()
{

  Serial.print("dit ");
  si5351.set_freq(freqMark, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  delay(LEN_DIT_FSKCW);
  digitalWrite(LED_MARK, LOW);
  si5351.set_freq(freqSpace, SI5351_CLK0);
  delay(LEN_DIT_FSKCW);
}

void dahDFCW()
{

  Serial.print("dah ");
  si5351.set_freq(freqMark, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);  // Re-enable clock output after previous character
  delay(LEN_QTR_SYM_DFCW * 3);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);  // Disable clock output for inter-symbol gap
  delay(LEN_QTR_SYM_DFCW);
}
void ditDFCW()
{

  Serial.print("dit ");
  si5351.set_freq(freqSpace, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);  // Re-enable clock output after previous character
  delay(LEN_QTR_SYM_DFCW * 3);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);  // Disable clock output for inter-symbol gap
  delay(LEN_QTR_SYM_DFCW);
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
  if (c == '\0') {
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
    delay(8 * LEN_QTR_SYM_DFCW);
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
  if (c == '\0') {
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





// Iterate through message, send ASCII chars to the
// appropriate decoding function for the selected mode:
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

void doTx()
{

  si5351.set_freq(freqSpace, SI5351_CLK0);
  digitalWrite(PTT_OUT, HIGH); digitalWrite(LED_PTT, HIGH);
  Serial.println("External PA is keyed");
  delay(10);  // Time for external PA or TX/RX switch to activate

  sendMsg(txMessage);

  digitalWrite(PTT_OUT, LOW); digitalWrite(LED_PTT, LOW);
  Serial.println("Unkeyed external PA");
  delay(50);
  si5351.set_freq(freqStdby, SI5351_CLK0);  // We don't want to feed the standby frequency into
  si5351.output_enable(SI5351_CLK0, 1);     // the PA while it's active
}





// TX prep:
// This runs 30 seconds before the next frame is to be transmitted.
// if you're putting telemetry data in your transmission, add it here;
// otherwise, just strcat your baseMessage into txMessage (which is
// reinitialized, full of NULs, after every transmission).
void prepareToTx()
{
  int currentSpeed = gps.speed.mph();

  char currSpdTens[2];
  char currSpdOnes[2];

  itoa(currentSpeed / 10, currSpdTens, 10);
  itoa(currentSpeed % 10, currSpdOnes, 10);

  strcat(txMessage, baseMessage);
  strcat(txMessage, currSpdTens);
  strcat(txMessage, currSpdOnes);

  Serial.print("Next TX message: ");
  Serial.println(txMessage);
}





// Mode-switching logic.
// By default, this switches modes after every transmitted
// frame; you could replace this with a counter, some logic
// to change modes based on the GPS time or date, a toggle
// switch, replace the bool with an enum, or disable
// mode-switching altogether.
void modeSwitch()
{
  if (true) {
//    modeFSKCW = !modeFSKCW;
    return;
  }
}





// This function determines timing of transmission frames.
// Return 2 when ready to TX, 1 when it's time to do TX prep, 0 when not time to TX yet
int gpsTxGate(int minute, int second)
{

  // TX trigger:
  // This determines the start time of the beacon transmission itself.
  if (minute % 10 == FRAME_OFFSET_MIN && second == 0)
    return 1;
  else if (debugForceTransmit == true)
    return 1;
  
  // TX prep:
  // This determines the timing of any "prep work" that needs to happen before
  // transmitting a frame (e.g. updating the transmitted message with new sensor
  // data or an updated Maidenhead locator).
  // This triggers 30 seconds before the next frame:
  if (minute % 10 == (FRAME_OFFSET_MIN + 9) % 10 && second == 30)
    return 2;
  else
    return 0;
}





// This runs every time we receive a new NMEA packet from the GPS; any
// GPS-related logic or routine low-frequency input polling (e.g. a
// voltage or temperature sensor for telemetry) could go here.
int gpsLoop()
{

  int currentHour   = gps.time.hour();
  int currentMinute = gps.time.minute();
  int currentSecond = gps.time.second();
  
  // Print time in HH:MM:SS format every 5 seconds.
  // There's definitely a cleaner way to do this, but this
  // way doesn't involve passing char[]s back and forth.
  if (currentSecond % 5 == 0) {
    if (currentHour < 10) { Serial.print("0"); Serial.print(currentHour); }
    else { Serial.print(currentHour); }
    Serial.print(":");
    if (currentMinute < 10) { Serial.print("0"); Serial.print(currentMinute); }
    else { Serial.print(currentMinute); }
    Serial.print(":");
    if (currentSecond < 10) { Serial.print("0"); Serial.println(currentSecond); }
    else { Serial.println(currentSecond); }
  }

  // Transmit or do TX prep at the appropriate time
//  if (gpsTxGate(currentMinute, currentSecond) == 1) {
//    return 1;
//  }
//  else if (gpsTxGate(currentMinute, currentSecond) == 2) {
//    return 2;
//  }
//  else
//    return 0;
  return gpsTxGate(currentMinute, currentSecond);
}







void loop()
{
  
  // Pass NMEA packets to TinyGPS++ for processing
  while (ss.available() > 0)
    gps.encode(ss.read());

  // Check GPS connection on power-up and inform user if we're not getting data,
  // or skip check if debugForceTransmit == false
  // TODO: monitor connection continuously, check for valid GPS fix (>=4 satellites)
  if (gps.charsProcessed() < 10 && millis() > 10000 && gpsConnection == true && !debugForceTransmit) {
    Serial.println("Not receiving packets from GPS;\ncheck connections!");
    gpsConnection = false;
    digitalWrite(LED_GPS, LOW);
  }

  int readyToTxOrPrep = 0; // reinitialize this flag on every loop
  // Every time we get a new NMEA packet, perform GPS-related tasks
  // and check if it's time to transmit or do TX prep:
  if (gps.time.isUpdated() && !debugForceTransmit) {
    gpsConnection = true;
    digitalWrite(LED_GPS, HIGH);
    readyToTxOrPrep = gpsLoop();
    delay(500);
  }

  if (readyToTxOrPrep == 1) {
    Serial.println("Time to transmit!");
    doTx();
    Serial.println("Done transmitting");
    modeSwitch();
    // Reinitialize txMessage after every transmission:
    memset(txMessage, 0, sizeof(txMessage));
  }
  else if (readyToTxOrPrep == 2) {
    Serial.println("Transmit in 30 seconds");
    prepareToTx();
  }
}
