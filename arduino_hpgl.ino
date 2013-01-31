#include "user.h"
#include "Wire.h"
#include <math.h>
#include <avr/pgmspace.h>  // for progmem stuff

#define DEBUG_enabled 0

#define buttonPin 8

#define ESC "\x1B"

#define cmdBufferSize 255

pState plotterState;
pState noConnectionReturnState;
pWorld plotterWorld;

byte errorCode = 0;
byte statusCode = 0;
int bufferCapacity = 0;

char serInStr[64]; 
char cmd[cmdBufferSize] ="";
char progStrLine[48] = "";
boolean textMessageBuffered;
boolean textMessagePlotted;

unsigned long nextDrawMillis = 0ul;
unsigned long nextPingMillis = 0ul;

union coordinate {
  byte b[4];
  double d;
} 
currentCoordinate;

const double minDistToDraw = 60.0;
double x, y, xOld, yOld;

boolean firstDraw = true;
boolean penDown = false;
boolean wantsToEnd = false;
boolean endingInited = false;

int currentPen = 0;

// A3 plotter space size: 16158 x 11040
int plotterWidth = 16158;
int plotterHeight = 11040;

// system coordinates MAX_INT x MAX_INT
int systemWidth=32767;
int systemHeight=32767;

int textLines = 0;
int margin = 40*10;

/********************** PROGMEM SECTION BELOW **********************/
// long text string can be stored in program memory, and sent to the plotter
// from there as commands or log messages

const char startMessage[] PROGMEM = 
"\n___________________________________________\n"
"\nARDUINO PLOTTER\n"
"version 2.0\n"
"\n"
"ole kristensen 2010\n"
"\n"
"mailto:ole@kristensen.name\n"
"http://ole.kristensen.name\n"
"___________________________________________\n\n"
;

const char textMessage[] PROGMEM =
"http://ole.kristensen.name\n"
"mailto:ole" "\x40" "kristensen.name\n"
;

const char resetPlotterCodes[] PROGMEM =
"\x1B" ".Y\n"
"\x1B" ".R\n"
"\x1B" ".M0;;;13:\n"
"IN;\n"
"SP0;\n"
;

boolean getProgStrLine(const prog_char str[], int lineNo){
  memset(progStrLine, 0, sizeof(progStrLine));
  char c;
  int currentLine = 0;
  byte currentChar = 0;
  if(!str) return false;
  while((c = pgm_read_byte(str++))){
    progStrLine[currentChar] = c;
    currentChar++;
    if(c == '\n'){
      if(currentLine == lineNo) {
        if(currentChar == 1){
          progStrLine[0] = ' ';
        }
        return true;
      } 
      else {
        memset(progStrLine, 0, sizeof(progStrLine));
        currentLine++;
        currentChar = 0;
      }
    }
  }
  if(currentLine == lineNo && currentChar > 0){
    return true;
  }
  if(currentLine < lineNo) {
    return false;
  }
}

void printProgStr(const prog_char str[])
{
  char c;
  if(!str) return;
  while((c = pgm_read_byte(str++)))
    Serial.print(c,BYTE);
}

void logProgStr(const prog_char str[])
{
  Serial.println(ESC ".)");
  printProgStr( str );
  Serial.println(ESC ".(");
}

/********************** MAIN SECTION BELOW **********************/

void setup() {

  // start/stop button
  pinMode(buttonPin, INPUT);

  Serial.begin(9600);

  setState(reset);
}

void loop() {

  if(plotterState == plotting){
    if(digitalRead(buttonPin) == HIGH){
      wantsToEnd = true;
    }
    if(wantsToEnd && !endingInited){
      endingInited = true;
    }

    if(wantsToEnd){
      plotSend();
      Serial.flush();
      Serial.println(";OA;");
      delay(1000);
      if (readSerialString()) {
        while(readSerialString()){
          Serial.flush();
        }
        setState(reset);
      }
    } 
    else {
      if(statusCode < 16) {

        if(millis() > nextDrawMillis){

          /********************** TEXT PLOTTING BELOW **********************/

          if(!textMessageBuffered){
            int textMessageLineNo = 0;
            textLines = 0;
            while(getProgStrLine(textMessage, textMessageLineNo++)){
              writeTextLine(progStrLine);
            }
            textMessageBuffered = true;
          }

          if(!textMessagePlotted){
            plotSend();
            Serial.flush();
            delay(10);
            Serial.println(";OA;");
            nextDrawMillis = millis() + 2000ul; // prevent buffer overflow while waiting
            if (readSerialString()) {
              while(readSerialString()){
                Serial.flush();
              }
              textMessagePlotted = true;
            }
          } 
          else {

            /********************** MAIN DRAWING BELOW **********************/

            setupDrawingWorld();

            double xReal = 0.0;
            double yReal = 0.0;
            double xDist = 0.0;
            double yDist = 0.0;

            if(firstDraw){
              selectPen(1);
              plotPenUp();
              plotPenAt(x,y);
              firstDraw = false;
            }

            penDown = true;

            x=random(-systemWidth/2.0,systemWidth/2.0);
            y=random(-systemHeight/2.0,systemHeight/2.0);

            xDist = x-xOld;
            yDist = y-yOld;

            xDist = abs(xDist);
            yDist = abs(yDist);

            if(xDist > minDistToDraw || yDist > minDistToDraw) {

              xOld = x;
              yOld = y;

              plotPenAt(x,y);

              if(penDown){
                plotPenDown();
              } 
              else{
                plotPenUp();
              }
            }

          }
        } 

      }
      else {
        if(errorCode == 0){
          if(!plotterPing())
            setState(noconnection);
        }
      }
    }
  } 
  else {
    if(errorCode == 0 && plotterState != reset){
      if(millis() > nextPingMillis){
        nextPingMillis = millis() + 2000ul;
        if(!plotterPing())
          setState(noconnection);
      }
    }
  }

  if(errorCode > 15)
    setState(error);

  if(plotterState == reset){
    resetPlotter();
  }

  if(plotterState == waiting){
    if(digitalRead(buttonPin) == HIGH){
        // give feedback, eg by turning on a specific led
      delay(1000);
      startPlotter();
    }
  }
  if(plotterState == noconnection){
    if(plotterPing()){
      plot(";");
      setState(noConnectionReturnState);
    }
    if(digitalRead(buttonPin) == HIGH){
        // give feedback, eg by turning on a specific led
      delay(1000);
      setState(reset);
    }
    delay(100);
  }
  if(plotterState == error){
    if(digitalRead(buttonPin) == HIGH){
        // give feedback, eg by turning on a specific led
      delay(1000);
      errorCode = 0;
      setState(reset);
    }
  }
}

void waitForPlotterBuffer(){
  boolean isBuffering = true;
  while(isBuffering){
    Serial.flush();
    Serial.println(ESC ".B");
    if (readSerialString()) {
      bufferCapacity = atoi(serInStr);
      debugStr(serInStr);
      if(bufferCapacity >= cmdBufferSize+1){
        isBuffering = false;
        setState(plotting);
      } 
      else {
        setState(buffering);
      }
    } 
    else {
      isBuffering = false;
      setState(noconnection);
    }
  }
}

boolean plotterPing(){
  Serial.println(ESC ".Y");
  Serial.flush();
  if (readSerialString()) {
    while(readSerialString()){
      Serial.flush();
    }
  }
  Serial.println(ESC ".O");
  if (readSerialString()) {
    statusCode = atoi(serInStr);
    char msg[128] = "";
    memset( msg, 0, sizeof(msg) );
    strcat(msg, serInStr);
    Serial.flush();
    if (readSerialString()) {
      while(readSerialString()){
        Serial.flush();
      }
    }
    Serial.println(ESC ".B");
    if (readSerialString()) {
      bufferCapacity = atoi(serInStr);
      strcat(msg, "\t");
      strcat(msg, serInStr);
    }
    if(errorCode == 0){
      Serial.flush();
      if (readSerialString()) {
        while(readSerialString()){
          Serial.flush();
        }
      }
      Serial.println(ESC ".E");
      if (readSerialString()) {
        errorCode = atoi(serInStr);
        strcat(msg, "\t");
        strcat(msg, serInStr);
        if (errorCode == 0){
          debugStr(msg);
          return true;
        }
        else{
          logStr(msg);
          return false;
        }
      }
    } 
    else {
      logStr(msg);
      return true;
    }
  } 
  else {
    return false;
  }
}

void resetPlotter() {

  Serial.flush();

  // Welcome message
  logProgStr(startMessage);

  // Reset plotter
  printProgStr(resetPlotterCodes);

  wantsToEnd = false;
  endingInited = false;
  plotterWorld = plotter;
  textMessageBuffered = (DEBUG_enabled == 1); //  skip text
  textMessagePlotted = (DEBUG_enabled == 1);  //  when debugging
  nextPingMillis = 0ul;
  nextDrawMillis = 0ul;
  penDown = false;
  firstDraw = true;
  x = 0.0;
  y = 0.0;

  textLines = 0;

  currentPen = 0;
  setState(waiting);
}

void startPlotter() {
  setState(plotting);
}

void plot(char *str){
  plotSend(strlen(str));
  strcat(cmd, str);
}

void plotln(char *str){
  plotSend(strlen(str)+1);
  strcat(cmd, str);
  strcat(cmd, "\n");
}

void plot(int i){
  char iStr[6] = "";
  itoa(i, iStr, 10);
  plotSend(strlen(iStr));
  strcat(cmd, iStr);
}

void plotln(int i){
  plot(i);
  plotSend(1);
  strcat(cmd, "\n");
}

void plot(long l){
  char lStr[24] = "";
  ltoa(l, lStr, 10);
  plotSend(strlen(lStr));
  strcat(cmd, lStr);
}

void plotln(long l){
  plot(l);
  plotSend(1);
  strcat(cmd, "\n");
}

void plot(double d){
  char dStr[48] = "";
  fmtDouble (d, 5, dStr, 48);
  plotSend(strlen(dStr));
  strcat(cmd, dStr);
}

void plotln(double d){
  plot(d);
  plotSend(1);
  strcat(cmd, "\n");
}

void plot(float f){
  plot((double)f);
}

void plotln(float f){
  plotln((double)f);
}

void plotSend(int len){
  if(strlen(cmd) + len > cmdBufferSize-1){
    plotSend();
  }
}

void plotSend(){
  waitForPlotterBuffer();
  if(plotterState != noconnection){
    Serial.println(cmd);
    memset( cmd, 0, sizeof(cmdBufferSize) );
  }
}

void setState(pState state){
  pState formerState = plotterState;
  plotterState = state;
  unsigned long bufferMillis = 0ul;

  switch (plotterState) {
  case reset:
    // change state indication, eg by turning on a specific led
    delay(500);
    break;
  case waiting:
    if(formerState != plotterState){      
      // change state indication, eg by turning on a specific led
    }
    break;
  case plotting:
    if(formerState != plotterState){      
      // change state indication, eg by turning on a specific led
    }
    break;
  case buffering:
    if(formerState != plotterState){
      // change state indication, eg by turning on a specific led
    }
    bufferMillis = millis()+3000ul;
    while(bufferMillis > millis()){
      if(digitalRead(buttonPin) == HIGH){
        // give feedback, eg by turning on a specific led
        wantsToEnd = true;
      }
    }    

    break;
  case noconnection:
    if(formerState != plotterState){
      noConnectionReturnState = formerState; 
      // change state indication, eg by turning on a specific led
    }
    break;
  case error:
    if(formerState != plotterState){      
      // change state indication, eg by turning on a specific led
    }
    break;
  default: 
    break;
  }
}

//read a string from the serial and store it in an array
//you must supply the array variable
uint8_t readSerialString()
{
  delay(10);

  if(!Serial.available()) {
    return 0;
  }
  delay(10);  // wait a little for serial data

  memset( serInStr, 0, sizeof(serInStr) ); // set it all to zero
  byte i = 0;
  while (Serial.available()) {
    serInStr[i] = Serial.read();   // FIXME: doesn't check buffer overrun
    i++;
  }
  //serInStr[i] = 0;  // indicate end of read string
  return i;  // return number of chars read
}

void logStr(char* str){
  Serial.print(ESC ".)");
  Serial.print(str);
  Serial.println(ESC ".(");
}

void debugStr(char* str){
  if(DEBUG_enabled == 1){
    logStr(str);
  }
}

void writeTextLine(char* text){
  waitForPlotterBuffer();
  setupTextWorld();
  textLines++;
  Serial.print("SP2;PU0,");
  Serial.print((plotterHeight-(margin*2))-(3*40*textLines));
  Serial.print(";DT\n;SI0.1,0.16;LB");
  Serial.print(text);
  Serial.println("\n;");
  setupDrawingWorld();
}

void setupTextWorld(){
  if(plotterWorld != text){
    int x1 = margin+plotterHeight+margin+margin;
    int y1 = margin;
    int x2 = plotterWidth-margin;
    int y2 = plotterHeight-margin;
    Serial.print(";IP");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.print(",");
    Serial.print(x2);
    Serial.print(",");
    Serial.print(y2);
    Serial.print(";");
    Serial.print("SC0,");
    Serial.print(x2-x1);
    Serial.print(",0,");
    Serial.print(y2-y1);
    Serial.print(";");
    plotterWorld = text;
  }
}

void setupDrawingWorld(){
  if(plotterWorld != drawing){
    int x1 = margin;
    int y1 = margin;
    int x2 = plotterHeight-(margin*2);
    int y2 = plotterHeight-(margin*2);
    int radius = systemWidth/2;

    Serial.print("IP");
    Serial.print(x1);
    Serial.print(",");
    Serial.print(y1);
    Serial.print(",");
    Serial.print(x2);
    Serial.print(",");
    Serial.print(y2);
    Serial.print(";");
    Serial.print("SC-");
    Serial.print(radius);
    Serial.print(",");
    Serial.print(radius);
    Serial.print(",-");
    Serial.print(radius);
    Serial.print(",");
    Serial.print(radius);
    Serial.println(";");
    plotterWorld = drawing;
  }
}

void fmtDouble(double val, byte precision, char *buf, unsigned bufLen = 0xffff);
unsigned fmtUnsigned(unsigned long val, char *buf, unsigned bufLen = 0xffff, byte width = 0);

//
// Produce a formatted string in a buffer corresponding to the value provided.
// If the 'width' parameter is non-zero, the value will be padded with leading
// zeroes to achieve the specified width.  The number of characters added to
// the buffer (not including the null termination) is returned.
//
unsigned
fmtUnsigned(unsigned long val, char *buf, unsigned bufLen, byte width)
{
  if (!buf || !bufLen)
    return(0);

  // produce the digit string (backwards in the digit buffer)
  char dbuf[10];
  unsigned idx = 0;
  while (idx < sizeof(dbuf))
  {
    dbuf[idx++] = (val % 10) + '0';
    if ((val /= 10) == 0)
      break;
  }

  // copy the optional leading zeroes and digits to the target buffer
  unsigned len = 0;
  byte padding = (width > idx) ? width - idx : 0;
  char c = '0';
  while ((--bufLen > 0) && (idx || padding))
  {
    if (padding)
      padding--;
    else
      c = dbuf[--idx];
    *buf++ = c;
    len++;
  }

  // add the null termination
  *buf = '\0';
  return(len);
}

//
// Format a floating point value with number of decimal places.
// The 'precision' parameter is a number from 0 to 6 indicating the desired decimal places.
// The 'buf' parameter points to a buffer to receive the formatted string.  This must be
// sufficiently large to contain the resulting string.  The buffer's length may be
// optionally specified.  If it is given, the maximum length of the generated string
// will be one less than the specified value.
//
// example: fmtDouble(3.1415, 2, buf); // produces 3.14 (two decimal places)
//
void
fmtDouble(double val, byte precision, char *buf, unsigned bufLen)
{
  if (!buf || !bufLen)
    return;

  // limit the precision to the maximum allowed value
  const byte maxPrecision = 6;
  if (precision > maxPrecision)
    precision = maxPrecision;

  if (--bufLen > 0)
  {
    // check for a negative value
    if (val < 0.0)
    {
      val = -val;
      *buf = '-';
      *buf++;
      bufLen--;
    }

    // compute the rounding factor and fractional multiplier
    double roundingFactor = 0.5;
    unsigned long mult = 1;
    for (byte i = 0; i < precision; i++)
    {
      roundingFactor /= 10.0;
      mult *= 10;
    }

    if (bufLen > 0)
    {
      // apply the rounding factor
      val += roundingFactor;

      // add the integral portion to the buffer
      unsigned len = fmtUnsigned((unsigned long)val, buf, bufLen);
      buf += len;
      bufLen -= len;
    }

    // handle the fractional portion
    if ((precision > 0) && (bufLen > 0))
    {
      *buf++ = '.';
      if (--bufLen > 0)
        buf += fmtUnsigned((unsigned long)((val - (unsigned long)val) * mult), buf, bufLen, precision);
    }

  }

  // null-terminate the string
  *buf = '\0';

} 

void selectPen(int pen){
  currentPen = pen;
  plot(";SP");
  plotln(pen);
}

void plotPenAt(double theX, double theY){
  plot(";PA");
  plot(theX);
  plot(",");
  plot(theY);
  plotln(";");
}

void plotPenUp(){
  plotln(";PU;");
}

void plotPenDown(){
  plotln(";PD;");
}

