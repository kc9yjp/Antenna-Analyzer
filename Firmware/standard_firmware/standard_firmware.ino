// modified by Bill Dornbush KM6BYD to use Mode button to select modes, like PC mode does
// modified by KJ6WEX to plot the VSWR and detecting BAND button sooner, and more
//
// This file is part of the K6BEZ Antenna Analyzer project.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

// include the library code:
#include <LiquidCrystal.h>

// * LCD RS pin to digital pin 8
// * LCD Enable pin to digital pin 9
// * LCD D4 pin to digital pin 15
// * LCD D5 pin to digital pin 14
// * LCD D6 pin to digital pin 16
// * LCD D7 pin to digital pin 10

// initialize the library with the numbers of the interface pins
LiquidCrystal lcd(8, 9, 15, 14, 16, 10);

#define FQ_UD 4
#define SDAT 3
#define SCLK 5
#define RESET 2
#define MODE A2
#define BAND A3

#define LCD_DELAY 15

#define DEBUG (false) // Set to true to enable debugging console output


long Fstart = 1000000;  // Start Frequency for sweep
long Fstop = 30000000;  // Stop Frequency for sweep
unsigned long current_freq; // Temp variable used during sweep
long serial_input_number; // Used to build number from serial stream
long num_steps = 1001; // Number of steps to use in the sweep
char incoming_char; // Character read from serial stream
byte mode_pressed = 0; //indicates that mode switch was pressed
int mode = 1; // mode setting selected
byte band_pressed = 0;
int band = 1;
const long buttonWait = 5000; // Length of time in milliseconds to wait for user to press the button again
unsigned long lastPressTime;
unsigned long currentTime;

// Graph variables

int plotValue[36]; // Values to be plotted along the vertical axis. There are 35 columns, but the counter includes 36 values
double storedVSWR[36]; // Raw VSWR values to be plotted
int colGraph = 0; // Calculate horizontal pixel position within the 8-character graph
int hChar = 0; // Determine which character position contains this data point
int colChar = 0; // Column within that character gets this data point
int oldColGraph = 99; // Bogus starting value will force initial calculation
int oldhChar = 0;
boolean recalculate = true;

double VSWR;
double totalVSWR = 0.0;
double avgVSWR = 0.0;
int readingsCount = 0;
double minVSWR = 999.0;
double maxVSWR = 1.0;
double minDisplayedVSWR = 999.0;
double maxDisplayedVSWR = 1.0;
long minFreq;
long maxFreq;

byte grafChar[8] = { // this is the array to feed to custom HC44780. Initialize as empty (nothing is displayed.)
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000,
  B00000
};

void setup() {
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  delay(LCD_DELAY);

  if (!DEBUG) {
    // Print a message to the LCD.
    showTitleScreen("Antenna Analyzer");
    delay(3000);
  }

  // Configiure DDS control pins for digital output
  pinMode(FQ_UD, OUTPUT);
  pinMode(SCLK, OUTPUT);
  pinMode(SDAT, OUTPUT);
  pinMode(RESET, OUTPUT);

  // Set up analog inputs on A0 and A1, internal reference voltage
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  analogReference(DEFAULT);

  // initialize serial communication at 57600 baud
  Serial.begin(115200);

  // Reset the DDS
  digitalWrite(RESET, HIGH);
  digitalWrite(RESET, LOW);

  //Initialise the incoming serial number to zero
  serial_input_number = 0;
  lcd.clear();
  lcd.setCursor(0, 0);
  delay(LCD_DELAY);
  lcd.print("1-30 MHz");

  lastPressTime = millis();

}

void loop() {
  //Check for character
  if (Serial.available() > 0) {
    mode = 0;
    band = 0;
    lcd.clear();
    lcd.setCursor(14, 0);
    delay(LCD_DELAY);
    lcd.print("PC");
    incoming_char = Serial.read();
    switch (incoming_char) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        serial_input_number = serial_input_number * 10 + (incoming_char - '0');
        break;
      case 'A':
        //Turn frequency into FStart
        Fstart = serial_input_number;
        serial_input_number = 0;
        break;
      case 'B':
        //Turn frequency into FStop
        Fstop = serial_input_number;
        serial_input_number = 0;
        break;
      case 'C':
        //Turn frequency into FStart and set DDS output to single frequency
        Fstart = serial_input_number;
        SetDDSFreq(Fstart);
        serial_input_number = 0;
        break;
      case 'N':
        // Set number of steps in the sweep
        num_steps = serial_input_number;
        serial_input_number = 0;
        break;
      case 'S':
      case 's':
        Perform_sweep();
        break;
      case '?':
        // Report current configuration to PC
        Serial.print("Start Freq:");
        Serial.println(Fstart);
        Serial.print("Stop Freq:");
        Serial.println(Fstop);
        Serial.print("Num Steps:");
        Serial.println(num_steps);
        break;
    }
    Serial.flush();
  } else {
    //No serial data was received
    lcd.blink();
    currentTime = millis();
    if (currentTime - lastPressTime >= buttonWait) {
      lcd.noBlink();
      if (band > 0) Perform_sweep();
    }
  }
  if ((digitalRead(BAND) == LOW) or (band_pressed == 1)) {
    while (digitalRead(BAND) == LOW) {} // Wait for the BAND button to be released for debouncing
    lastPressTime = millis();

    // Initialize variables for new band selection
    totalVSWR = 0.0;
    avgVSWR = 0.0;
    readingsCount = 0;
    minVSWR = 999.0;
    maxVSWR = 1.0;
    minDisplayedVSWR = 999.0;
    maxDisplayedVSWR = 1.0;
    minFreq = Fstart;
    maxFreq = Fstart;

    band_pressed = 0;
    band += 1;
    if (band == 12) band = 1;
    num_steps = 1000;
    switch (band) {
      case 1:
        // Full sweep 1-30 MHz
        lcd.clear();
        delay(LCD_DELAY);
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("1-30 MHz");
        Fstart = 1000000;
        Fstop = 30000000;
        break;
      case 2:
        // 160m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("160m");
        Fstart = 1500000;
        Fstop = 2300000;
        break;
      case 3:
        // 80m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("80m");
        Fstart = 2000000;
        Fstop = 5000000;
        break;
      case 4:
        // 60m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("60m");
        Fstart = 5000000;
        Fstop = 6000000;
        break;
      case 5:
        // 40m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("40m");
        Fstart = 6000000;
        Fstop = 8000000;
        break;
      case 6:
        // 30m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("30m");
        Fstart = 9000000;
        Fstop = 11000000;
        break;
      case 7:
        // 20m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("20m");
        Fstart = 13000000;
        Fstop = 16000000;
        break;
      case 8:
        // 17m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("17m");
        Fstart = 17000000;
        Fstop = 19000000;
        break;
      case 9:
        // 15m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("15m");
        Fstart = 20000000;
        Fstop = 23000000;
        break;
      case 10:
        // 12m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("12m");
        Fstart = 24000000;
        Fstop = 26000000;
        break;
      case 11:
        // 10m
        lcd.clear();
        lcd.setCursor(0, 0);
        delay(LCD_DELAY);
        lcd.print("10m");
        Fstart = 27000000;
        Fstop = 30000000;
        break;

    }

  }

  if ((digitalRead(MODE) == LOW) or (mode_pressed == 1)) {  //mode switch was pressed
    while (digitalRead(MODE) == LOW) {}  //Wsit for MODE button to be released for debouncing
    lastPressTime = millis();

    //Initialize variables for new mode selection
    totalVSWR = 0.0;
    avgVSWR = 0.0;
    readingsCount = 0;
    minVSWR = 999.0;
    maxVSWR = 1.0;
    minDisplayedVSWR = 999.0;
    maxDisplayedVSWR = 1.0;

    //display switch use help
    lcd.clear();
    lcd.setCursor(0, 1);
    delay(LCD_DELAY);
    lcd.print("BAND +1,MODE nxt");
    
    //get frequency start
    lcd.setCursor(0, 0);
    delay(LCD_DELAY);
    lcd.print("FSTART=");
    Fstart = getNumber(7, 0, 8);
    
    // get frequence stop
    lcd.setCursor(0, 0);
    delay(LCD_DELAY);
    lcd.print("FSTOP =");
    Fstop = getNumber(7, 0, 8);
    if (Fstop < Fstart) {
      Fstop = Fstart;  //stop frequency must be not less than start frequency
    }
    
    // get number of steps
    lcd.setCursor(0, 0);
    delay(LCD_DELAY);
    lcd.print("NSTEPS=");
    num_steps = getNumber(7, 0, 4);
    if (num_steps < 1) {
      num_steps = 1;  //num_steps must be greater than 0
    }
    
    // display from and to frequencies
    lcd.clear();
    lcd.setCursor(0, 0);
    delay(LCD_DELAY);
    String dispFreq = String(Fstart / 1000000.0, 1);
    lcd.print(dispFreq);
    lcd.print("-");
    dispFreq = String(Fstop / 1000000.0, 1);
    lcd.print(dispFreq);

    // now sweep with the input parameters
    if DEBUG {
      Serial.print("Fstart=");
      Serial.println(Fstart);
      Serial.print("Fstop=");
      Serial.println(Fstop);
      Serial.print("num_steps=");
      Serial.println(num_steps);
    }
    
    Perform_sweep();
  }

}

void Perform_sweep() {
  int FWD = 0;
  int REV = 0;
  int REV_nosig = 0;
  int FWD_nosig = 0;
  String dispFreq;
  String minString;
  long Fstep = (Fstop - Fstart) / num_steps;

  clearChar();
  clearCustomChar(0, 7); // erase all custom chars in LCD's memory

  // Reset the DDS
  digitalWrite(RESET, HIGH);
  delay(1);
  digitalWrite(RESET, LOW);
  delay(10);
  SetDDSFreq(Fstart);
  delay(100);

  // Start loop
  for (long i = 0; i <= num_steps; i++) {
    // Calculate current frequency
    current_freq = Fstart + i * Fstep;

    // Set DDS to current frequency
    SetDDSFreq(current_freq);
    // Wait a little for settling
    if (digitalRead(BAND) == LOW) {
      band_pressed = 1;
      i = num_steps;  // Force the sweep to end when user presses the BAND button
    }
    delay(10);
    // Read the forawrd and reverse voltages
    REV = analogRead(A0);
    FWD = analogRead(A1);

    if (REV >= FWD) {
      // To avoid a divide by zero or negative VSWR then set to max 999
      VSWR = 999;
    } else {
      // Calculate VSWR
      VSWR = ((double)FWD + (double)REV) / ((double)FWD - (double)REV);
    }

    if (VSWR <= minVSWR)
    {
      minFreq = current_freq;
      minVSWR = VSWR;
    }

    if (VSWR >= maxVSWR)
    {
      maxFreq = current_freq;
      maxVSWR = VSWR;
    }

    // Graph the results
    totalVSWR += VSWR;
    readingsCount++;

    colGraph = (i * 35) / num_steps; // Calculate horizontal pixel position within the entire 7-character graph (35 total pixels wide)
    if (oldColGraph != colGraph) {
      oldColGraph = colGraph;
      avgVSWR = totalVSWR / (double)readingsCount;  // Get the average VSWR since last plotted value
      storedVSWR[colGraph] = avgVSWR;
      if (avgVSWR < minDisplayedVSWR) {
        minDisplayedVSWR = avgVSWR;
        recalculate = true;
      }
      if (avgVSWR > maxDisplayedVSWR) {
        maxDisplayedVSWR = avgVSWR;
        recalculate = true;
      }
      
      // hwd
      if (maxDisplayedVSWR > 3)
      {
        maxDisplayedVSWR = 3;
      }
      
      if (DEBUG) {
        Serial.print(colGraph);  // DEBUG
        Serial.print("\t");  // DEBUG
        Serial.print(minDisplayedVSWR);  // DEBUG
        Serial.print("\t");  // DEBUG
        Serial.print(avgVSWR);  // DEBUG
        Serial.print("\t");  // DEBUG
        Serial.print(maxDisplayedVSWR);  // DEBUG
        Serial.print("\t");  // DEBUG
        Serial.println(recalculate);  // DEBUG
      }
      // Calculate the vertical pixel value for this reading. If recalculate is true, recalculate the historical values, too, as min or max has changed
      plotValue[colGraph] = getHeight(avgVSWR, minDisplayedVSWR, maxDisplayedVSWR); // Value to be plotted along the vertical axis
      if (recalculate) {
        if (DEBUG) {
          Serial.print("====== Recalculating Begin ======\n");
        }
        for (int j = 0; j <= colGraph; j++) {
          plotValue[j] = getHeight(storedVSWR[j], minDisplayedVSWR, maxDisplayedVSWR); // Recalculate values to be plotted along the vertical axis
          if (DEBUG) {
            Serial.print(j);  // DEBUG
            Serial.print("\t");  // DEBUG
            Serial.print(minDisplayedVSWR);  // DEBUG
            Serial.print("\t");  // DEBUG
            Serial.print(storedVSWR[j]);  // DEBUG
            Serial.print("\t");  // DEBUG
            Serial.print(maxDisplayedVSWR);  // DEBUG
            Serial.print("\t");  // DEBUG
            Serial.println(plotValue[j]);  // DEBUG
          }
        }
        if (DEBUG) {
          Serial.print("====== Recalculating End ======\n");
        }
      }

      totalVSWR = 0.0;  // Reset values for next plotted average
      readingsCount = 0;

      if (recalculate) {
        for (int j = 0; j <= colGraph; j++) {
          hChar = j / 5; // Determine which character position contains this data point
          if (hChar != oldhChar) {
            oldhChar = hChar;
            clearChar();
          }
          colChar = j % 5;  // Determine which column within that character gets this data point

          createPoint(plotValue[j], colChar, hChar);
        }
        recalculate = false;
      }
      else {
        hChar = colGraph / 5; // Determine which character position contains this data point
        if (hChar != oldhChar) {
          oldhChar = hChar;
          clearChar();
        }
        colChar = colGraph % 5;  // Determine which column within that character gets this data point

        createPoint(plotValue[colGraph], colChar, hChar);
      }

      lcd.home();
      lcd.setCursor(9, 0);

      for (byte j = 0; j < 7; j++) {
        lcd.write(byte(j));
      }
    }

    if (band == 0) {
      // Send current line back to PC over serial bus
      Serial.print(current_freq);
      Serial.print(",");
      Serial.print(long(VSWR * 1000)); // This *1000 is to make the system compatible with the PIC version
      Serial.print(",");
      Serial.print(FWD);
      Serial.print(",");
      Serial.println(REV);
    }
  }
  // Send "End" to PC to indicate end of sweep
  if (band == 0) {
    Serial.println("End");
    Serial.print("Freq ");
    Serial.print(minFreq);
    Serial.print(", VSWR ");
    Serial.println(minVSWR);
    Serial.flush();
  }

  lcd.setCursor(15, 1);
  lcd.print(".");
  delay(100);
  lcd.setCursor(15, 1);
  lcd.print(" ");
  lcd.setCursor(0, 1);
  dispFreq = String(minFreq / 1000000.0, 6);
  lcd.print(dispFreq);
  lcd.print(",");
  minString = String(minVSWR, 1);
  lcd.print(minString);
  lcd.print(":1    ");

  digitalWrite(13, HIGH);
  //delay(10);
  digitalWrite(13, LOW);
}

void SetDDSFreq(long Freq_Hz) {
  // Calculate the DDS word - from AD9850 Datasheet
  int32_t f = Freq_Hz * 4294967295 / 125000000;
  // Send one byte at a time
  for (int b = 0; b < 4; b++, f >>= 8) {
    //    SPI.transfer(f & 0xFF);
    send_byte(f & 0xFF);
  }
  // 5th byte needs to be zeros
  //SPI.transfer(0);
  send_byte(0);
  // Strobe the Update pin to tell DDS to use values
  digitalWrite(FQ_UD, HIGH);
  digitalWrite(FQ_UD, LOW);
}

void send_byte(byte data_to_send) {
  // Bit bang the byte over the SPI bus
  for (int i = 0; i < 8; i++, data_to_send >>= 1) {
    // Set Data bit on output pin
    digitalWrite(SDAT, data_to_send & 0x01);
    // Strobe the clock pin
    digitalWrite(SCLK, HIGH);
    digitalWrite(SCLK, LOW);
  }
}

void showTitleScreen (String titleText) {
  lcd.clear();
  delay(LCD_DELAY);
  lcd.setCursor(16, 0);
  delay(LCD_DELAY);
  lcd.autoscroll();
  delay(LCD_DELAY);
  // Scroll the title text:
  for (int thisChar = 0; thisChar < titleText.length(); thisChar++) {
    lcd.print(titleText[thisChar]);
    delay(250);
  }
  // turn off automatic scrolling
  lcd.noAutoscroll();
}

// function to get graph point height
int getHeight ( float vswrNow , float minVSWR , float maxVSWR ) { // takes measurements , minimum and maximum VSWR and reurns a value from 0 to 7
  if (vswrNow <= minVSWR) return 0;  // lowest graph point
  if (vswrNow >= maxVSWR) return 7; // highest graph point
  if (vswrNow > minVSWR & vswrNow < maxVSWR) {
    return (vswrNow - minVSWR) * 8 / (maxVSWR - minVSWR); // here is the way values from 1 to 6 are calculated
  }
}

// function to make graph point in hc 77480 special characters
void createPoint (int row, int col, int hcchar) { // takes as input the row(height) , the column of custom char that it must be set, and custom character memory position at LCD.
  row = 7 - row; // lcd character 0 is top left so invert coordinates
  col = 4 - col; // lcd character 0 is top left so invert coordinates
  while (row < 8 ) {
    bitSet(grafChar[row], col ) ; // this loop sets to 1 the bit of the needed pixel and all below that at grafChar bitarray so as to display a bar
    row++;
  }
  lcd.createChar(hcchar, grafChar); // write to custom character the contents of grafChar array.
}

// function to set clear custom graph chararacter matrix (not an independent function, just to make loop code neater
void clearChar() { // a quick way to set all grafChar bits back to zero so as to creat a new one.
  int countj = 0;
  while (countj < 8) {
    grafChar[countj] = B00000;
    countj++;
  }
  return;
}

// function to clear range of LCD custom characters. Needs graph char to be set to nothing first (clearchar)
void clearCustomChar (int first , int second) { // clear the custom character LCD memory at the range given at function input.
  int counter = first;
  while (counter < second + 1) {
    lcd.createChar(counter, grafChar);
    counter++;
  }
  return;
}

// function to get number from switches and display on LCD
// assumes that BAND increments digit, MODE increments to next digit
// assumes that number should be in format nn.nnnnnn if there are 8 digits
// otherwise format is 0000
// colStart = column to start displaying the number
// rowStart = row to start displaying the number
// ndigits = number of digits in the number
//returns a long value
long getNumber(int colStart, int rowStart, int ndigits) {
  long returnNumber = 0;  //number to be returned
  int showDigit = colStart;  //current digit to display
  int inloop = 1;  //while loop exit value
  int cdigit = 0;  //current digit to be dieplayed and incremented
  // initialize display
  lcd.setCursor(colStart, rowStart);
  lcd.print("         ");

  for (int idigit = 0; idigit < ndigits; idigit++) {  //loop for all digits
    inloop = 1;  //initialize while exit value
    cdigit = 0;  //current digit to be displayed and incremented
    lcd.setCursor(showDigit, rowStart);
    lcd.noCursor();
    lcd.noBlink();
    lcd.print("0");
    // get a digit and display
    while (inloop == 1) {
      if (digitalRead(MODE) == LOW) {
        while (digitalRead(MODE) == LOW) { delay(50); }  //debounce
        // MODE pressed so go to next digit
        inloop = 0;  //exit the while loop
        lcd.setCursor(showDigit, rowStart);
        lcd.noCursor();
        lcd.noBlink();
        lcd.print(cdigit);
        if (idigit < ndigits-1) {  //increment digit if not the last time
          returnNumber = returnNumber * 10;
        }
        showDigit++;
        if (showDigit == colStart+2 && ndigits == 8) {  //print decimal point for MHz
          lcd.print(".");
          showDigit++;
        }
        lcd.print("0");  //print the initial value for the next digit
      }
      if (digitalRead(BAND) == LOW) {
        while (digitalRead(BAND) == LOW) { delay(50); } //debounce
        // BAND pressed, to increment digit
        returnNumber++;
        cdigit++;
        lcd.setCursor(showDigit, rowStart);
        lcd.noCursor();
        lcd.noBlink();
        lcd.print(cdigit);
      }
      delay(LCD_DELAY);
    }
  }
  return returnNumber;
}
