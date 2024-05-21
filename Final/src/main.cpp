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

/*Port SPI*/
#define SCK  18
#define MISO  19
#define MOSI  23
#define CS  32

SPIClass spi = SPIClass(VSPI);

/*Definition*/
#define DEBUG 0
#define SP30_COMMS Wire
#define USE_50K_SPEED 1

void read_PM(); 
void read_CO2 (); 


/*Initialisation des fonctions*/
SensirionI2CSgp40 sgp40;
SensirionI2CSfa3x sfa3x;
Adafruit_BME280 bme;
SPS30 sps30;
SCD30 airSensor;

float sampling_interval = 1.f;
VOCGasIndexAlgorithm voc_algorithm(sampling_interval);
String dataMessage;
int Refresh = 60000; // 1min = 60000
int RefreshCap = 100; // 30 seconde = 30000

// Replace with your network credentials
const char* ssid = "WIFI-CIEL"; // test
const char* password = "alcasarciel"; //alcasarciel

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

int essai=0; 
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 7200* 1; //Décalage horaire : 3600 en hiver et 7200 en été
const int   daylightOffset_sec = 7200 * 0; //Décalage horaire : 3600 en hiver et 7200 en été

RTC_PCF8523 rtc; // Créez un objet pour le module RTC PCF8523


//Variable de fonctionement
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

/////////////////////////////////
/*Initialisation de la carte SD*/
/////////////////////////////////
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
    writeFile(SD, "/Valeur.csv", "Date; Heure; Température; Humidité; Indice de COV; Forme Aldéhyde; CO²; PM_2.5; PM_1; PM_10 \r\n");
  }
  else {
    Serial.println("File already exists");  
  }
  file.close();
}

/*Écriture des lignes d'informations dans la carte SD*/
void ecriture(){
  dataMessage = String(Date) +";"+ String(Heure) +";"+ String(Temp) + ";" + String(Hum) + ";" + String(COV) + ";" + String(Alde) + ";" + String(CO2) + ";" + String(PM_1) + ";" + String(PM_25) + ";" + String(PM_10) + "\r\n";
  Serial.print("Sauvegarde: ");
  Serial.println(dataMessage);
  appendFile(SD, "/Valeur.csv", dataMessage.c_str());
}
///////////////////////////////
/*Initialisation des capteurs*/
///////////////////////////////

// Fonction pour mesurer la valeur brute du signal SGP40 en mode faible consommation
void sgp40MeasureRawSignalLowPower(uint16_t compensationRh, uint16_t compensationT, uint16_t* error, int32_t voc_index);

void initSPG40(){
 // Initialisation du capteur SGP40
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
        Serial.println();
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

void initSFA30(){
   sfa3x.begin(Wire);
    // Démarrage de la mesure continue avec SFA3x
    int error = sfa3x.startContinuousMeasurement();
    if (error) {
        Serial.print("Error trying to execute startContinuousMeasurement(): ");
        // Gérer l'erreur si nécessaire
    }
}

void initBME280(){
   // Initialisation du capteur BME280
    if (!bme.begin(0x76)) {
        Serial.println("Could not find a valid BME280 sensor, check wiring!");
        while (1);
    }
}

void initCAp(){
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

///////////////////////
/*Mesure des capteurs*/
///////////////////////

/*Basile*/
// Fonction pour mesurer la valeur brute du signal SGP40 en mode faible consommation
void sgp40MeasureRawSignalLowPower(uint16_t compensationRh, uint16_t compensationT, uint16_t* error, int32_t voc_index) {
    uint16_t srawVoc = 0;
    // Demande d'une première mesure pour chauffer la plaque (ignorant le résultat)
    *error = sgp40.measureRawSignal(compensationRh, compensationT, srawVoc);
    if (*error) {
        return;
    }
    // Délai de 170 ms pour laisser la plaque chauffer.
    // En gardant à l'esprit que la commande de mesure inclut déjà un délai de 30 ms
    delay(140);
    // Demande des valeurs de mesure
    *error = sgp40.measureRawSignal(compensationRh, compensationT, srawVoc);
    if (*error) {
        return;
    }
    // Désactiver le chauffage
    *error = sgp40.turnHeaterOff();
    if (*error) {
        return;//on degage
    }
    // Traitement des signaux bruts par l'algorithme d'indice de gaz VOC pour obtenir les valeurs de l'indice VOC
    voc_index = voc_algorithm.process(srawVoc);
    COV = voc_index;
    Serial.println("COV: "+ String(COV));
}

/*Basile*/
void capteurT() {
    // Mesure des valeurs BME280
    Temp = bme.readTemperature();
    Serial.println("Temperature = " + String(Temp)+ "°C");
    Hum = bme.readHumidity();
    Serial.println("Humidité = " + String(Hum) + "%");
}

/*Basile*/
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

/*Asfal*/
void read_PM(){
  struct sps_values val;
  uint8_t ret = sps30.GetValues(&val);
  //Serial.println("réveille");
  ret = sps30.wakeup();
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
  //Serial.println("Entering sleep-mode");
  ret = sps30.sleep(); 
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
  // Lire et afficher les données
  Serial.println("CO2: " + String(CO2)+ "ppm");
  // Arrêter les mesures
  airSensor.setMeasurementInterval(0); // Mettre l'intervalle à 0 pour arrêter les mesures
}

// Lire l'heure du module RTC
void donnerHeure (){
  DateTime now = rtc.now();
  // Affichage de la date
  Date = (String(now.day()) + "/" + String(now.month()) + "/" + String(now.year()) + " ");
  // Affichage de l'heure
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

void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}
String generateData() {
  // Measure sensors data
  capteurT();
  read_PM();
  // Generate JSON string
  String data = "{\"temperature\": " + String(Temp, 2) + ", \"humidity\": " + String(Hum, 2) + ",\"CO2\": " + String(CO2) + ", \"COV\": " + String(COV) + "\"FormeAlde\": " + String(Alde) + ",\"PM_1\": " + String(PM_1, 2) + ", \"PM_25\": " + String(PM_25, 2) + ", \"PM_10\": " + String(PM_10, 2) + "}";
  return data;
}

void setup() {
    Serial.begin(115200);
    Wire.begin();
    initSDCard();
    initFile();
    initBME280();
    initSFA30();
    initSPG40();
    initCAp();
    serveurNTP();
    initRTC();
   /* initWiFi();
    server.on("/data", HTTP_GET, [](AsyncWebServerRequest *request) {
    capteurT();
    request->send_P(200, "application/json", generateData().c_str());
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SD, "/index.html", "text/html");
  });

  server.serveStatic("/", SD, "/");
  server.serveStatic("/styles.css", SD, "/styles.css");
  server.serveStatic("/script.js", SD, "/script.js");
  server.begin();*/
}

void loop() {
  /*Capteur Basile*/
    capteurT(); 
    capteurC();
    capteurF();
    /*Capteur Asfal*/
    read_CO2();
    read_PM();
    donnerHeure();
    /*Capteur Idem*/

    /*Carte SD*/
    ecriture();
    /*Refresh*/
    delay(RefreshCap);
}