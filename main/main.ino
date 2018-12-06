#include <MinimumSerial.h>
#include <SdFatConfig.h>
#include <SdFat.h>
#include <SysCall.h>
#include <BlockDriver.h>
#include <FreeStack.h>

#include <Adafruit_VS1053.h>
#include <I2S.h>
#include <Adafruit_NeoPixel.h>

const int RESET_BUTTON = A3;
const int SOUND_BUTTON = A4;
const int LIGHT_BUTTON = A5;
const int DONE_PIN = A2;
const int LED_PIN = 12;
const int NUM_LEDS = 32;

Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
#define VS1053_RESET   -1
#define VS1053_CS       6     // VS1053 chip select pin (output)
#define VS1053_DCS     10     // VS1053 Data/command select pin (output)
#define CARDCS          5     // Card chip select pin
// DREQ should be an Int pin *if possible* (not possible on 32u4)
#define VS1053_DREQ     9     // VS1053 Data request, ideally an Interrupt pin


#define SECOND 1000

Adafruit_VS1053_FilePlayer musicPlayer = 
  Adafruit_VS1053_FilePlayer(VS1053_RESET, VS1053_CS, VS1053_DCS, VS1053_DREQ, CARDCS);

SdFat SD; //VS1053 lib requires a global SD object because it uses the old SD lib

char * * patternFiles = NULL; //expected to be found at SD://patterns/
char * * soundFiles = NULL; //expected to be found at SD://
int numPatternFiles;
int numSoundFiles;

int interval;
int runIndex;
int brightness;
int loudness;

int volume = 75;

bool soundGo = false;
bool lightsGo = false;
long loopCount = 0;

struct pattern
{
  int length;
  char lines[50][32];
};

void setup() {
  strip.begin();

  setAllLights(0, 0, 0);
  pinMode(RESET_BUTTON, INPUT_PULLUP);
  pinMode(SOUND_BUTTON, INPUT_PULLUP);
  pinMode(LIGHT_BUTTON, INPUT_PULLUP);
  pinMode(DONE_PIN, OUTPUT);
  
  
  randomSeed(analogRead(0));

   if (digitalRead(RESET_BUTTON) == HIGH) {
    configuration();
    
  } 
//  char buff[32];
//  getProp("interval", buff, 32);
//  interval = atoi(buff);
//
//  if (0 == interval) {
//    done();
//  }

  
   
//   Serial.begin(9600);
//   while ( ! Serial ) { delay( 1 ); }
//
//  Serial.println("setup!!!");

//  interval = getProp("interval").toInt();
//  Serial.println(interval);
//  setStatusLights(255, 0, 0, interval);
//  runIndex = getProp("index").toInt();
//
//   if (0 == interval) {
//    done();
//   }

//  if (shouldGo()) {
//    go();
//  } else {
//    if (interval != 0) {
//      runIndex = runIndex + 1 % interval;
//      setProp("index", String(runIndex));
//    }
//    done();
//  }

  
  
  if (! musicPlayer.begin()) { // initialise the music player
     Serial.println(F("Couldn't find VS1053, do you have the right pins defined?"));
     while (1);
  }

  delay(1000);
  musicPlayer.sineTest(0x44, 500);    // Make a tone to indicate VS1053 is working
  if (!SD.begin(CARDCS)) {
    Serial.println(F("SD failed, or not present"));
    while (1);  // don't do anything more
  }
  else {
    SD.ls();
  }
  Serial.println("SD OK!");
  
  blinkAllLights (0, 255, 0, 500);

  musicPlayer.setVolume(2,2);
  musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);  // DREQ int
  
  

  loadSoundAndPatternNames ();
 
  go();
  
  
}

void loop() {
  //do nothing!
}

void go() {
  //loudness = getProp("sound").toInt();
  musicPlayer.setVolume(2,2);//100 -(loudness*14),100 -(loudness*14));
 // brightness = getProp("light").toInt();
  strip.setBrightness(200);//36*brightness);

  int soundIndex = -1;
  int patternIndex = -1;
  unsigned long actionStart;

  struct pattern myPattern;
      
  
  patternIndex = random(0,numPatternFiles);
  if (!loadPattern (patternFiles[patternIndex], myPattern))
  {
     
  }
   
  soundIndex = random(0, numSoundFiles);
  
  if (!musicPlayer.startPlayingFile(soundFiles[soundIndex]))
  {
     
  }

  actionStart = millis();

  while (!musicPlayer.stopped() && 15 * SECOND >= millis() - actionStart)
  {
    playPattern(myPattern);
  }

  if (!musicPlayer.stopped())
  {
    musicPlayer.stopPlaying();
  }
  
  //setProp("index", String(0));

  done();

}

void done() {
   while (1) {
      digitalWrite(DONE_PIN, HIGH);
      delay(1);
      digitalWrite(DONE_PIN, LOW);
      delay(1);
    }
}

void setAllLights (int r, int g, int b) {
  for (int j = 0; j < NUM_LEDS; j++) {
    strip.setPixelColor(j, r, g, b); //pixel num, r, g, b
  }
  strip.show();
}

void setStatusLights(int r, int g, int b, int level) {
  for (int i = 0; i < 4; i++) {  
    for (int j = 0; j < level; j++) {
      strip.setPixelColor((i*NUM_LEDS/4) + j, r, g, b); //pixel num, r, g, b
    }
  }
  strip.show();
}

void blinkAllLights (int r, int g, int b, long dur) {
  setAllLights(r, g, b);
  delay(dur);
  setAllLights(0, 0, 0);
}

bool shouldGo() {
  char buff1[32];
  char buff2[32];
  getProp("interval", buff1, 32);
  interval = atoi(buff1);
  getProp("index", buff2, 32);
  runIndex = atoi(buff2);;

  return interval == runIndex;
}

void configuration() {
  unsigned long start = millis();
  bool done = false;

  while (millis() - start < 5 * SECOND) {
    if (digitalRead(LIGHT_BUTTON) == LOW) {
      configLights();
      done = true;
    } else if (digitalRead(SOUND_BUTTON) == LOW) {
      configSound();
      done = true;
    }
  }

  if (!done) {
    configInterval();
  }
}

void configInterval() {
  blinkAllLights(255, 0, 0, 500);
  char buff[32];
  getProp("interval", buff, 32);
  interval = atoi(buff);
  
  setStatusLights(255, 0, 0, interval);
  
  unsigned long start = millis();
  while (HIGH == digitalRead(RESET_BUTTON)) {
    if (millis() - start >= 3 * SECOND) {
      interval += 1 % 8;
      setAllLights(0, 0, 0);
      sprintf(buff, "%d\n", interval);
      setProp("interval", buff);
      setStatusLights(255, 0, 0, interval);
      start = millis();
    }
  }

  blinkAllLights(255, 0, 0, 500); //probably won't run
}

void configLights() {
  unsigned long start = millis();
  blinkAllLights(0, 255, 255, 500);
  char buff[32];
  getProp("light", buff, 32);
  brightness = atoi(buff);
  setStatusLights(0, 255, 255, brightness);
  
  while (millis() - start < 5 * SECOND) {
    if (digitalRead(LIGHT_BUTTON) == LOW) {
      start = millis();
      brightness = (brightness + 1) % 8;
      setAllLights(0, 0, 0);
      setStatusLights(0, 255, 255, brightness);
      start = millis();
    }
  }

  sprintf(buff, "%d\n", brightness);
  setProp("light", buff);
  blinkAllLights(0, 255, 255, 500);
}

void configSound() {
  unsigned long start = millis();
  blinkAllLights(0, 255, 0, 500);
  char buff[32];
  getProp("sound", buff, 32);
  loudness = atoi(buff);
  setStatusLights(0, 255, 0, loudness);

  while (millis() - start < 5 * SECOND) {
    if (digitalRead(SOUND_BUTTON) == LOW) {
      start = millis();
      loudness = (loudness + 1) % 8;
      setAllLights(0, 0, 0);
      setStatusLights(0, 255, 0, loudness);
      start = millis();
    }
  }

  sprintf(buff, "%d\n", loudness);
  setProp("sound", buff);
  blinkAllLights(0, 255, 0, 500);
}

void getProp(char * key, char * buff, unsigned len) {
  SdFile myFile;
  char fullName[26];
  int index;
  sprintf (fullName, "%s.prp", key);
  SD.chdir ();
  if (myFile.open (fullName))
  {
    while (myFile.available())
    {
      index = 0;
      char ch;
      memset(buff, 0, len);
      do {
        ch = myFile.read();
        buff[index++] = ch;
      }while ('\n' != ch && index < len);
      
     
      buff[index - 1] = '\0';  
    }
    
    myFile.close ();
  }
}

void setProp(char * key, char * val) {
  SdFile myFile;
  char fullName[26];
  char buff[32];
  SD.chdir ();
  sprintf (fullName, "%s.prp", key);

  if (myFile.open (fullName, FILE_WRITE))
  {
    myFile.print(val);
    
    myFile.close ();
  }
}

void loadSoundAndPatternNames ()
{
  SdFile myFile;
  char buff[16];
  int index;
  
  numSoundFiles = 0;
  numPatternFiles = 0;
  
  //count entries on SD card
  SD.chdir ();
  while (myFile.openNext(SD.vwd(), O_READ))
  {
    if (!myFile.isSubDir() && !myFile.isHidden())
    {
      myFile.getSFN (buff);
      upperCase (buff, 16);

      if (NULL != strstr(buff, ".MP3"))
      {
        numSoundFiles++;
      }
    }
    
    memset(buff, 0, 16);
    myFile.close();
  }

  numSoundFiles--; //not including sound debugging song
  
  // get file names

  SD.chdir ();
  if (soundFiles)
  {
    free (soundFiles);
  }
  
  soundFiles = (char **) malloc (numSoundFiles * sizeof(char *));
  for (int i = 0; i < numSoundFiles; i++)
  {
    soundFiles[i] = (char *) malloc (16 * sizeof (char));
  }
  index = 0;

  //Serial.print ("sound files: ");
  //Serial.println (numSoundFiles);

  while (myFile.openNext(SD.vwd(), O_READ))
  {
    if (!myFile.isSubDir() && !myFile.isHidden())
    {
       myFile.getSFN (buff);
       upperCase (buff, 16);
       
       //Debugging song, don't worry about it.
       if (0 != strcmp (buff, "SEAN.MP3") && NULL != strstr(buff, ".MP3"))
       {  
           strncpy (soundFiles[index++], buff, 16);
           //Serial.println(String(buff) + " is available to play");
       }
    }
    
    myFile.close();
  }

  SD.chdir("/patterns");
  
  while (myFile.openNext(SD.vwd(), O_READ))
  {
    if (!myFile.isSubDir() && !myFile.isHidden())
    {
      numPatternFiles++; 
    }
    
    myFile.close();
  }

//    Serial.print ("pattern files: ");
//  Serial.println (numPatternFiles);

  SD.chdir("/patterns");

  if (patternFiles)
  {
    free(patternFiles);
  }
  
  patternFiles = (char **) malloc (numPatternFiles *sizeof(char *));
  for (int i = 0; i < numPatternFiles; i++)
  {
    patternFiles[i] = (char *) malloc (16 * sizeof (char));
  }
  index = 0;
  
  // get file names
  while (myFile.openNext(SD.vwd(), O_READ))
  {
    if (!myFile.isSubDir() && !myFile.isHidden())
    {
      myFile.getSFN (buff);

      strncpy (patternFiles[index++], buff, 16);
      //Serial.println(String(buff) + " is available to use");

    }
    
    myFile.close();
  }
  
  SD.chdir();
  
}

boolean loadPattern (char * fileName, struct pattern &myPattern)
{
  boolean success = true;
  SdFile myFile;
  char fullName[26];
  char buff[32];
  int index;
  
  myPattern.length = 0;
  
  sprintf (fullName, "/patterns/%s", fileName);

  if (myFile.open (fullName))
  {
    while (myFile.available() && myPattern.length < 100)
    {
      index = 0;
      memset(buff, 0, sizeof(buff));
      while ('\n' != (buff[index++] = myFile.read()) && index < 32);
     
      buff[index - 1] = '\0';
      
      strncpy (myPattern.lines[myPattern.length++], buff, 32);
      //Serial.println (myPattern.lines[myPattern.length - 1]);
    }
    
    myFile.close ();
  }
  else
  {
    success = false;
  }
  
  return success;
}

boolean playPattern (struct pattern myPattern)
{
  char effect[10];
  char buff[32];
  byte row = 0;
  byte r, g, b;
  unsigned long duration;
  char * str;
  char * i;
  int index = 0;
  boolean success = true;
  byte line = 0;

  while (index < myPattern.length)
  {
    str = strtok_r (myPattern.lines[index], " ", &i);
  
    if (4 != strlen(str))
    {
      row = B1111;
    }
    else
    {
      for (int i = 0; i < 4; i++)
      {
        row <<= 1;
        row |= ('1' == str[i] ? 0x1 : 0x0);
      } 
    }

    
    str = strtok_r (NULL, " ", &i);
    strncpy (effect, str, 6);
    
    str = strtok_r (NULL, " ", &i);
    r = atoi (str);
    
    r = r < 0 ? 0: r;
    r = r > 255 ? 255: r;
    
    str = strtok_r (NULL, " ", &i);
    g = atoi (str);
    
    g = g < 0 ? 0: g;
    g = g > 255 ? 255: g;
    
    str = strtok_r (NULL, " ", &i);
    b = atoi (str);
    
    b = b < 0 ? 0: b;
    b = b > 255 ? 255: b;
    
    str = strtok_r (NULL, " ", &i);
    duration = atoi (str);
    
    duration = duration < 1 ? 1: duration;
    duration = duration > 9999 ? 9999: duration;      
    
    //only supports three effects at the moment
    if (0 == strcmp(effect, "flash"))
    {
      // turns LEDs on then off
      ledFlash (row, r, g, b, duration);
    }
    else if (0 == strcmp (effect, "wave"))
    {
      // activates the lights on a chip one at a time from top to bottom
      ledWave (row, r, g, b, duration);
    }
    else if (0 == strcmp (effect, "alt"))
    {
      // alternates colors rapidly. Alternates with the color used during the
      // last call
      ledAlternate (row, r, g, b, duration);
    }
    else
    {
       //default: all white for duration
       blinkAllLights (255, 255, 255, duration);
    }
    
    index++;
  }
  
  return success;
}

void ledWave (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  unsigned long start = millis ();

  while (duration >= millis () - start)
  {
    for (int j = 0; j < NUM_LEDS / 4; j++) {
      if (leds & 0x1)
      {
        strip.setPixelColor (j, r, g, b);
      }
      
      if (leds & 0x2)
      {
        strip.setPixelColor((NUM_LEDS/4) + j, r, g, b);
      }
      
      if (leds & 0x4)
      {
        strip.setPixelColor((NUM_LEDS/2) + j, r, g, b);
      }
      
      if (leds & 0x8)
      {
        strip.setPixelColor ((3*NUM_LEDS/4) + j, r, g, b); 
      }
      
      strip.show ();
      
      delay (50);
      
      strip.setPixelColor (j, 0, 0, 0);
      strip.setPixelColor((NUM_LEDS/4) + j, 0, 0, 0);
      strip.setPixelColor((NUM_LEDS/2) + j, 0, 0, 0);
      strip.setPixelColor ((3*NUM_LEDS/4) + j, 0, 0, 0); 

      strip.show ();     
    }
    
  }
  
  setAllLights (0, 0, 0);

}

void ledSetByRow (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  for (int i = 0; i < NUM_LEDS / 4; i++)
  {
    if (leds & 0x1)
    {
      strip.setPixelColor (i, r, g, b);
    }
    
    if (leds & 0x2)
    {
      strip.setPixelColor((NUM_LEDS / 4) + i, r, g, b);
    }
    
    if (leds & 0x4)
    {
     strip.setPixelColor ((2 * NUM_LEDS / 4) + i, r, g, b); 
    }
    
    if (leds & 0x8)
    {
     strip.setPixelColor ((3 * NUM_LEDS / 4) + i, r, g, b); 
    }
  }
  
  strip.show ();
  
  delay (duration);
}

void ledFlash (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  ledSetByRow (leds, r, g, b, duration);
  
  setAllLights (0, 0, 0);  
}

void ledAlternate (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  static byte lastR = 0;
  static byte lastG = 0;
  static byte lastB = 0;
  unsigned long start = millis ();
  
  while (duration >= millis () - start)
  {
    ledSetByRow (leds, r, g, b, 50);
    ledSetByRow (leds, lastR, lastG, lastB, 50);
  }
  
  lastR = r;
  lastG = g;
  lastB = b;
  
  setAllLights (0, 0, 0);  
}

void upperCase (char * buf, int buf_size)
{
  for (int i = 0; i < buf_size && buf[i]; i++)
  {
     buf[i] = toupper(buf[i]);
  } 
}
