/* 
 *  NB: this is #included in BOTH modulators.cpp and herculine-mept.ino
 */


// *** Frequency definitions.
// DFCW dit, FSKCW 'space' or key-up (hundredths of Hz)
#define FREQ_SPACE 703975000
// DFCW dah, FSKCW 'mark' or key-down (hundredths of Hz)
// Set the bandwidth of your signal here; standard is 5 Hz
#define FREQ_MARK FREQ_SPACE+500
// Oscillator is parked here while not transmitting;
// this improves thermal stability vs. disabling
// the clock output between beacon frames
#define FREQ_STDBY 3000000000

// *** FSKCW parameters.
// Length of one dit (milliseconds); standard is 6000
#define LEN_DIT_FSKCW 6000
// Length of one dah (milliseconds)
#define LEN_DAH_FSKCW 18000

// *** DFCW parameters.
// One DFCW symbol = 3/4 key-down + 1/4 no carrier
// Length of one quarter-symbol (milliseconds); standard is 2500
#define LEN_QTR_SYM_DFCW 2500
// Length of space between letters, *not* including
// the no-carrier at the end of the preceding symbol:
#define LEN_SPC_DFCW 7500
// TODO: make it easier to change the DFCW on/off ratio

// *** Peripherals:

// This line is held HIGH during the beacon's TX cycle, and is used
// to key an external PA or TX/RX switch
#define PTT_OUT 4
// Lit while sending message (and while the external PA or TX/RX switch is triggered)
#define LED_PTT 11
// Lit while 'key is down' (FSK mark or DFCW symbol on)
// This is NOT a PTT line
#define LED_MARK 13
// Lit once we've parsed our first valid time packet from the GPS (stays on)
#define LED_GPS 12



extern Si5351 si5351;  // instantiated in herculine-mept.ino

// these are called into by herculine-mept.ino
void sendFSKCW(char c);
void sendDFCW(char c);
