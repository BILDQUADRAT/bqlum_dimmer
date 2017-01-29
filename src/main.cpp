#include <UIPEthernet-8.h>
#include <PubSubClient.h>
#include <SoftPWM.h>
#include <EEPROM.h>


// Setup
#define LED_RED 0
#define LED_GREEN 1

#define NUM_ENTITIES 4

/** GEHT VON 4 CHANNELS AUS! */
#define BRIGHT_START_ADDR 0  // 0-3
#define COLOR_START_ADDR  4  // 4-11
#define MAC_START_ADDR    256 // 256-262 (incl. checksum)
#define MYIP_START_ADDR   263 // 263-267 (incl. checksum)
#define SERVER_START_ADDR 268 // 268-272 (incl. checksum)
#define CHAN_START_ADDR   273 // 273-276

// Update these with values suitable for your network.
byte mac[]    = {  0x00, 0x0B, 0xF4, 0x00, 0x00, 0x01 };
byte server[] = { 10, 0, 24, 2 };
byte myIp[] = { 10, 0, 24, 251 };

char clientid[128];

// const Node stuff
#define TYPE_SINGLE 0
#define TYPE_SWITCH 1
#define TYPE_COLOR  2
#define TYPE_WHITEDUO 3


const unsigned char pins[]  = {  2,  3,  4, 10, 19, 18, 17, 16, 15, 14 };
unsigned char channels[NUM_ENTITIES] = {
  /* SOLLTE NICHT 255 SEIN */
  1, // node 0
  2, // node 1
  3, // node 2
  4  // node 3
};
const unsigned char types[NUM_ENTITIES] = {
  TYPE_SINGLE,   // node 0
  TYPE_COLOR, // node 1
  TYPE_COLOR,    // node 2
  TYPE_COLOR     // node 3
};
const unsigned char singlePins[NUM_ENTITIES] = {
  0, // node 0
  0, // dummy
  0, // dummy
  0  // dummy
};
const unsigned char colorPins[NUM_ENTITIES][3] = {
  {0,0,0}, // dummy
  {1,2,3}, // node 1 (color)
  {4,5,6}, // node 2 (color)
  {7,8,9}  // node 3 (color)
};

// value storage
uint8_t brightness[NUM_ENTITIES] = {
  255, // node 0
  255, // node 1
  255, // node 2
  255  // node 3
};
uint8_t color[NUM_ENTITIES][3] = {
  {0xFF,0xFF,0xFF}, // dummy
  {0xFF,0xFF,0xFF}, // node 1
  {0xFF,0xFF,0xFF}, // node 2
  {0xFF,0xFF,0xFF}  // node 3
};

// misc
void(* resetFunc) (void) = 0;

uint8_t colorMultiply(uint8_t one, uint8_t two) {
  long res = one*two;
  res = res/255;

  return (uint8_t)res;
}

void setMulticolor(uint8_t num, uint8_t channelCount) {
  for(uint8_t i = 0; i < channelCount; i++) {
    SoftPWMSet(pins[colorPins[num][i]], colorMultiply(brightness[num], color[num][i]));
  }
}
void setMulticolor(uint8_t num) {
  setMulticolor(num, 3);
}

char* getCompStr(char* channel, char* param) {
  char* comp = (char*) malloc(sizeof("light//")-1 + sizeof(param)-1 + sizeof(channel));
  strcpy(comp, "light/");
  strcat(comp, channel);
  strcat(comp, "/");
  strcat(comp, param);
  return comp;
}

void storeChannel(uint8_t num) {
  // Helligkeit ablegen
  EEPROM.write(BRIGHT_START_ADDR + num, brightness[num]);
  // Farbe ablegen
  for(uint8_t i = 0; i < 3; i++) {
    EEPROM.write(COLOR_START_ADDR + (num*3) + i, color[num][i]);
  }
}

// Callback function
void callback(char* topic, byte* payload, unsigned int length) {
 // Copie du payload dans byte* pl pour usage dans les fonctions, car il
 // est vide lors d'un appel a publish
 char pl[length];
 memcpy(pl,payload,length);
 int plInt = atoi(pl);

 // TOPIC abfragen
 // - welcher Channel
 for(uint8_t i = 0; i < sizeof(types); i++) {
   // channel in char*
   char channel[4];
   itoa(channels[i], channel, 10);

   // channel des aktuellen Durchlaufs?
   char *comp = getCompStr(channel,"");
   if(strncmp(topic, comp, sizeof(comp)) == 0) {
     // copy for re-use
     itoa(channels[i], channel, 10);
     // teste auf "brightness" parameter
     char *compB = getCompStr(channel, "brightness");
     if(strcmp(topic, compB) == 0 && plInt >= 0 && plInt <= 255) {
       brightness[i] = plInt;
       if(types[i] == TYPE_WHITEDUO) {
         // duo-pin white stripe (uses two color bytes - hex encoded)
         setMulticolor(i,2);
       } else if(types[i] == TYPE_COLOR) {
         // tripple-pin rgb light (uses three color bytes - hex encoded)
         setMulticolor(i,3);
       } else {
         // some kind of single pin light
         SoftPWMSet(pins[singlePins[i]], brightness[i]);
       }
     }
     free(compB);

     // teste auf "color" parameter
     char *compC = getCompStr(channel, "color");
     if(strcmp(topic, compC) == 0) {
       char subnum[3];
       for(uint8_t j = 0; j < 3; j++) {
         strncpy(subnum, pl+(j*2), 2);
         color[i][j] = strtol(subnum, NULL, 16);
       }
       if(types[i] == TYPE_WHITEDUO) {
         setMulticolor(i,2);
       } else if(types[i] == TYPE_COLOR) {
         setMulticolor(i,3);
       }
     }
     free(compC);

     storeChannel(i);
   }
   free(comp);
 }
}

EthernetClient ethClient;
PubSubClient client(server, 1883, callback, ethClient);

void registerNode(uint8_t channel, uint8_t type) {
  char c_channel[4];
  itoa(channel, c_channel, 10);
  // subscribe to attributes
  char topic[16];
  strcpy(topic, "light/");
  strcat(topic, c_channel);
  strcat(topic, "/#");
  client.subscribe(topic);

  // announce node
  // - type as string
  char c_type[16];
  strcpy(c_type, "single");
  if(type == TYPE_SWITCH) {
    strcpy(c_type, "switch");
  } else if(type == TYPE_COLOR) {
    strcpy(c_type, "RGB");
  } else if(type == TYPE_WHITEDUO) {
    strcpy(c_type, "whiteDuo");
  }

  char msg[64];
  strcpy(msg, c_channel);
  strcat(msg, " ");
  strcat(msg, c_type);
  client.publish("light/_new", msg);
}

void restoreChannel(uint8_t num) {
  // Helligkeit lesen
  brightness[num] = EEPROM.read(BRIGHT_START_ADDR + num);
  if(types[num] == TYPE_WHITEDUO) {
    // duo-pin white stripe (uses two color bytes - hex encoded)
    // Farbe lesen
    for(uint8_t i = 0; i < 2; i++) {
      color[num][i] = EEPROM.read(COLOR_START_ADDR + (num*3) + i);
    }
    setMulticolor(num,2);
  } else if(types[num] == TYPE_COLOR) {
    // tripple-pin rgb light (uses three color bytes - hex encoded)
    // Farbe lesen
    for(uint8_t i = 0; i < 3; i++) {
      color[num][i] = EEPROM.read(COLOR_START_ADDR + (num*3) + i);
    }
    setMulticolor(num,3);
  } else {
    // some kind of single pin light
    SoftPWMSet(pins[singlePins[num]], brightness[num]);
  }
}

void fromEEPROMIfChecksum(byte* target, uint8_t target_size, const uint16_t startAddr) {
  byte temp[target_size];
  uint8_t checksum = 0;
  for(uint8_t i = 0; i < target_size; i++) {
    temp[i] = EEPROM.read(startAddr + i);
    checksum += temp[i];
  }
  checksum = checksum & 0xFF;
  if((uint8_t)EEPROM.read(startAddr + target_size) == checksum) {
    for(uint8_t j = 0; j < target_size; j++) {
      target[j] = temp[j];
    }
  }
}

void restoreConfig() {
  // MAC-Adresse
  fromEEPROMIfChecksum(mac, sizeof(mac), MAC_START_ADDR);

  // Eigene Fallback IP
  fromEEPROMIfChecksum(myIp, sizeof(myIp), MYIP_START_ADDR);

  // Server IP
  fromEEPROMIfChecksum(server, sizeof(server), SERVER_START_ADDR);

  // Channel-Zuordnung
  uint8_t temp = 0;
  for(uint8_t i = 0; i < sizeof(channels); i++) {
    temp = (uint8_t)EEPROM.read(CHAN_START_ADDR+i);
    if(temp < 255) {
      channels[i] = temp;
    }
  }
}

void setupClientId() {
  strcpy(clientid, "dimmer");
  char c_uuid[74];
  itoa(mac[5], c_uuid, 10);
  strcat(clientid, c_uuid);
}


void setup()
{
  //randomSeed(analogRead(0));

  // debug leds
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, LOW);

  // Dimmer-Kanäle initialisieren
  SoftPWMBegin();
  for(uint8_t i = 0; i < sizeof(pins); i++) {
    // Create and set pin to 0 (off)
    SoftPWMSet(pins[i], 0);
    // Set fade time
    SoftPWMSetFadeTime(pins[i], 10, 10);
  }
  // alte Werte setzen
  for(uint8_t i = 0; i < sizeof(channels); i++) {
    restoreChannel(i);
  }

  restoreConfig();
  if(!Ethernet.begin(mac)) {
    Ethernet.begin(mac, myIp);
  }

  //setupClientId();

  // 2 second timeout before trying to connect
  digitalWrite(LED_GREEN, HIGH);
  delay(1000);
  digitalWrite(LED_GREEN, LOW);
  delay(1000);
}

void loop()
{
  // library loop
  // - reconnect if necessary
  if(!client.connected()) {
    digitalWrite(LED_GREEN, LOW);
    delay(1000);
    if(client.connect("dimmer-test-ton")) {
      digitalWrite(LED_GREEN, HIGH);
      client.publish("test/sys/dimmer","connected!");
      // nodes initialisieren
      for(uint8_t i = 0; i < sizeof(channels); i++) {
        registerNode((int)channels[i], types[i]);
      }
    }
  }

  client.loop();
}
