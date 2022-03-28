
#include <si5351.h>
#include <Wire.h>

#include "cw-decoders.h"  // Configuration #defines are over there





// Morse encoding table:
// maps from ASCII chars to an encoded format, which become Morse
// characters after some arithmetical voodoo later on.
// Only used by sendFSKCW(), sendDFCW()
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





// *** TX a dah or a dit when sendFSKCW() (resp. sendDFCW()) asks us to:

// TODO: these REALLY need to be moved into main, to get the awful
// 'extern Si5351' out of modulators.h and move the config variables back.
// **This shouldn't be happening here.**
// Maybe rekerjigger the sendMODE() fns to return an array* of channel symbols,
// then pass that pointer to a unified xmitSymbolsMODE() for each mode?
// Still messy, but...

void dahFSKCW()
{

  Serial.print("dah ");
  si5351.set_freq(FREQ_MARK, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  delay(LEN_DAH_FSKCW);
  digitalWrite(LED_MARK, LOW);
  si5351.set_freq(FREQ_SPACE, SI5351_CLK0);
  delay(LEN_DIT_FSKCW);
}
void ditFSKCW()
{

  Serial.print("dit ");
  si5351.set_freq(FREQ_MARK, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  delay(LEN_DIT_FSKCW);
  digitalWrite(LED_MARK, LOW);
  si5351.set_freq(FREQ_SPACE, SI5351_CLK0);
  delay(LEN_DIT_FSKCW);
}

void dahDFCW()
{

  Serial.print("dah ");
  si5351.set_freq(FREQ_MARK, SI5351_CLK0);
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
  si5351.set_freq(FREQ_SPACE, SI5351_CLK0);
  digitalWrite(LED_MARK, HIGH);
  si5351.output_enable(SI5351_CLK0, 1);  // Re-enable clock output after previous character
  delay(LEN_QTR_SYM_DFCW * 3);
  digitalWrite(LED_MARK, LOW);
  si5351.output_enable(SI5351_CLK0, 0);  // Disable clock output for inter-symbol gap
  delay(LEN_QTR_SYM_DFCW);
}





// *** Decode individual characters into dits and dahs, transmit them:

void sendFSKCW(char c)
{

  int i;
  if (c == ' ') {
    delay(5 * LEN_DIT_FSKCW);
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
