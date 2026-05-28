#include <VS1053.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESP32_VS1053_Stream.h>
#include <PubSubClient.h>


/*Processeur Xtensa LX6 Execute le code Arduino et la stack réseau.
 Radio 2.4 GHz (PHY)Emet/reçoit les ondes electromagnetiques. 
 Couche MAC 802.11 Gere les trames WiFi (association, authentification). 
 Antenne PCBF il imprimé sur le circuit — convertit signal électrique vers onde radio. 
 RAM interne (520 Ko)Stocke les buffers réseau et la stack TCP/IP. 
 Flash 4 Mo Stocke les identifiants WiFi sauvegardés par WiFiManager */


//=========== Partie define et repondre au transmission ===============


#define VS1053_CS    32
#define VS1053_DCS   33
#define VS1053_DREQ  15


#define NOMBRE_STATIONS 7


ESP32_VS1053_Stream stream;        // Objet principal pour le streaming
// Ajoute cette ligne :
VS1053 player(VS1053_CS, VS1053_DCS, VS1053_DREQ);   // Pour acces aux registres bas niveau


// ================== WiFi & MQTT ==================
const char* mqttServer = "broker.emqx.io"; // partie connexion broker
const int   mqttPort   = 1883; // Port de sortie


WiFiClient espClient;           // Client WiFi pour MQTT
PubSubClient mqttClient(espClient);   // Client MQTT
/*VSD SP4 Processeur DSP dédié au décodage 
MP3/AACX ROM / Y ROM Memoire contenant les algorithmes de decodage 
DAC stereo Convertit le signal numérique en tension analogique Stereo 
Earphone DriverAmplifie le signal pour alimenter un casque */

int volume = 85;
uint8_t Bass_Amp = 10;
uint8_t Treble_Amp = 6;
uint8_t Bass_Freq = 14;
uint8_t Treble_Freq = 3;
int spatialMode = 0;     
int currentStation = 0;


// Les stations 
const char* stations[] = {
  "http://stream03.ustream.ca/cism128.mp3",
  "http://chisou-02.cdn.eurozet.pl/;",
  "http://streamer01.sti.usherbrooke.ca/cfak.mp3",
  "http://radios.rtbf.be/wr-c21-metal-128.mp3",
  "http://ecoutez.chyz.ca/mp3",
  "http://ice4.somafm.com/seventies-128-mp3",
  "http://lyon1ere.ice.infomaniak.ch/lyon1ere-high.mp3"
};
/*La structure de tes topics
radio / Sichard / [fonction]
  |        |          |
domaine  device    commande
Tes topics suivent une hiérarchie logique avec des ‘/’ :
*/
// ============================= Topics ==============================
const char* topicCommand = "radio/Sichard/cmd";  //lancer des commande
const char* topicVolume  = "radio/Sichard/volume"; // gerer le vol
const char* topicChaine  = "radio/Sichard/chaine";//pour bouton sation
const char* topicBass    = "radio/Sichard/bass";// pour slide bass
const char* topicTreble  = "radio/Sichard/treble"; // pour slide aigu
const char* topicSpatial = "radio/Sichard/spatial"; // pour bouton spa


unsigned long lastMsg = 0;


// ====================== fonction setTone ============================
/*uint8_t = entier non signé sur 8 bits → valeurs de 0 à 255. Les = 10, = 6 etc. 
Sont des valeurs par defaut — si on appelle setTone() sans arguments, ces valeurs sont utilisées automatiquement.

constrain(valeur, min, max) est une fonction Arduino qui limite une valeur dans un intervalle. 
Si quelqu'un envoie bass+ 20 fois de suite, Bass_Amp pourrait dépasser 15 — ce qui corromprait le registre du VS1053. 
Le constrain protège contre ça. 
*/

void setTone(uint8_t bassLevel = 10, uint8_t trebleLevel = 6,
             uint8_t bassFreq = 14, uint8_t trebleFreq = 3) {
  // Limitation stricte selon le datasheet VS1053
  bassLevel   = constrain(bassLevel,   0, 15);   // 0 à 15  (+15 dB max)
  trebleLevel = constrain(trebleLevel, 0, 15);
  bassFreq    = constrain(bassFreq,    2, 15);
  trebleFreq  = constrain(trebleFreq,  1, 15);
  uint8_t rtone[4] = {Treble_Amp, Treble_Freq, Bass_Amp, Bass_Freq};/*C'est un tableau de 4 octets qui correspond exactement au format attendu par le registre SCI_BASS du VS1053 */
  stream.setTone(rtone);   // Important : on utilise stream, pas player /*stream (ESP32_VS1053_Stream) gère le flux audio en cours. 
  Si on modifie les registres via player pendant que stream tourne, les deux bibliothèques peuvent entrer en conflit sur le bus SPI — d'où l'utilisation exclusive de stream.setTone(). */


  Serial.print("Tonalite => Basses: ");
  Serial.print(Bass_Amp);
  Serial.print(" | Aigus: ");
  Serial.println(Treble_Amp);
}




// ==================fonction setSpatialization ======================
void setSpatialization(int mode) {
  spatialMode = constrain(mode, 0, 3);


  uint16_t reg = player.readRegister(0x00);   // 0x00 = SCI_MODE
  reg &= ~0x00C0;                             // Efface les bits de spatialisation (bits 4 et 7)


  switch (spatialMode) {
    case 0:                                 // OFF
      Serial.println("Spatialisation : OFF");
      break;
    case 1:                                 // Léger
      reg |= 0x0010;                        // Bit 4 = 1
      Serial.println("Spatialisation : Léger");
      break;
    case 2:                                 // Moyen
      reg |= 0x0080;                        // Bits 7 = 1
      Serial.println("Spatialisation : Moyen");
      break;
    case 3:                                 // Fort
      reg |= 0x0090;                        // Bit 7+4 = 1
      Serial.println("Spatialisation : Fort");
      break;
  }


  player.writeRegister(0x00, reg);
}


// ====================== MQTT CALLBACK ======================
/*C'est le module ESP32-WROOM-32 intégré sur la carte Adafruit HUZZAH32 qui s'en charge. Il contient :
Un microprocesseur Xtensa LX6 dual-core (240 MHz)
Une radio WiFi 802.11 b/g/n 2.4 GHz intégrée
Une antenne PCB imprimée directement sur le module
C'est cette radio WiFi qui émet et reçoit les trames réseau contenant les messages MQTT.
*/
void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  msg.trim();


  Serial.print("MQTT reçu [");
  Serial.print(topic);
  Serial.print("] : ");
  Serial.println(msg);


  if (String(topic) != topicCommand) return;


  // //////////////////////// commande ////////////////////
  if (msg == "next" || msg == "NEXT") {
    currentStation = (currentStation + 1) % NOMBRE_STATIONS;
    playStation(currentStation);
  }
  else if (msg == "prev" || msg == "PREV" || msg == "previous") {
    currentStation = (currentStation - 1 + NOMBRE_STATIONS) % NOMBRE_STATIONS;
    playStation(currentStation);
  }
/*L operateur % (modulo) permet de boucler — quand on depasse la derniere station on revient à la premiere, et inversement. Le + NOMBRE_STATIONS sur le prev evite les nombres negatifs. */


  else if (msg == "bass+" || msg == "BASS+") {
    Bass_Amp = min(15, Bass_Amp + 1); setTone();
  }
  else if (msg == "bass-" || msg == "BASS-") {
    Bass_Amp = max(0, Bass_Amp - 1); setTone();
  }
  else if (msg == "treble+" || msg == "TREBLE+") {
    Treble_Amp = min(15, Treble_Amp + 1); setTone();
  }
  else if (msg == "treble-" || msg == "TREBLE-") {
    Treble_Amp = max(0, Treble_Amp - 1); setTone();
  }
  else if (msg == "spatial" || msg == "SPATIAL") {
    spatialMode = (spatialMode + 1) % 4;
    setSpatialization(spatialMode);
  }
  else if (msg == "default" || msg == "DEFAULT") {
    Bass_Amp = 10; Treble_Amp = 6; spatialMode = 0;
    setTone(); setSpatialization(0);
  }


  // /////////////////// slider à revoir /////////
  else if (msg.startsWith("treble")) {
// probleme de differenciation msg mqtt entre bouton et slider et cmd volume
    Treble_Amp = constrain(msg.substring(6).toInt(), 0, 15);
    setTone();
    Serial.print("aigu_slider : "); Serial.println(Treble_Amp);
  }
  else if (msg.startsWith("bass")) {
    Bass_Amp = constrain(msg.substring(4).toInt(), 0, 15);
    setTone();
    Serial.print("basse_slider : "); Serial.println(Bass_Amp);
  }
  else if (msg.startsWith("vol")) {
    if (msg == "vol+") volume = min(100, volume + 5);
    else if (msg == "vol-") volume = max(0, volume - 5);
    else volume = msg.substring(3).toInt();
   
    volume = constrain(volume, 0, 100);
    if (volume == 0) {
      stream.setVolume(100);
      Serial.println("mute");
    } else {
      stream.setVolume(volume);
      Serial.print("vol : "); Serial.println(volume);
    }
  }


  // //////////	Slider Volume à terminer/////////
  else if (isDigit(msg.charAt(0))) {
    volume = msg.toInt();
    volume = constrain(volume, 0, 100);
    if (volume == 0) {
      stream.setVolume(100);
      Serial.println("mute");
    } else {
      stream.setVolume(volume);
      Serial.print("vol : "); Serial.println(volume);
    }
  }


  publishState();
}


void reconnect() {
  while (!mqttClient.connected()) {
    Serial.print("Connexion MQTT...");
    String clientId = "ESP32Radio_" + String(random(0xffff), HEX);


    if (mqttClient.connect(clientId.c_str())) {
      Serial.println("OK");
      mqttClient.subscribe(topicCommand);
    } else {
      Serial.print("Échec rc=");
      Serial.println(mqttClient.state());
      delay(5000);
    }
  }
}


void publishState() {
  mqttClient.publish(topicVolume, String(volume).c_str(), true);
  mqttClient.publish(topicChaine, String(currentStation).c_str(), true);
  mqttClient.publish(topicBass,   String(Bass_Amp).c_str(), true);
  mqttClient.publish(topicTreble, String(Treble_Amp).c_str(), true);
  mqttClient.publish(topicSpatial,String(spatialMode).c_str(), true);
}




// Station 
void playStation(int station) {
  currentStation = station % NOMBRE_STATIONS;
/*L'opérateur modulo %
Le % calcule le reste d'une division entière. Exemples :
10 % 3 = 1   (10 = 3×3 + 1)
7  % 3 = 1   (7  = 3×2 + 1)
6  % 3 = 0   (6  = 3×2 + 0) 
Dans votre cas — NOMBRE_STATIONS = 7
station % 7 = toujours un résultat entre 0 et 6
Sans le modulo, si vous appuyez sur next depuis la station 6 (la dernière) : 
exemple : currentStation = 6 + 1 = 7 stations[7] → n'existe pas ! → crash ou comportement imprévisible 
*/
  Serial.print("Lecture station ");
  Serial.println(currentStation);
  stream.stopSong();
  delay(100);
  if (!stream.connectToHost(stations[currentStation])) {
    Serial.println("Échec connexion station");
  }
}


//=====================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\n\n=== Web Radio + MQTT ===");


  // WiFiManager
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  // wm.resetSettings();   // Decommente pour réinitialiser les WiFi




  bool res = wm.autoConnect("RadioESP32", "12345678");
  if (!res) {
    Serial.println("Échec connexion WiFi → Redémarrage");
    ESP.restart();
  }




  Serial.print("Connecté à : ");
  Serial.println(WiFi.SSID());
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());


  // MQTT mqttClient.setCallServer(broker.io.mx, port de sortie), mqttClient.setCallback(message de renvoye)
  mqttClient.setServer(mqttServer, mqttPort);
  mqttClient.setCallback(callback);


  // Initialisation du streaming
  if (!stream.startDecoder(VS1053_CS, VS1053_DCS, VS1053_DREQ)) {
    Serial.println("Erreur : VS1053 non détecté !");
    while(1) delay(100);
  }


  // Initialisation de player pour pouvoir accéder aux registres (spatialisation, etc.)
  SPI.begin();
  player.begin();
  player.switchToMp3Mode();


  stream.setVolume(volume);
  setTone();                    // Tonalité par défaut
  setSpatialization(0);         // Spatialisation OFF au démarrage




  playStation(0);               // Démarre la première station
}


///////////////////////////////////////////////////////////////////////
void loop() {
  stream.loop();                    // Obligatoire !




  // MQTT
  if (!mqttClient.connected()) {
    reconnect();
  }
  mqttClient.loop();




  // Contrôles clavier
  if (Serial.available()) {
    char c = Serial.read();




    if (c == 'n' || c == 'N') playStation(currentStation + 1);
    if (c == 'p' || c == 'P') playStation(currentStation - 1);


    if (c == 'y' || c == 'Y') { if (volume < 100) volume++; stream.setVolume(volume); }
    if (c == 'b' || c == 'B') { if (volume > 0) volume--; stream.setVolume(volume); }


    if (c == 'g' || c == 'G') { Bass_Amp = min(15, Bass_Amp + 1); setTone(); }
    if (c == 'f' || c == 'F') { Bass_Amp = max(0, Bass_Amp - 1); setTone(); }
    if (c == 'j' || c == 'J') { Treble_Amp = min(15, Treble_Amp + 1); setTone(); }
    if (c == 'h' || c == 'H') { Treble_Amp = max(0, Treble_Amp - 1); setTone(); }


    if (c == 'd' || c == 'D') { Bass_Amp = 10; Treble_Amp = 6; setTone(); }
    if (c == 's' || c == 'S') { spatialMode = (spatialMode + 1) % 4; setSpatialization(spatialMode); }
  }


  // Publication périodique
  if (millis() - lastMsg > 8000) {
    lastMsg = millis();
    publishState();
  }
}
