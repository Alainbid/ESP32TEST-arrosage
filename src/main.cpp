#include <Arduino.h>
#include "BluetoothSerial.h"
#include <EEPROM.h>
 #include <Wire.h>
#include <RTClib.h>
 #include <SPI.h>



#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif
#define EEPROM_SIZE 512 // Define the size of EEPROM (ESP8266 has up to 512 bytes of EEPROM)
#define STRING_ADDR 0   // Start address in EEPROM de la commande
#define AJUSTEMENT_ADDR (495) // Zone de stockage dans EEPROM  du % d'arrosage saisonnier
#define CHECKSUM_ADDR (500) // Zone de stockage de la checksum de la commande
#define DATASIZE_ADDR (505) // Zone de stockage dans EEPROM de la longueur de la commande

RTC_DS3231 rtc;

BluetoothSerial SerialBT;
bool test = false;
bool dansLeSetup = true;


//WiFiUDP ntpUDP;
// Créez un objet NTPClient pour interroger un serveur NTP
//NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000); // décalage en secondes et intervalle de mise à jour
//unsigned long epochTime = 0L;


// Define the relay pins
const int relayPins[] = {26, 25, 33, 32};
//  PINS 26 = Potager  25 = Parking  33 = Portillon  32 = Grand Tour
// nombre de relay
const int numRelays = sizeof(relayPins) / sizeof(relayPins[0]);

int compteur = 0;
unsigned long attente = 0;
int debut = 0;
int fois[] = {0, 0, 0, 0};
String command = "";
int actuel = 0;
int relayCommandCount = 0;
int manuel5mn = 0;
int quelleZone =0;

// en fonction de la saison un pourcentage de modification de la durée
// float ajustementSaison = 0.5F; // soit un coefficient de 50%
float ajustementSaison = 1.00F; // soit un coefficient de 100%


struct RelayCommand {
  int action; //N° du relay
  int start;  //heure début en s depuis 0h00
  unsigned long duration; // durée en secondes
  int nbrDeFois; // répétition 1 fois ... 2 , 3
  bool inProgress; // nouvel état
  unsigned long startTime; // début en millisecondes
  bool done;
};
RelayCommand relayCommands[12]; //4 relais x 3 fois


unsigned int calculateChecksum(String data) {
  unsigned int checksum = 0;
      for (unsigned int i = 0; i < data.length(); i++) {
        checksum += data[i];
      }
  return checksum;
}

//stockage de la commande dans EEPROM
void storeDataWithChecksum(String data) {
 int  dataSize = data.length();
  // Write string to EEPROM
    for (int i = 0; i < dataSize; i++) {
      EEPROM.write(STRING_ADDR + i, data[i]);
    }
    EEPROM.write(STRING_ADDR + dataSize , '\0'); // Null-terminate the string
  // Calculate and store checksum
  unsigned int checksum = calculateChecksum(data);
  EEPROM.put(CHECKSUM_ADDR, checksum);
  EEPROM.put(DATASIZE_ADDR, dataSize);
  EEPROM.commit(); // Save changes to EEPROM

  if(test){ 
    Serial.println("\n\nDonnées stockées dans EEPROM: " + data +"\n\n"); 
   // Serial.println("Checksum written to EEPROM: " + String(checksum));
  }
}

//stockage de ajustementsaison dans EEPROM
void storeAjustementSaison(float ajustement) {
  EEPROM.put(AJUSTEMENT_ADDR, ajustement);
  EEPROM.commit(); // Save changes to EEPROM
  if(test){ 
    Serial.print(" ajustement stocké dans EEPROM " ); 
    Serial.println(String( ajustementSaison)); 
  }
}

//lecture de la dernière commande mémorisée en EEPROM
String retrieveDataWithVerification() {
  int dataSize = 0;
  EEPROM.get(DATASIZE_ADDR, dataSize);
  char readData[dataSize];
  // Read string from EEPROM
  for(int i = 0; i< dataSize ; i++){
    readData[i] = EEPROM.read(STRING_ADDR + i);
  }
  readData[dataSize] = '\0'; //le dernier cractère doit être '/0'
  // Read stored checksum
  unsigned int storedChecksum;
  EEPROM.get(CHECKSUM_ADDR, storedChecksum);
  // lire valeur ajustement saison
  EEPROM.get(AJUSTEMENT_ADDR, ajustementSaison);
  Serial.print("\najustement saisonnier = "  );
  Serial.println(String(ajustementSaison ));
  // Calculate checksum of retrieved data
  String retrievedData = String(readData);

  unsigned int calculatedChecksum = calculateChecksum(retrievedData);
  if(test){
    Serial.println("\nValeur ajustement saison" + String( ajustementSaison));
    Serial.println("checksum calculée  " + String(calculatedChecksum));
    }
  //Verify checksum
  if (storedChecksum == calculatedChecksum) {
    Serial.println(" data lus dans EEPROM " + retrievedData);
  }
  else{
    Serial.println(" data ERREUR de checksum " + retrievedData);
  }
  return retrievedData;
}


//exemple de  commande  "21000$1/21020/10/0/&4/21035/8/0/&2/21045/5/0/&"
// chaque block de commande est délimité par le caractère :  &
// il peur y avoir jusqu'à 4 relais  x  3 repetitions possibles
// composition du bloc de commande : 
//relay numéro / heure départ en secondes / durée en secondes / répétitions 0 1 2
void interpretationDesCommnades(String command) {
  int RTCcompteur =0;
  String minutesDebut = "";
  relayCommandCount = 0;
  int endIndex = command.indexOf('$');
  // $ sépare le début de command de la programmation
  // permet de mettre compteur à l'heure
  DateTime now = rtc.now();// on récupère an jour  mois
  minutesDebut = command.substring(0, endIndex);
    //ajuster le  RTC aux denières valeurs
    // si après reboot/reset on ne modifie pas le RTC
    if(!dansLeSetup) {// on ne vient pas  du setup donc 
      //synchronisation des secondes du RTC avec la date actuelle
      
      compteur =  minutesDebut.toInt(); 
      if(test)Serial.println("le compteur avec setup = false " + String(compteur));
      int hr =abs( compteur / 3600);
      int mn = abs((compteur % 3600)/60);
      int sec = compteur - (hr*3600 + mn*60);
      
       rtc.adjust(DateTime(now.year(),now.month(),now.day(),hr,mn,sec));
      DateTime actuel = rtc.now();
      RTCcompteur = actuel.hour()*3600 + actuel.minute()*60 + actuel.second();
      
   //si la différence entre RTC et compteur est supérieure à 30 mn 
   //en particulier au changement d'horaire en été/hiver
   // on réajuste le RTC 
    // if(abs(RTCcompteur - compteur) > 1800 ){
    //   int hr = compteur / 3600;
    //   int mn = (compteur % 3600)/60;
    //   rtc.adjust(DateTime(now.year(),now.month(),now.day(),hr,mn,0));
    //   Serial.println("\nRTC ajusté à la valeur du compteur  " + String(compteur));
    // }

      compteur = RTCcompteur;
    }
    Serial.println();
    Serial.println("\n compteur " + String(compteur) + "\n");
    Serial.print(now.year(), DEC);
    Serial.print('/');
    Serial.print(now.month(), DEC);
    Serial.print('/');
    Serial.print(now.day(), DEC);
    Serial.print(" ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.println();

   
  unsigned int startIndex = 0;
  int blocIndex = command.indexOf('&');
  // on décompose les commandes
  startIndex = minutesDebut.length() + 1;

  while (startIndex < command.length()) { // fin de command ?
    if (blocIndex == -1) { // fin du bloc de commandes du relay
      blocIndex = command.length();
    }
    // découpage de chaque commandes du relay encours
    String block = command.substring(startIndex, blocIndex);
    Serial.println(" bloc " + block);
    int startSubIndex = 0;
    int endSubIndex = block.indexOf('/');

    // quel relay ?
    if (endSubIndex != -1) {
      relayCommands[relayCommandCount].action = block.substring(startSubIndex, endSubIndex).toInt();
      startSubIndex = endSubIndex + 1;
      endSubIndex = block.indexOf('/', startSubIndex);
    }
    // heure de début
    if (endSubIndex != -1) {
      relayCommands[relayCommandCount].start = block.substring(startSubIndex, endSubIndex).toInt();
      startSubIndex = endSubIndex + 1;
      endSubIndex = block.indexOf('/', startSubIndex);
    }
    // durée
    if (endSubIndex != -1) {
      int x = block.substring(startSubIndex, endSubIndex).toInt();
      relayCommands[relayCommandCount].duration =(int)(x * ajustementSaison) ;
      startSubIndex = endSubIndex + 1;
      endSubIndex = block.indexOf('/', startSubIndex);
    }
    // nombre de fois 1,2,3 ?
    if (endSubIndex != -1) {
      relayCommands[relayCommandCount].nbrDeFois = block.substring(startSubIndex, endSubIndex).toInt();
      relayCommands[relayCommandCount].inProgress = false;
      relayCommands[relayCommandCount].done = false;
    }
    relayCommandCount++;

    startIndex = blocIndex + 1; 
    // bloc de commande du relay suivant
    blocIndex = command.indexOf('&', startIndex);
  }
}


void executionDeLaCommande(RelayCommand &relayCmd) {
  //if(test) {Serial.println("\n\ndans executionDeLaCommande Relay Numéro " + String(relayCmd.action ) );}
  int relayNumber  = relayCmd.action - 1; // on commence à indice 0 dans le tableau des relais
    //on est à l'heure d'arrosage
     if (!relayCmd.inProgress && !relayCmd.done && compteur == relayCmd.start) {
      relayCmd.startTime = millis(); // Start timing
      digitalWrite(relayPins[relayNumber], HIGH); // Turn relay on
      Serial.println("\n\nRelay " + String(relayNumber + 1) + " is ON pour " + String(relayCmd.duration) + " seconds\n");
      relayCmd.inProgress = true;//arrosage en cours
    }
    // on a atteind la fin de l'arrosage
    if (relayCmd.inProgress &&( millis() - relayCmd.startTime) >= (relayCmd.duration * 1000)) {
      digitalWrite(relayPins[relayNumber], LOW); // Turn relay off
      Serial.println("\n\nRelay " + String(relayNumber + 1) + " is OFF\n");
      relayCmd.inProgress = false;//arrosage terminé
      fois[relayNumber]++; // on incrémente la commande est terminée pour ce relay
      relayCmd.done = true; // Command terminée
    }
}



void setup() {

  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  attente= millis();
  Serial.begin(115200);
  Serial.println("ESP32 is running!");
  SerialBT.begin("ESP32 Bluetooth");// BLUETOOTH activé
  Serial.println("The device started, now you can pair it with bluetooth!");


   Wire.begin(21, 27); // SDA (GPIO12), SCL (GPIO27)
for (int i =0 ; i<10; i++)
   { if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    delay(100);
    }
  }
  
  
//if (test){
      // WiFi.begin(ssid, password);
      // int wifiTimeout = 0;
      // while (WiFi.status() != WL_CONNECTED && wifiTimeout < 20) {
      //   delay(1000);
      //   Serial.println("Connexion au Wi-Fi...");
      //   wifiTimeout++;
      // }
      // Serial.print("Adresse MAC de l'ESP8266: ");
      // Serial.println(WiFi.macAddress());
      // Serial.print("Adresse IP de l'ESP8266: ");
      // Serial.println(WiFi.localIP());
  //}

  // Initialisez les pins des relais à 0
  for (int i = 0; i < numRelays; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], LOW); // Relays are off initially
  }
    
  // lecture des données stockées dans EEPROM 
  command = retrieveDataWithVerification();
  if(test) Serial.println("\n début commande stockée dans EEPROM :" + command);
  // Parse the command once
  dansLeSetup = true;
  interpretationDesCommnades(command);//true > donc dans le setup()
}

void loop() {

  if(SerialBT.available()){
    String nouvelleCommande="";
    nouvelleCommande = SerialBT.readStringUntil('\n'); // Read the nouvelleCommande string until a newline character
    int cmd = nouvelleCommande.charAt(0);
    switch (cmd) {

          case 97:{
            //on a reçu la lettre 'a' lecture de EEPROM
            // ajout ajustement saisonnier
            // Serial.println( "\nEEPROM = " + command  );
            // Serial.println( "EEPROM ajustement saisonnier = " + String(ajustementSaison)  );
            SerialBT.println( "\n" + String(ajustementSaison) + "%" + command +"\n");
          break;}
          
          case 122:{
            // reçu  "z" RAZ  de la programmation
            for (int i = 0; i < numRelays; i++) {
                digitalWrite(relayPins[i], LOW); // Relays are off initially
              }
            command = "3600$1/3615/0/0/&";
            storeDataWithChecksum(command );// backup dans EEPROM
          break;}

          case 109:{// reçu  "m" manuel5mn tester la programmation
                quelleZone = nouvelleCommande.substring(1,2).toInt();
                int duree = nouvelleCommande.substring(2).toInt();
                
                 Serial.println("\n\n arrosage  " + String(quelleZone) + " pour " + String(duree) + " mn\n" );
                if(manuel5mn == 0){
                  for (int i = 0; i < numRelays; i++) {//tous les relays à 0
                    digitalWrite(relayPins[i], LOW); // Relays are off initially
                  }
                }
                
                if(duree == 0 ){
                  digitalWrite(relayPins[quelleZone], LOW);}
                else {
                  digitalWrite(relayPins[quelleZone], HIGH); // Arrosage de la zone
                }
               if(manuel5mn == 0){ manuel5mn=millis()+(duree*60*1000);}
          break;}
          
          case 37:{
            // reçu "%"  modification de % d'ajustement saisonnier
            String s = nouvelleCommande.substring(1);
            float x = s.toFloat();
            nouvelleCommande="";
            ajustementSaison = x / 100;
                  Serial.print("\n ajustement saisonnier = "  );
                  Serial.println(ajustementSaison );
            storeAjustementSaison(ajustementSaison);
            break;}
        
          default:{
                if(nouvelleCommande.length() > 8){
                  dansLeSetup = false;
                for (int i = 0; i < 4; i++) {
                  fois[i] = 0; // RAZ du tableau fois[]
                }
                  command = nouvelleCommande;
                  storeDataWithChecksum(command);// backup dans EEPROM
                  Serial.println("\ncommande  reçue : "+ command);
                  interpretationDesCommnades(command );
                }
            break;}
    }
  }

  // Add a delay for readability
  if(millis() >= attente + 1000)
  {
    attente = millis();
    DateTime now = rtc.now();
    Serial.print("   ");
    Serial.print(now.hour(), DEC);
    Serial.print(':');
    Serial.print(now.minute(), DEC);
    Serial.print(':');
    Serial.print(now.second(), DEC);
    Serial.print(" > ");
   
    compteur = now.hour()*3600 + now.minute()*60 + now.second();
    if( compteur < 60) { // RAZ  du compteur à minuit
      for(int i=0; i<4; i++){fois[i] = 0;}
    }
  
    if( manuel5mn != 0 && millis() > manuel5mn){
      for (int i = 0; i < numRelays; i++) {//tous les relays à 0
        digitalWrite(relayPins[i], LOW); // Relays are off initially
        }
        manuel5mn = 0;
    }
      Serial.print(String(compteur));
  }

  //Iterate over each relay command and execute it if conditions are met
  for (int i = 0; i < relayCommandCount; i++) {
    RelayCommand& relayCmd = relayCommands[i];
    executionDeLaCommande(relayCmd);
  }
}
