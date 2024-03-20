/*Blibiothèque*/
#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2CSgp40.h>
#include <SensirionI2CSfa3x.h>
#include <Adafruit_BME280.h>
#include <VOCGasIndexAlgorithm.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "time.h"

/*Port SPI*/
#define SCK  18
#define MISO  19
#define MOSI  23
#define CS  32

//Variable des mesures
float Temp;
int Ald;
float Hum;
int Date = 14;
int Heure = 18;
int COV = 2;
int CO2 = 9;
int PM_25 = 100;
int PM_1 = 55 ;
int PM_10 = 12 ;

/*Initialisation des fonctions*/
SensirionI2CSgp40 sgp40;
SensirionI2CSfa3x sfa3x;
Adafruit_BME280 bme;
float sampling_interval = 1.f;
VOCGasIndexAlgorithm voc_algorithm(sampling_interval);
String dataMessage;
int Refresh = 60000; // 1min
SPIClass spi = SPIClass(VSPI);

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
  dataMessage = String(Date) +";"+ String(Heure) +";"+ String(Temp) + ";" + String(Hum) + ";" + String(COV) + ";" + String(Ald) + ";" + String(CO2) + ";" + String(PM_25) + ";" + String(PM_1) + ";" + String(PM_10) + "\r\n";
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
    uint16_t serialNumber[3];
    uint8_t serialNumberSize = 3;
    // Obtention du numéro de série du capteur SGP40
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
  // Initialisation du capteur SFA30
    sfa3x.begin(Wire);
    // Démarrage de la mesure continue avec SFA30
    uint16_t error = sfa3x.startContinuousMeasurement();
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

///////////////////////
/*Mesure des capteurs*/
///////////////////////

void MesureSGP40(){
  // Mesure de la valeur brute du signal SGP40 en mode faible consommation
  uint16_t error;
  char errorMessage[256];
  uint16_t compensationRh = 0x8000;  // Valeur de compensation à ajuster
  uint16_t compensationT = 0x6666;   // Valeur de compensation à ajuster
  int32_t voc_index = 0;
  // Appel de la fonction pour mesurer la valeur brute & transformer du signal SGP40
  sgp40MeasureRawSignalLowPower(compensationRh, compensationT, &error, voc_index);

}

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
    Serial.print("srawVOC: ");
    Serial.println(srawVoc);
    // Désactiver le chauffage
    *error = sgp40.turnHeaterOff();
    if (*error) {
        return;
    }
    // Traitement des signaux bruts par l'algorithme d'indice de gaz VOC pour obtenir les valeurs de l'indice VOC
    voc_index = voc_algorithm.process(srawVoc);
    int COV = voc_index;
    Serial.print("COV Indice: ");
    Serial.println(COV);
}

void MesureSFA30(){
// Mesure des valeurs SFA3x
  delay(1000);
  int16_t hcho;
  int16_t humidity;
  int16_t temperature;
  int16_t error = sfa3x.readMeasuredValues(hcho, humidity, temperature);
  if (error) {
        Serial.print("Error trying to execute readMeasuredValues(): ");
        // Gérer l'erreur si nécessaire
    } else {
        Ald = hcho / 5.0;
        Serial.print("Hcho:");
        Serial.print(Ald);
        Serial.println("\t");
    }
}

void MesureBME280(){
  // Mesure des valeurs BME280
    Serial.println("\t");
    Serial.println("\t");
    Temp = bme.readTemperature();
    Serial.print("Temperature = ");
    Serial.print(Temp);
    Serial.println(" °C");

    Hum = bme.readHumidity();
    Serial.print("Humidité = ");
    Serial.print(Hum);
    Serial.println(" %");
}


void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    Wire.begin();
    initSDCard();
    initFile();
    initBME280();
    initSFA30();
    initSPG40();
}

void loop() {
    MesureSFA30();
    MesureBME280();
    MesureSGP40();
    ecriture();
    delay(Refresh);
}