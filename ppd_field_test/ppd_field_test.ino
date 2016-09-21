//Libraries
#include <NS_Rainbow.h>         //Downloaded from internet, see documentation
#include <Adafruit_VS1053.h>    //Downloaded from internet, see documentation
#include <SPI.h>                //Standard Arduino library
#include <SdFat.h>              //SD card library
#include <string.h>
#include <Wire.h>
#include <RTClib.h>
#include <MemoryFree.h>

//Constants
#define LEDPIN1 27          //Pin for first LED strand
#define LEDPIN2 29          //Pin for second LED strand
#define NLED 16             //Number of LEDs per strand
#define BRIGHTNESS 64       //LED brightness, from 1-128. Default is 64.

#define LIGHT_SENSOR_1 12       //Pin for first light sensor
#define LIGHT_SENSOR_2 13       //Pin for second light sensor
#define LIGHT_THRESHOLD 200  //Lower limit for light sensors before device turns on

#define S1 43         //Pin for switch 1
#define S2 41         //Pin for switch 2
#define S3 39         //Pin for switch 3
#define S4 37         //Pin for switch 4
#define S5 35         //Pin for switch 5
#define S6 33         //Pin for switch 6
#define S7 31         //Pin for switch 7

#define BREAKOUT_RESET  9      // VS1053 reset pin (output)
#define BREAKOUT_CS     10     // VS1053 chip select pin (output)
#define BREAKOUT_DCS    8      // VS1053 Data/command select pin (output)
#define BREAKOUT_SDCS 4               // Card chip select pin
#define DREQ 3                 // VS1053 Data request, ideally an Interrupt pin
#define LVOLUME 2             // Volume for left speaker. 
#define RVOLUME 2             // Volume for right speaker.

#define LOGGING_SDCS 6

//ERROR CODES
#define LED_FAILURE              -1 //now way for this to be thrown with the current library
#define SOUND_FAILURE            -2
#define LIGHT_SENSOR_FAILURE     -3 //can be thrown accidentally if sensors are completely covered
#define SWITCH_FAILURE           -4 //will not be thrown at the moment -> maybe eventually add self test?
#define SOUND_SD_CARD_FAILURE    -5
#define LOGGING_SD_CARD_FAILURE  -6
#define RTC_FAILURE              -7

#define MINUTE 60000


//Initialize LED strands
NS_Rainbow strand1 = NS_Rainbow(NLED, LEDPIN1);
NS_Rainbow strand2 = NS_Rainbow(NLED, LEDPIN2);

//Initialize music player
Adafruit_VS1053_FilePlayer musicPlayer = Adafruit_VS1053_FilePlayer(BREAKOUT_RESET, BREAKOUT_CS, BREAKOUT_DCS, DREQ, BREAKOUT_SDCS);

unsigned long start;
unsigned long waitInterval;

SdFat SD; //VS1053 lib requires a global SD object because it uses the old SD lib
SdFat loggingSD;

boolean verboseLogging = false;

char * * patternFiles = NULL; //expected to be found at loggingSD://patterns/
char * * soundFiles = NULL; //expected to be found at SD://
int numPatternFiles;
int numSoundFiles;

boolean setupSuccessful = true;
boolean ledFailed = false;
boolean ignoreSetupErrors = false;

boolean loggingSDSetupFailed = false;

int overrideMode = -1;
int overrideSensitivity = -1;
int overrideNight = -1;
int overrideInterval = -1;

int volume = 75;

RTC_DS3231 RTC;

struct pattern
{
  int length;
  char lines[50][32];
};

//typedef struct pattern blaw;

void setup ()
{
  Serial.begin (9600);
  randomSeed(analogRead(0));
  
  do
  {
    setupSuccessful = true;
    
    pinMode (LOGGING_SDCS, OUTPUT);
    digitalWrite (LOGGING_SDCS, HIGH);  
    
    if (!rtcSetup())
    {
      error (RTC_FAILURE);
      setupSuccessful = false;
    }
    
    if (! SD.begin(BREAKOUT_SDCS)){
      Serial.println(F("sound SD card setup failed."));
      error (SOUND_SD_CARD_FAILURE);
      setupSuccessful = false;
    }
    else
    {
      SD.ls(); 
    }
         
    if (!loggingSD.begin(LOGGING_SDCS))
    {
      Serial.println(F("logging SD card setup failed."));
      error (LOGGING_SD_CARD_FAILURE);
      loggingSDSetupFailed = true;
      setupSuccessful = false;
    }
    else
    {
       loggingSDSetupFailed = false;
       loggingSD.ls(); 
    }
   
    //LEDs
    if (! ledSetup(BRIGHTNESS)){
      Serial.println(F("LED setup failed."));
      writeToLog("LED setup failed.");  
      ledFailed = true;
      setupSuccessful = false;
    }
    else {
      ledWave(B1111, 255, 255, 255, 1000);
      ledSet(0,0,0);
    }
    
    //MP3 Player
    if (! musicPlayer.begin()){
      Serial.println(F("Speakers and MP3 player setup failed."));
      writeToLog ("Speakers and MP3 player setup failed.");
      error (SOUND_FAILURE);
      setupSuccessful = false;
    }
    else {
      //Rest of speaker setup
      musicPlayer.setVolume(volume, volume);
      musicPlayer.useInterrupt(VS1053_FILEPLAYER_PIN_INT);
      musicPlayer.sineTest(0x44, 1000);
      
      if (ledFailed)
      {
        error (LED_FAILURE); 
      }
    }
    
    //Light sensors
    if (!lightSensorSetup()){
      Serial.println(F("Light sensor setup failed."));
      writeToLog ("Light sensor setup failed."); 
      error (LIGHT_SENSOR_FAILURE);
      setupSuccessful = false;
    }
  
    //Switches
    if (!switchSetup()){
      Serial.println(F("Switch setup failed."));
      writeToLog("Switch setup failed.");  
      error (SWITCH_FAILURE);
      setupSuccessful = false;
    }

    loadAndRunDebug(); // only early options can run. for now this only means the ignore errors setting
    
    delay (3000);
    
  } while (!setupSuccessful && !ignoreSetupErrors);

  loadSoundAndPatternNames ();  

  writeToLog ("booted successfully");
  ledSet (0, 255, 0);
  delay (500);
  ledSet (0, 0, 0);

  loadAndRunDebug();

  writeToLog ("Beginning main program");
  Serial.println ("Beginning main program");
  
  start = millis();
  waitInterval = MINUTE / 2;  
}

void loop ()
{
  byte mode;
  int soundIndex = -1;
  int patternIndex = -1;
  int baseInterval = 0;
  int adjustment = 0;
  unsigned long actionStart;
  struct pattern myPattern;
  
  if (millis() - start > waitInterval)
  { 
    if (isNight(isSensitive()))
    {
      mode = getMode();
      
      if (0x1 & mode)
      {
        patternIndex = random(0,numPatternFiles);
        loadPattern (patternFiles[patternIndex], myPattern);
        writeToLog("Using pattern: " + String (patternFiles[patternIndex]));
      }
      
      if (0x2 & mode)
      {
        soundIndex = random(0, numSoundFiles);
        
        if (getVolume () != volume)
        {
          volume = getVolume ();
          musicPlayer.setVolume (volume, volume);
        }
        
        writeToLog("Playing " + String (soundFiles[soundIndex]));
        if (!musicPlayer.startPlayingFile(soundFiles[soundIndex]))
        {
           Serial.println ("problem with player"); 
        }
      }

      actionStart = millis();

      while (!musicPlayer.stopped() && MINUTE / 2 >= millis() - actionStart)
      {
        if (0x1 & mode)
        { 
          playPattern(myPattern);
        }
      }

      if (0x2 & mode || !musicPlayer.stopped())
      {
        musicPlayer.stopPlaying();
      }
      
      baseInterval = getInterval ();

      adjustment = random(-(baseInterval/5)*2,(baseInterval/5)*2 + 1);
      //Serial.println ("Adjustment is " + String(adjustment));
      waitInterval = (baseInterval + adjustment) * MINUTE;
      start = millis();
      
    }
    else
    {
      writeToLog ("Daytime");
      waitInterval = 10 * MINUTE;
      start = millis();
    }
  }
}

// plays all sounds on the sound SD card for 10 seconds
void playAllSounds ()
{
  unsigned long start;
//    Serial.print ("Number of sounds: ");
//  Serial.println (numSoundFiles);
  for (int i = 0; i < numSoundFiles; i++)
  {
    if (verboseLogging)
    {
      writeToLog("Playing " + String(soundFiles[i]));
    }
    musicPlayer.startPlayingFile (soundFiles[i]);
    start = millis();

    while (MINUTE / 2 >= millis () - start);
    musicPlayer.stopPlaying();
//    Serial.println ("end of sound file");
  }
}

// plays all patterns in the 'pattern/' directory of the logging SD card
void playAllPatterns ()
{
//  Serial.print ("Number of patterns: ");
//  Serial.println (numPatternFiles);
  struct pattern myPattern;
  
  for (int i = 0; i < numPatternFiles; i++)
  {
    if (verboseLogging)
    {
      writeToLog("Running pattern " + String(patternFiles[i]));
    }
    if (!loadPattern (patternFiles[i], myPattern))
    {
      Serial.print (F("Error running: "));
      Serial.println (patternFiles[i]);
    }
    else
    {
      playPattern (myPattern); 
    }
  }
}


void debugSound()
{
  unsigned long start;

  ledSet (200, 255, 0);
  musicPlayer.setVolume(50,100);
  musicPlayer.startPlayingFile ("Sean.mp3");
  start = millis ();

  while (10000 >= millis() - start);


  ledSet (0, 255, 126);
  musicPlayer.setVolume(100,50);
  start = millis ();

  while (10000 >= millis() - start);

  ledSet (0, 255, 0);
  delay (1000);
  
  musicPlayer.stopPlaying();
  musicPlayer.setVolume (5, 5);
}

//lights will change colors to indicate a successfully detected switch flip
void debugSwitches()
{
  byte switches[] = {S1, S2, S3, S4, S5, S6, S7};
  boolean switches_init[7];
  boolean set = false;
  byte numChanged = 0;
  long debounceDelay = 75;
  long lastDebounceTime[] = {0, 0, 0, 0, 0, 0, 0};
  boolean stateChanged[] = {false, false, false, false, false, false, false};

  for (int i = 0; i < 7; i++)
  {
    switches_init[i] = digitalRead(switches[i]);
  }
  
  ledSet (255, 255, 0);
  
  do
  {
    for (int i = 0; i < 7; i++)
    {
      if (switches_init[i] != digitalRead(switches[i]) && !stateChanged[i])
      {
        lastDebounceTime[i] = millis();
        stateChanged[i] = true;
      }

      if ((millis() - lastDebounceTime[i]) > debounceDelay)
      {
        if (switches_init[i] != digitalRead(switches[i]))
        {
          Serial.println("Switch on pin " + String (switches[i]));
          Serial.print("Sensitivity = " + String (isSensitive()));
          Serial.print("; Mode = " + String (getMode()));
          Serial.println("; Interval = " + String (getInterval()));
          switches_init[i] = digitalRead(switches[i]);
          numChanged++;
          ledSet (255 - (numChanged*36), 255, (numChanged*36));
        }

        stateChanged[i] = false;
      }
    }
    
  } while (numChanged < 7);

  ledSet (0, 255, 0);
  delay (500);
  ledSet (0 , 0, 0);
  delay (500);
  ledSet (0, 255, 0);
  delay (500);
  ledSet (0 , 0, 0);
  delay (500);
}


// lights will turn green once test is passed
void debugSensors()
{
  ledSet (126, 126, 126);
  while (!isNight(false));
  ledSet(0, 0, 255);
  while (isNight(true));
  ledSet(0, 255, 0);
  delay (1000);
  ledSet (0, 0, 0);
  
}

// allows user to choose a different mode for debugging
// purposes
void loadAndRunDebug ()
{
  static boolean earlyRun = true;
  SdFile myFile;
  char line[32];
  int index;
  boolean repeat = false;
  char * str;
  char * i;
  
  loggingSD.chvol();
  
  
  if (myFile.open("debug.txt"))
  {
    do
    {
      Serial.println("Running debug commands");
      myFile.rewind();
      while (myFile.available())
      {
         index = 0;
         memset (line, 0, sizeof(line));
         while ('\n' != (line[index++] = myFile.read()) && index < 32);

         while (!isAlphaNumeric(line[index - 1]))
         {
            line[index - 1] = '\0';
            index--;
         }
         
         if (0 == strcmp(line, "debug sound") && !earlyRun)
         {
           if (verboseLogging)
           {
            writeToLog("debugging sound");
           }
           debugSound(); 
         }
         else if (0 == strcmp(line, "debug switches") && !earlyRun)
         {
           if (verboseLogging)
           {
            writeToLog("debugging switches");
           }
           debugSwitches();
         }
         else if (0 == strcmp(line, "debug sensors") && !earlyRun)
         {
           if (verboseLogging)
           {
            writeToLog("debugging sensors");
           }
           debugSensors();
         }
         else if (0 == strcmp(line, "play all sounds") && !earlyRun)
         {
          if (verboseLogging)
           {
            writeToLog("playing all sounds");
           }
           playAllSounds();
         }
         else if (0 == strcmp (line, "play all patterns") && !earlyRun)
         {
           if (verboseLogging)
           {
            writeToLog("playing all patterns");
           }
           playAllPatterns();
         }
         else if (0 == strcmp (line, "verbose logging") && earlyRun)
         {
           verboseLogging = true; 
           writeToLog("Verbose logging turned on");
         }
         // none of the following should not be used by end users
         // they are primarily intended for bench/assembly testing
         else if (0 == strcmp (line, "ignore errors") && earlyRun)
         {
            if (verboseLogging)
            {
              writeToLog ("ignoring any errors during setup");
            }
            ignoreSetupErrors = true;
         }
         else if (strstr(line, "mode="))
         {
            str = strtok_r(line, "=", &i);
            overrideMode = (int)atoi (strtok_r(NULL, "=", &i));
            if (verboseLogging)
            {
              writeToLog("mode set to " + String(overrideMode));
            }
         }
         else if (strstr(line, "sensitivity="))
         {
            str = strtok_r(line, "=", &i);
            overrideSensitivity = (int)atoi (strtok_r(NULL, "=", &i));
            if (verboseLogging)
            {
              writeToLog("sensitivity set to " + String(overrideSensitivity));
            }
         }
         else if (strstr(line, "night="))
         {
            str = strtok_r(line, "=", &i);
            overrideNight = (int)atoi (strtok_r(NULL, "=", &i));
            if (verboseLogging)
            {
              writeToLog("night set to " + String(overrideNight));
            }
         }
         else if (strstr(line, "interval="))
         {
            str = strtok_r(line, "=", &i);
            overrideInterval = atoi (strtok_r(NULL, "=", &i));
            if (verboseLogging)
            {
              writeToLog("interval set to " + String(overrideInterval));
            }
         }
         else if (0 == strcmp (line, "repeat") && !earlyRun)
         {
           repeat = true;
         }
      }
    } while (repeat);
    myFile.close ();
  }
  else
  {
    writeToLog("no debug file"); 
  }

  earlyRun = false;
  
  SD.chvol();
}

void upperCase (char * buf, int buf_size)
{
  for (int i = 0; i < buf_size && buf[i]; i++)
  {
     buf[i] = toupper(buf[i]);
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
           Serial.println(String(buff) + " is available to play");
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
      Serial.println(String(buff) + " is available to use");

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
  byte index;
  Serial.print("freeMemory()=");
  Serial.println(freeMemory());
  
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
    error(1); //throw a generic error
  }
  
  return success;
}

boolean playPattern (struct pattern myPattern)
{
  char effect[10];
  char buff[32];
  byte ledChip = 0;
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
      ledChip = B1111;
    }
    else
    {
      for (int i = 0; i < 4; i++)
      {
        ledChip <<= 1;
        ledChip |= ('1' == str[i] ? 0x1 : 0x0);
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
      ledFlash (ledChip, r, g, b, duration);
    }
    else if (0 == strcmp (effect, "wave"))
    {
      // activates the lights on a chip one at a time from top to bottom
      ledWave (ledChip, r, g, b, duration);
    }
    else if (0 == strcmp (effect, "alt"))
    {
      // alternates colors rapidly. Alternates with the color used during the
      // last call
      ledAlternate (ledChip, r, g, b, duration);
    }
    else
    {
       //default: all white for duration
       ledSet (255, 255, 255);
       delay (duration);
       ledSet (0, 0, 0);
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
    for (int i = 0; i < 8; i++)
    {
      if (leds & 0x1)
      {
        strand1.setColor (i, r, g, b);
      }
      
      if (leds & 0x2)
      {
        strand1.setColor(NLED - 1 - i, r, g, b);
      }
      
      if (leds & 0x4)
      {
       strand2.setColor (i, r, g, b); 
      }
      
      if (leds & 0x8)
      {
       strand2.setColor (NLED - 1 - i, r, g, b); 
      }
      
      strand1.show ();
      strand2.show ();
      
      delay (75);
      
      strand1.setColor (i, 0, 0, 0);
      strand1.setColor (NLED - 1  - i, 0, 0, 0);
      strand2.setColor (i, 0, 0, 0);
      strand2.setColor (NLED - 1 - i, 0, 0, 0);
      
      strand1.show ();
      strand2.show ();     
    } 
  }
  
  ledSet (0, 0, 0);

}

void ledSetByChip (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  for (int i = 0; i < 8; i++)
  {
    if (leds & 0x1)
    {
      strand1.setColor (i, r, g, b);
    }
    
    if (leds & 0x2)
    {
      strand1.setColor(NLED - 1 - i, r, g, b);
    }
    
    if (leds & 0x4)
    {
     strand2.setColor (i, r, g, b); 
    }
    
    if (leds & 0x8)
    {
     strand2.setColor (NLED - 1  - i, r, g, b); 
    }
  }
  
  strand1.show ();
  strand2.show ();
  
  delay (duration);
}

void ledFlash (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  ledSetByChip (leds, r, g, b, duration);
  
  ledSet (0, 0, 0);  
}

void ledAlternate (byte leds, byte r, byte g, byte b, unsigned long duration)
{
  static byte lastR = 0;
  static byte lastG = 0;
  static byte lastB = 0;
  unsigned long start = millis ();
  
  while (duration >= millis () - start)
  {
    ledSetByChip (leds, r, g, b, 50);
    ledSetByChip (leds, lastR, lastG, lastB, 50);
  }
  
  lastR = r;
  lastG = g;
  lastB = b;
  
  ledSet (0, 0, 0);  
}

void ledSet (byte r, byte g, byte b)
{
   for (int i = 0; i < NLED; i++)
   {
      strand1.setColor(i, r,g,b);
      strand2.setColor(i, r,g,b);   
   } 
   
   strand1.show();
   strand2.show();
}

void writeToLog(String entry)
{  
  DateTime now = RTC.now();
  
  SdFile log;
  
  //If the logging SD card is absent or never got set up correctly
  //then don't worry about trying to log anything
  if (!loggingSDSetupFailed)
  {
    loggingSD.chvol();
    loggingSD.chdir();
    
    if (log.open("activity.log", O_RDWR | O_CREAT | O_AT_END))
    {
      log.print(now.month(), DEC);
      log.print('-');
      log.print(now.day(), DEC);
      log.print('-');
      log.print(now.year(), DEC);
      log.print(' ');
      log.print(now.hour(), DEC);
      log.print (':');
      if (now.minute() < 10) log.print ('0');
      log.print(now.minute(), DEC);
      log.print (':');
      if (now.second() < 10) log.print ('0');
      log.print(now.second(), DEC);
      log.print ("> ");
      log.println (entry);
      log.close();
    }
  
    SD.chvol();
  }
  
}

boolean rtcSetup()
{
  boolean success = true;
  if (!RTC.begin())
  {
    success = false;
  }
  else
  {
    if (RTC.lostPower())
    {
      RTC.adjust(DateTime(__DATE__, __TIME__));
    }
  }  
  
  return success;
}

boolean ledSetup(int brightness){
    
    //Set up first strand
    strand1.begin();
    strand1.setBrightness(brightness);
    strand1.show();
    
    //Set up second strand
    strand2.begin();
    strand2.setBrightness(brightness);
    strand2.show();
    
    return true;
}

boolean switchSetup ()
{
  pinMode(S1, INPUT);
  pinMode(S2, INPUT);
  pinMode(S3, INPUT);
  pinMode(S4, INPUT);
  pinMode(S5, INPUT);
  pinMode(S6, INPUT);
  pinMode(S7, INPUT);
  
  return true; //pinMode does not return anything
}

boolean lightSensorSetup()
{
  //Set pins to input
  pinMode(LIGHT_SENSOR_1, INPUT);
  pinMode(LIGHT_SENSOR_2, INPUT);

  //Test sensors. If either value is 0, might be disconnected.
  int s1 = analogRead(LIGHT_SENSOR_1);
  int s2 = analogRead(LIGHT_SENSOR_2);
  if (s1 == 0 || s2 == 0){
    return false;
  }
  
  return true;
}

void error(int errorCode)
{
  ledSet (0, 0, 0);

  if (LOGGING_SD_CARD_FAILURE != errorCode)
  {
    writeToLog ("Error occurred. Error: " + String (errorCode));
  }
  
  switch (errorCode)
  {
    case LED_FAILURE:
      musicPlayer.setVolume(10,10);
      for (int i = 0; i < 3; i++)
      {
        musicPlayer.sineTest(0x44, 1000);
        delay (500);
      }
      break;
    case SOUND_FAILURE:
    case LIGHT_SENSOR_FAILURE:
    case SWITCH_FAILURE:
    case SOUND_SD_CARD_FAILURE:
    case LOGGING_SD_CARD_FAILURE:
    case RTC_FAILURE:
      for (int i = 0; i < 3; i++)
      {
        for (int j = 0; j < -errorCode; j++)
        {
          ledSet (255, 0, 0);
          delay(500);
          ledSet (0, 0, 0);
          delay(500);
        } 
        delay(2000);
      }
      break;
    default:
      for (int i = 0; i < 3; i++)
        {
          ledSet (255, 255, 0);
          delay(500);
          ledSet (0, 0, 0);
          delay(500);
        } 
      
  } 
}

boolean isNight (boolean isSensitive)
{
  int sensor1 = analogRead(LIGHT_SENSOR_1);
  int sensor2 = analogRead(LIGHT_SENSOR_2);
  boolean rtcDay = false;
  
  boolean sensor1IsNight = sensor1 <= LIGHT_THRESHOLD;
  boolean sensor2IsNight = sensor2 <= LIGHT_THRESHOLD;

  if (useRTCForDay ())
  {
    DateTime now = RTC.now();
    if (4 < now.hour() && 20 > now.hour ())
    {
      rtcDay = true;
    }
  }

  if (overrideNight >= 0)
  {
    return overrideNight;
  }
  
  return ((isSensitive && (sensor1IsNight || sensor2IsNight)) 
          || (!isSensitive && (sensor1IsNight && sensor2IsNight))) && !rtcDay;
}

boolean isSensitive ()
{
  //S2
  boolean sensitivity;
  
  sensitivity = digitalRead(S2);
  
  if (overrideSensitivity >= 0)
  {
    sensitivity =  overrideSensitivity;
  }
  
  if (verboseLogging)
  {
    writeToLog ("Sensitivity is " + String (sensitivity));
  }
  
  return sensitivity;
}

byte  getMode ()
{
  //S4, S5
  byte mode = 0;
  mode = digitalRead(S4) ? mode | B10 : mode;
  mode = digitalRead(S5) ? mode | B1 : mode;
  
  if (overrideMode >= 0)
  {
    mode = overrideMode;
  }

  if (verboseLogging)
  {
    writeToLog ("Mode is " + String (mode));
  }

    
  return mode;
}

int getInterval ()
{
  //S6, S7
  int interval;
  int intervalMultiplier = digitalRead(S6) << 1;
  intervalMultiplier |= digitalRead(S7);

  interval = 5 + (intervalMultiplier * 5);

  if (overrideInterval >= 0)
  {
    interval = overrideInterval;
  }
  
  if (verboseLogging)
  {
    writeToLog("Interval is " + String(interval));
  }

  return interval;
}

int getVolume ()
{
  // S1
  byte value = digitalRead(S1);
  int volume = 0;

  if (!value) // default == loud
  {
    volume = 5;
  }
  else
  {
    volume = 100;
  }

  return volume;
}

boolean useRTCForDay()
{
  return digitalRead(S3);
}

