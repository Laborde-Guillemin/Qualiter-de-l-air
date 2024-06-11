/* _____                 _  _                            _           _      _         _
 / ___ \               | |(_) _                        | |         | |    ( )       (_)
| |   | | _   _   ____ | | _ | |_    ____   ____     _ | |  ____   | |    |/   ____  _   ____
| |   |_|| | | | / _  || || ||  _)  / _  ) / ___)   / || | / _  )  | |        / _  || | / ___)
 \ \____ | |_| |( ( | || || || |__ ( (/ / | |      ( (_| |( (/ /   | |       ( ( | || || |
  \_____) \____| \_||_||_||_| \___) \____)|_|       \____| \____)  |_|        \_||_||_||_| */

/*Blibiothèque*/
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CSgp40.h>
#include <SensirionI2CSfa3x.h>
#include <Adafruit_BME280.h>
#include <VOCGasIndexAlgorithm.h>
#include "sps30.h"
#include "SparkFun_SCD30_Arduino_Library.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "time.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <RTClib.h>
#include <Arduino_GFX_Library.h>

/*Definition*/
#define DEBUG 0
#define SP30_COMMS Wire
#define USE_50K_SPEED 1


/*Initialisation des fonctions*/
SensirionI2CSgp40 sgp40;
SensirionI2CSfa3x sfa3x;
Adafruit_BME280 bme;
SPS30 sps30;
SCD30 airSensor;

float sampling_interval = 1.f;
VOCGasIndexAlgorithm voc_algorithm(sampling_interval);
int Refresh = 60000; // 1min = 60000
int RefreshCap = 30000; // 30 seconde = 30000

// Replace with your network credentials
const char* ssid = "WIFI-CIEL"; //  Wifi-visiteur
const char* password = "alcasarciel"; // Ba4:z653z
// Create AsyncWebServer object on port 80
AsyncWebServer server(80);


//Variable Mesures Capteurs
String Date;//
String Heure;//
float Temp;// bme.readTemperature();
float Hum;// bme.readHumidity()
int COV;//
int Alde;// hcho / 5.0
int CO2;//
float PM_1;//
float PM_25;//
float PM_10;//

////////////////////
/*Gestion carte SD*/
////////////////////

//Port SPI de la carte SD
#define SCK  18
#define MISO  19
#define MOSI  23
#define CS  32

String dataMessage;
SPIClass spi = SPIClass(VSPI);

/*Initialisation de la carte SD*/
void initSDCard(){
  spi.begin(SCK, MISO, MOSI, CS);
  if (!SD.begin(CS,spi,80000000)) {
    Serial.println("Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if(cardType == CARD_NONE){
    Serial.println("No SD card attached");
    return;
  }
  Serial.print("SD Card Type: ");
  if(cardType == CARD_MMC){
    Serial.println("MMC");
  } else if(cardType == CARD_SD){
    Serial.println("SDSC");
  } else if(cardType == CARD_SDHC){
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }
  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
}

/*Écriture du fichier*/
void writeFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Writing file: %s\n", path);
  File file = fs.open(path, FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  if(file.print(message)){
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  file.close();
}

/*Rajout dans le fichier*/
void appendFile(fs::FS &fs, const char * path, const char * message){
  Serial.printf("Appending to file: %s\n", path);
  File file = fs.open(path, FILE_APPEND);
  if(!file){
    Serial.println("Failed to open file for appending");
    return;
  }
  if(file.print(message)){
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

/*Préparation du fichier d'enregistrement*/
void initFile(){
  File file = SD.open("/Valeur.csv");
  if(!file) {
    Serial.println("File doesn't exist");
    Serial.println("Creating file...");
    writeFile(SD, "/Valeur.csv", "Date; Heure; Température; Humidité; Indice de COV; Forme Aldéhyde; CO²; PM_1; PM_2,5; PM_10 \r\n");
  }
  else {
    Serial.println("File already exists");  
  }
  file.close();
}

/*Écriture des lignes d'informations dans la carte SD*/
void ecriture(){
  dataMessage = String(Date) +";"
              + String(Heure) +";"
              + String(Temp) + ";" 
              + String(Hum) + ";" 
              + String(COV) + ";" 
              + String(Alde) + ";" 
              + String(CO2) + ";" 
              + String(PM_1) + ";" 
              + String(PM_25) + ";" 
              + String(PM_10) + "\r\n";
  Serial.println("Sauvegarde SD est OK");
  appendFile(SD, "/Valeur.csv", dataMessage.c_str());
}
////////////
/*Capteurs*/
////////////

/*Initialisation SPG40*/
void initSPG40(){
    sgp40.begin(Wire);
    // Récupération du numéro de série du capteur SGP40
    uint16_t serialNumber[3];
    uint8_t serialNumberSize = 3;
    uint16_t error = sgp40.getSerialNumber(serialNumber, serialNumberSize);
    Serial.print("Sampling interval (sec):\t");
    Serial.println(voc_algorithm.get_sampling_interval());
    Serial.println("");
    if (error) {
        Serial.print("Error trying to execute getSerialNumber(): ");
        // Gérer l'erreur si nécessaire
    } else {
        Serial.print("SerialNumber: ");
        for (size_t i = 0; i < serialNumberSize; i++) {
            Serial.print("0x");
            Serial.print(serialNumber[i], HEX);
            Serial.print(" ");
        }
    }
    uint16_t testResult;
    // Test d'auto-étalonnage du capteur SGP40
    error = sgp40.executeSelfTest(testResult);
    if (error) {
        Serial.print("Error trying to execute executeSelfTest(): ");
        // Gérer l'erreur si nécessaire
    } else if (testResult != 0xD400) {
        Serial.print("executeSelfTest failed with error: ");
        Serial.println(testResult, HEX);
    }
}

/*Fonction pour mesurer la valeur brute du signal SGP40 en mode faible consommation*/
void sgp40MeasureRawSignalLowPower(uint16_t compensationRh, uint16_t compensationT, uint16_t* error, int32_t voc_index) {
    uint16_t srawVoc = 0;
    *error = sgp40.measureRawSignal(compensationRh, compensationT, srawVoc);// Demande d'une première mesure pour chauffer la plaque
    if (*error) {
        return;
    }
    delay(140);// Délai de 170 ms pour laisser la plaque chauffer; la commande de mesure a un délai de 30 ms.
    *error = sgp40.measureRawSignal(compensationRh, compensationT, srawVoc); // Demande des valeurs de mesure
    if (*error) {
        return;
    }
    // Désactiver le chauffage
    *error = sgp40.turnHeaterOff();
    if (*error) {
        return;//on degage
    }
    voc_index = voc_algorithm.process(srawVoc); // Traitement des signaux bruts par l'algorithme d'indice de gaz VOC pour obtenir les valeurs de l'indice VOC
    COV = voc_index;
    Serial.println("COV: "+ String(COV));
}

/*Initialisation BME280*/
void initBME280(){
   // Initialisation du capteur BME280
    if (!bme.begin(0x76)) {
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
        while (1);
    }
}
/*Mesure Température et humidité*/
void capteurT() {
    // Mesure des valeurs BME280
    Temp = bme.readTemperature();
    Serial.println("Temperature = " + String(Temp)+ "°C");
    Hum = bme.readHumidity();
    Serial.println("Humidité = " + String(Hum) + "%");
}
/*Initialaisation SFA30*/
void initSFA30(){
   sfa3x.begin(Wire);
    // Démarrage de la mesure continue avec SFA3x
    int error = sfa3x.startContinuousMeasurement();
    if (error) {
        Serial.print("Error trying to execute startContinuousMeasurement(): ");
        // Gérer l'erreur si nécessaire
    }
}
/*Mesure Formaldéhyde*/
void capteurF() {
    // Mesure des valeurs SFA3x
    int16_t hcho;      // Formaldéhyde
    int16_t humidity;  // Humidité
    int16_t temperature;
    int error = sfa3x.readMeasuredValues(hcho, humidity, temperature);
    if (error) {
        Serial.print("Error trying to execute readMeasuredValues(): ");
        // Gérer l'erreur si nécessaire
    } else {
        Alde = hcho / 5.0;
        Serial.println("Forme Aldéhyde:" + String(Alde));
    }
}

/*Basile*/
void capteurC(){
  // Mesure de la valeur brute du signal SGP40 en mode faible consommation
    uint16_t error;
    char errorMessage[256];
    uint16_t compensationRh = 0x8000;  // Valeur de compensation à ajuster
    uint16_t compensationT = 0x6666;    // Valeur de compensation à ajuster
    int32_t voc_index = 0;
    // Appel de la fonction pour mesurer la valeur brute du signal SGP40
    sgp40MeasureRawSignalLowPower(compensationRh, compensationT, &error, voc_index);
}

/////////////////
/*Capteur Asfal*/
/////////////////
/*Initialisation des capteurs PM et CO2*/
void initSPS30(){
 // set driver debug level
  sps30.EnableDebugging(DEBUG);
  // Begin communication channel
  SP30_COMMS.begin();
  if (sps30.begin(&SP30_COMMS) == false) {
    Serial.println(F("Could not set I2C communication channel."));
    Serial.println(F("Program on hold"));
    while(true) delay(100000);
  }
  // check for SPS30 connection
  if (! sps30.probe()) {
    Serial.println(F("could not probe / connect with SPS30."));
    Serial.println(F("Program on hold"));
    while(true) delay(100000);
  }
  else {
    Serial.println(F("Detected SPS30."));
  }
  // reset SPS30 connection
  if (! sps30.reset()) {
    Serial.println(F("could not reset."));
    Serial.println(F("Program on hold"));
    while(true) delay(100000);
  }
  if (sps30.I2C_expect() == 4)
    Serial.println(F(" !!! Due to I2C buffersize only the SPS30 MASS concentration is available !!! \n"));

  if (airSensor.begin() == false) {
    Serial.println("Air sensor not detected. Please check wiring. Freezing...");
    while (1);
  }
  // Démarre les mesures avec un intervalle de 20 secondes
  airSensor.setMeasurementInterval(2);
}

/*Asfal*/
void read_PM(){
  struct sps_values val;
  uint8_t ret = sps30.GetValues(&val);
  ret = sps30.wakeup(); //Réveille du mode sommeille
  if (ret != SPS30_ERR_OK) {
    Serial.print(F("Error during reading values: "));
    char buf[80];
    sps30.GetErrDescription(ret, buf, 80);
    Serial.println(buf);
    return;
  }
  PM_1 = val.MassPM1;//
  PM_25 = val.MassPM2;//
  PM_10 = val.MassPM10;//
  char buffer[50];
  sprintf(buffer, "Valeur PM1: %.2f, PM2.5: %.2f, PM10: %.2f", PM_1, PM_25, PM_10);
  Serial.println(buffer);
  ret = sps30.sleep(); //entrer en mode sommeil
}

/*Asfal*/
void read_CO2 (){ 
  //Démarrer les mesures
  airSensor.setMeasurementInterval(2);
  // Attendre que les données soient disponibles
  while (!airSensor.dataAvailable()) {
    delay(1000); // Attente de 100 ms avant de vérifier à nouveau
  }
  CO2 = airSensor.getCO2();
  Serial.println("CO2: " + String(CO2) + "ppm");
  // Arrêter les mesures
  airSensor.setMeasurementInterval(0); // Mettre l'intervalle à 0 pour arrêter les mesures
}

///////////////
/*Gestion RTC*/
///////////////

int essai=0; 
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200* 1; //Décalage horaire : 3600 en hiver et 7200 en été
const int   daylightOffset_sec = 7200 * 0; //Décalage horaire : 3600 en hiver et 7200 en été
RTC_PCF8523 rtc; // Créez un objet pour le module RTC PCF8523

void initRTC (){
    // Initialiser le module RTC
  if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  // Synchroniser l'heure avec le serveur NTP
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  delay(2000); // Attendez que l'heure soit synchronisée
  
  // Obtenez l'heure actuelle du serveur NTP et réglez-la sur le RTC
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
  } 
  else {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min));
  }
}

// Lire l'heure du module RTC
void donnerHeure (){
  DateTime now = rtc.now();
  // variable date
  Date = (String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " ");
  // variable heure
  Heure = (String(now.hour())+ ":" + String(now.minute()) +"");
  // Horodater les mesures des capteurs avec l'heure du RTC
  Serial.println("RTC time: "+Date +Heure);
}

//////////////////////////
/*Gestion du serveur web*/
//////////////////////////
void serveurNTP(){
  WiFi.mode(WIFI_STA); // Optional
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(100);
    essai++;
    if (essai >= 30) 
    {
        Serial.println("Connexion échouée après plusieurs tentatives."); 
        break;
      }
  } 
}

String wifi;

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  wifi = WiFi.localIP().toString();
  Serial.println(wifi);
}

void comServeur(){
  server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
      Serial.println("Envoie vers le serveur.Ok");
    String data = "{\"temperature\": " + String(Temp) + 
                  ", \"humidity\": " + String(Hum) + 
                  ", \"CO2\": " + String(CO2) + 
                  ", \"COV\": " + String(COV) + 
                  ", \"FormeAlde\": " + String(Alde) + 
                  ", \"PM_1\": " + String(PM_1) + 
                  ", \"PM_25\": " + String(PM_25) + 
                  ", \"PM_10\": " + String(PM_10) + "}";
    request->send(200, "application/json", data);
  });
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/index.html", "text/html");
  });
  server.serveStatic("/", SD, "/");
  server.serveStatic("/styles.css", SD, "/styles.css");
  server.serveStatic("/script.js", SD, "/script.js");

  server.begin();
}

//////////////////
/*Gestion de IHM*/
//////////////////

// Broches pour l'écran TFT
#define TFT_SCK    18
#define TFT_MOSI   23
#define TFT_MISO   19
#define TFT_CS     5
#define TFT_DC     2
#define TFT_RESET  4
// Initialisation de l'afficheur
Arduino_ESP32SPI bus = Arduino_ESP32SPI(TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, TFT_MISO);
Arduino_ILI9341 display = Arduino_ILI9341(&bus, TFT_RESET);

// Étiquettes pour les données affichées
const char* labels[] = {"Temperature:","Humidite:","COV:","Forme Aldehyde:","CO2:","Pm_1:","Pm_2,5:","Pm_10:"};

char values[8][10]; // Tableau pour stocker les valeurs à afficher

void initEcran(){
    // Initialisation de l'afficheur
  display.begin();
  display.setRotation(3);
  display.fillScreen(WHITE);

  // Initialise les valeurs affichées à des chaînes vides
  for (int i = 0; i < 8; i++) {
    values[i][0] = '\0';
  }
}

void ecran(){
  // Met à jour les valeurs affichées
  snprintf(values[0], sizeof(values[0]), "%.2f dgC", Temp);
  snprintf(values[1], sizeof(values[1]), "%.2f %%", Hum);
  snprintf(values[2], sizeof(values[2]), "%d /25000", COV);
  snprintf(values[3], sizeof(values[3]), "%d ug/m3", Alde);
  snprintf(values[4], sizeof(values[4]), "%d ppm", CO2);
  snprintf(values[5], sizeof(values[5]), "%.2f ug/m3", PM_1);
  snprintf(values[6], sizeof(values[6]), "%.2f ug/m3", PM_25);
  snprintf(values[7], sizeof(values[7]), "%.2f ug/m3", PM_10);

  // Efface l'écran
  display.fillScreen(BLACK);

 // Afficher les étiquettes et les valeurs avec les couleurs correspondantes
    int yPos = 10; // Position Y pour les éléments
    display.setTextSize(2);
    display.setTextColor(WHITE);
    for (int i = 0; i < sizeof(labels) / sizeof(labels[0]); ++i) {
        display.setCursor(20, yPos);
        display.setTextColor(WHITE);
        display.print(labels[i]);
        // Définir la couleur en fonction de l'index de l'étiquette
        if (i == 2) { // COV
            long covValue = atol(values[i]);
            if (covValue > 10000) {
                display.setTextColor(RED);
            } else if (covValue >= 3000 && covValue <= 10000) {
                display.setTextColor(ORANGE);
            } else if (covValue >= 1000 && covValue < 3000) {
                display.setTextColor(YELLOW);
            } else if (covValue >= 300 && covValue < 1000) {
                display.setTextColor(GREENYELLOW);
            } else if (covValue == 300) {
                display.setTextColor(GREENYELLOW);
            } else {
                display.setTextColor(WHITE); // Couleur par défaut
            }
        } else if (i == 4) { // CO2
            float co2Value = atof(values[i]);
            if (co2Value > 1000) {
                display.setTextColor(RED);
            } else if (co2Value >= 600 && co2Value <= 1000) {
                display.setTextColor(ORANGE);
            } else if (co2Value >= 400 && co2Value < 600) {
                display.setTextColor(YELLOW);
            } else if (co2Value == 400) {
                display.setTextColor(GREENYELLOW);
            } else {
                display.setTextColor(WHITE); // Couleur par défaut
            }
        } else if (i == 3) { // Alde
            display.setTextColor(WHITE); // Couleur noire pour Alde
        } else {
            display.setTextColor(WHITE); // Couleur par défaut pour les autres étiquettes
        }
        display.print(values[i]);
        display.setTextColor(WHITE);
        yPos += 30; // Espacement vertical entre les étiquettes 
    }
   // Affichage de l'heure et de la date
    display.setTextSize(1);
    display.setCursor(214,202);
    display.print(Heure); 
    display.setCursor(214,215);
    display.print(Date);
    display.setCursor(214,228);
    display.print("IP" + wifi);
}

/*Audio*/
// Débit en bauds pour la communication série avec le SOMO2
#define GPS_BAUD 9600

// Broches RX et TX pour la communication série avec le SOMO2
#define RXD2 16
#define TXD2 17

// Objets pour la communication série avec le SOMO2
HardwareSerial SOMOSerial(2);

// Messages de contrôle pour le SOMO2
char play[]         = {0x7E, 0x0D, 0x00, 0x00, 0x00, 0xFF, 0xF4, 0xEF};
char next[]         = {0x7E, 0x01, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xEF};
char Pause[]        = {0x7E, 0x0E, 0x00, 0x00, 0x00, 0xFF, 0xF2, 0xEF};
char previous[]     = {0x7E, 0x02, 0x00, 0x00, 0x00, 0xFF, 0xFE, 0xEF};
char volumeup[]     = {0x7E, 0x04, 0x00, 0x00, 0x00, 0xFF, 0xFC, 0xEF};
char volumedown[]   = {0x7E, 0x05, 0x00, 0x00, 0x00, 0xFF, 0xFB, 0xEF};
char volumebas[]    = {0x7E, 0x06, 0x00, 0x00, 0x05, 0xFF, 0xF5, 0xEF};
char volumehaut[]   = {0x7E, 0x06, 0x00, 0x00, 0x1E, 0xFF, 0xDC, 0xEF};
char volumeselect[] = {0x7E, 0x06, 0x00, 0x00, 0x05, 0xFF, 0xF5, 0xEF};
char specifyeq[]    = {0x7E, 0x16, 0x00, 0x00, 0x00, 0xFF, 0xEA, 0xEF};
char stop[]         = {0x7E, 0x16, 0x00, 0x00, 0x00, 0xFF, 0xEA, 0xEF};
char specifypiste[] = {0x7E, 0x03, 0x00, 0x00, 0x01, 0xFF, 0xFC, 0xEF};

// Durée de lecture de chaque piste en secondes
const int DUREE_PISTE = 7;

// Fonction pour calculer la somme de contrôle
void checkSum(char piste){
  char Sum2 = 0;
  int Sum = 0;
  Sum = 0xFFFF - (0x03 + 0x00 + 0x00 + piste) + 1;
  Sum2 = Sum;
  specifypiste[6] = Sum2;
}
const byte piste1 = 0x01;
const byte piste2 = 0x02;
const byte piste3 = 0x03;
const byte piste4 = 0x04;
const byte piste5 = 0x05;
const byte piste6 = 0x06;
const byte piste7 = 0x07;
const byte piste8 = 0x08;
const byte piste9 = 0x09;
// Fonction pour envoyer des données via la communication série au SOMO2
void out_sci_sv(char *msg)
{
  for (int i = 0; i < 8; i++)
  {
    SOMOSerial.write(*msg);
    msg++;
  }
}

// Jouer une piste avec une couleur spécifique et une durée de lecture de 7 secondes
void playTrackWithDelay(char piste, uint16_t textColor) {
    display.setTextColor(textColor);
    specifypiste[4] = piste;
    checkSum(piste);
    out_sci_sv(specifypiste);
    delay(DUREE_PISTE * 1000); // Attendre 7 secondes
    out_sci_sv(volumehaut);
}

void alerteAudio(){ 
   // Vérifie la valeur du CO2 et joue la piste correspondante
    float co2Value = atof(values[4]);
    if (co2Value > 1000) {
        playTrackWithDelay(piste1, RED);
    } else if (co2Value >= 600 && co2Value <= 1000) {
        playTrackWithDelay(piste2, ORANGE);
    } else if (co2Value >= 400 && co2Value < 600) {
        playTrackWithDelay(piste3, YELLOW);
    } else if (co2Value == 400) {
        playTrackWithDelay(piste4, GREENYELLOW);
    }

    // Vérifie la valeur de COV et joue la piste correspondante
    long covValue = atol(values[2]);
    if (covValue > 10000) {
        playTrackWithDelay(piste5, RED);
    } else if (covValue >= 3000 && covValue <= 10000) {
        playTrackWithDelay(piste6, ORANGE);
    } else if (covValue >= 1000 && covValue < 3000) {
        playTrackWithDelay(piste7, YELLOW);
    } else if (covValue >= 300 && covValue < 1000) {
        playTrackWithDelay(piste8, GREENYELLOW);
    } else if (covValue == 300) {
        playTrackWithDelay(piste9, GREENYELLOW);
    }
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    initSDCard();
    initFile();
    initRTC();
    initWiFi();
    initBME280();//température
    initSFA30();//Formaldéhyde
    initSPG40();//COV
    initSPS30();//PM
    serveurNTP();
    initEcran();
    SOMOSerial.begin(GPS_BAUD, SERIAL_8N1, RXD2, TXD2);
    comServeur();
}

void loop() {
  /*Capteur Basile*/
    capteurT(); //Capteur de température
    capteurC(); //Capteur de COV
    capteurF(); // Capteur Forme Aldhéyde
    /*Capteur Asfal*/
    read_CO2(); //Capteur de CO2
    read_PM();  //Capteur de PM
    donnerHeure(); // RTC
    /*IHM Idem*/
    ecran(); //Mise à jour de l'écran
    alerteAudio();
    /*Guillemin*/
    ecriture();// écriture des valeurs dans la carte SD
    delay(RefreshCap/2);
}