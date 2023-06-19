/*
 Basic ESP8266 MQTT example
 This sketch demonstrates the capabilities of the pubsub library in combination
 with the ESP8266 board/library.
 It connects to an MQTT server then:
  - publishes "hello world" to the topic "outTopic" every two seconds
  - subscribes to the topic "inTopic", printing out any messages
    it receives. NB - it assumes the received payloads are strings not binary
  - If the first character of the topic "inTopic" is an 1, switch ON the ESP Led,
    else switch it off
 It will reconnect to the server if the connection is lost using a blocking
 reconnect function. See the 'mqtt_reconnect_nonblocking' example for how to
 achieve the same result without blocking the main loop.
 To install the ESP8266 board, (using Arduino 1.6.4+):
  - Add the following 3rd party board manager under "File -> Preferences -> Additional Boards Manager URLs":
       http://arduino.esp8266.com/stable/package_esp8266com_index.json
  - Open the "Tools -> Board -> Board Manager" and click install for the ESP8266"
  - Select your ESP8266 in "Tools -> Board"
*/
#include <WiFi.h>
#include <EEPROM.h>
#include "PubSubClient.h"
#include "ArduinoJson.h"
#include "time.h"
#include "Arduino.h"
#include "IRsend.h"
#include "IRrecv.h"
#include "IRremoteESP8266.h"
#include "IRutils.h"
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
#include "Ticker.h"

unsigned long lastMsg = 0;
#define MSG_BUFFER_SIZE (50)
char msg[MSG_BUFFER_SIZE];
int value = 0;
bool estado = true;

//  VARIABLES :
//  PARA CONEXION WIFI
const char* ssid = "miredwifi";
const char* password = "__123456987__";

WiFiClient espClient;

//  DIRECCION SERVIDOR MQTT
const char* mqtt_server = "192.168.254.1";

PubSubClient client(espClient);

//  SERVIDOR Y CLIENTE NTP A RTC
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = -4*3600;
const int   daylightOffset_sec = 0*3600;
//  INFRARROJO RECEPTOR TRANSMISOR
const uint16_t kRecvPin = 14;
const uint16_t kIrLedPin = 4;
// As this program is a special purpose capture/resender, let's use a larger
// than expected buffer so we can handle very large IR messages.
// i.e. Up to 512 bits.
const uint16_t kCaptureBufferSize = 1024;
// kTimeout is the Nr. of milli-Seconds of no-more-data before we consider a
// message ended.
const uint8_t kTimeout = 50;  // Milli-Seconds
// kFrequency is the modulation frequency all messages will be replayed at.
const uint16_t kFrequency = 38000;  // in Hz. e.g. 38kHz.
// The IR transmitter.
IRsend irsend(kIrLedPin);
// The IR receiver.
IRrecv irrecv(kRecvPin, kCaptureBufferSize, kTimeout, false);
// Somewhere to store the captured message.
decode_results results;

//  MEMORIA EEPROM
#define EEPROM_SIZE 128    // Define the size of the EEPROM memory
bool recv_nombre = true;

//  PANTALLA OLED SSD1306
// 0X3C+SA0 - 0x3C or 0x3D
#define I2C_ADDRESS 0x3C
// Define proper RST_PIN if required.
#define RST_PIN -1
SSD1306AsciiWire oled;

// INTERRUPCIONES INTERNAS
Ticker reloj;
Ticker toggler;
Ticker changer;
float periodoReloj = 0.5;  //seconds
const float togglePeriod = 5; //seconds

void setup() {
  //  *********/*/*/*/*/******************/*/*/*/*/**************
  //  INICIALIZACION DE MODULOS
  //  *********/*/*/*/*/******************/*/*/*/*/**************
  //  INICIALIZACION PANTALLA OLED SSD1306 INICIO
  Wire.begin();
  Wire.setClock(400000L);
  oled.begin(&Adafruit128x64, I2C_ADDRESS);
  oled.setFont(System5x7);
  // Set auto scrolling at end of window.
  oled.setScrollMode(SCROLL_MODE_AUTO);
  //  INICIALIZACION PANTALLA OLED SSD1306 FIN
  Serial.begin(115200);

  //  CONEXION WIFI INICIO
  delay(10);
  oled.clear();
  Serial.println();
  Serial.print("Conectando a ");
  Serial.println(ssid);
  oled.print("Conectando a ");
  oled.println(ssid);

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    oled.print(".");
  }
  //randomSeed(micros());
  Serial.println("");
  Serial.println("WiFi conectado");
  Serial.println("Direccion IP: ");
  Serial.println(WiFi.localIP());
  oled.println("");
  oled.println("WiFi conectado");
  oled.println("Direccion IP: ");
  oled.println(WiFi.localIP());
  //  CONEXION WIFI FIN

  //  CONEXION SERVIDOR MQTT INICIO
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
  client.setBufferSize(1024);

  while (!client.connected()) {
    reconnect();
  }
  Serial.println("paso1");
  //  CONEXION SERVIDOR MQTT FIN

  //  CONEXION SERVIDOR NTP INICIO + CONFIGURACION RTC
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("paso2");
  imprimirHora();
  Serial.println("paso3");
  //  CONEXION SERVIDOR NTP FIN

  //  INICIALIZACION MODULO IR RX TX INICIO
  irrecv.enableIRIn();  // Start up the IR receiver.
  irsend.begin();       // Start up the IR sender.
  Serial.print("Repetidor Infrarrojo en marcha, esperando senal IR en el pin: ");
  Serial.println(kRecvPin);
  Serial.print("y la retransmitira por el pin: ");
  Serial.println(kIrLedPin);
  //  INICIALIZACION MODULO IR RX TX FIN

  

  //  INICIALIZACION USO MEMORIA EEPROM INICIO
  // Initialize the EEPROM with the specified size
  EEPROM.begin(EEPROM_SIZE);
  // Read the stored string from the EEPROM
  String storedString = "";
  for (int i = 0; i < EEPROM_SIZE; i++) {
    char c = EEPROM.read(i);
    if (c == '\0') {
      break;    // Reached the end of the stored string
    }
    storedString += c;
  }
  
  // Print the stored string
  int lengthEeprom1 = getStoredStringLength();
  Serial.println("Stored string: " + storedString);
  oled.println("Stored string: " + storedString);
  Serial.print("Stored lentgh: ");
  Serial.println(lengthEeprom1);
  oled.print("Stored lentgh: ");
  oled.println(lengthEeprom1);

  //  INICIALIZACION USO MEMORIA EEPROM FIN

  //  *********/*/*/*/*/******************/*/*/*/*/**************
  //  SECUENCIAS DE INICIO DE PROCEDIMIENTOS DEL SISTEMA
  //  *********/*/*/*/*/******************/*/*/*/*/**************
  //  encender mostrar reloj
  reloj.attach(periodoReloj,imprimirHora);
  
  
  
  //  solicitar identificacion
  //  verificar si id ya esta en eeprom

  int lengthEeprom = getStoredStringLength();
  StaticJsonDocument<256> doc;
  char msg[256];
  if (lengthEeprom > 14)    // dispositivo-xx 14 caracteres
  //  si si, siguiente paso
  {
    recv_nombre = true;
    client.subscribe("/listas/dispositivos/out");
    //  si no, solicitar en base de datos
    while (recv_nombre){
      doc["tieneNombre"] = "no";
      serializeJson(doc, msg);
      client.publish("/listas/dispositivos/in", msg);
    }
    client.unsubscribe("/listas/dispositivos/out");
    doc.clear();
    doc["dir_IP"] = "no";
    serializeJson(doc, msg);
    client.publish("/listas/dispositivos/in", msg);
  }

  //  SELECCIONAR AMBIENTE

  client.subscribe("/listas/ambientes/out");
  doc["tieneNombre"] = "no";
  serializeJson(doc, msg);
  client.publish("/listas/dispositivos/in", msg);
  client.unsubscribe("/listas/ambientes/out");
  doc.clear();



}

void loop() {

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  unsigned long now = millis();
  if (now - lastMsg > 2000) {
    lastMsg = now;
    /*++value;
    snprintf (msg, MSG_BUFFER_SIZE, "hello world #%ld", value);
    Serial.print("Publish message: ");
    Serial.println(msg);
    client.publish("outTopic", msg);
    */

    StaticJsonDocument<200> doc;
    StaticJsonDocument<200> doc2;
    char output[300];
    char output2[300];
    ++value;

    if (estado) {
      estado = false;
      doc["estado"] = "activo";
    } else {
      estado = true;
      doc["estado"] = "inactivo";
    };


    doc["ir_code"] = value;
    doc2["1"] = 1;
    serializeJson(doc, output);
    serializeJson(doc2, output2);
    Serial.println(output);
    client.publish("/actividad/usuario", output);

    client.publish("/listas/ambientes/in", output2);
    Serial.println("Sent");
    //client.subscribe();
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  // Parse the received JSON payload
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload, length);
  // NOMBRAR DISPOSITIVO
  if (topic == "/listas/dispositivos/out") {
    recv_nombre = false;
    // escribir en EEPROM
    for (int i = 0; i < length; i++) {
      EEPROM.write(i, payload[i]);
    }
    EEPROM.write(length, '\0');   // Mark the end of the string
    EEPROM.commit();   // Save the changes to the EEPROM
    printPayloadFromEEPROM(length);
    return;
  }

  //  SELECCIONAR AMBIENTE
  if (topic == "/listas/ambientes/out") {

    // seleccionar ambiente
    
    // escribir en EEPROM
    for (int i = 0; i < length; i++) {
      EEPROM.write(i, payload[i]);
    }
    EEPROM.write(length, '\0');   // Mark the end of the string
    EEPROM.commit();   // Save the changes to the EEPROM
    printPayloadFromEEPROM(length);
    return;
  }
  // Check for parsing errors
  if (error) {
    Serial.print("Error parsing JSON: ");
    Serial.println(error.c_str());
    return;
  }

  int numObjetos = doc.size();
  int id = doc[1]["id"];
  const char* hogar = doc[1]["hogar"];
  const char* nombre_amb = doc[1]["nombre_amb"];
  Serial.print("Numero de objetos = ");
  Serial.println(numObjetos);
  Serial.print("id = ");
  Serial.println(id);
  Serial.print("hogar = ");
  Serial.println(hogar);
  Serial.print("nombre_amb = ");
  Serial.println(nombre_amb);
  /*
  // Extract values from the JSON object
  const char* name = doc["name"];
  int age = doc["age"];
  
  // Print the received values
  Serial.print("Name: ");
  Serial.println(name);
  Serial.print("Age: ");
  Serial.println(age);
  */
}




void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Realizando conexion MQTT...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
       Serial.println("conectado");
      // resuscribirse
      client.subscribe("inTopic");
      client.subscribe("/listas/ambientes/out");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void imprimirHora(){

  unsigned long startMicros = micros();

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return;
  }
  //oled.clear();
  
  oled.setCol(20);
  /*
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  Serial.print("Day of week: ");
  Serial.println(&timeinfo, "%A");
  oled.print("Day of week: ");
  oled.println(&timeinfo, "%A");
  
  Serial.print("Month: ");
  Serial.println(&timeinfo, "%B");
  Serial.print("Day of Month: ");
  Serial.println(&timeinfo, "%d");
  Serial.print("Year: ");
  Serial.println(&timeinfo, "%Y");
  */
  Serial.print("Hour: ");
  Serial.println(&timeinfo, "%H");
  //Serial.print("Hour (12 hour format): ");
  //Serial.println(&timeinfo, "%I");
  Serial.print("Minute: ");
  Serial.println(&timeinfo, "%M");
  Serial.print("Second: ");
  Serial.println(&timeinfo, "%S");
  oled.print(&timeinfo, "%H");
  oled.print(":");
  oled.print(&timeinfo, "%M");
  oled.print(":");
  oled.print(&timeinfo, "%S");
  
  
  // Lógica de la función
  
  unsigned long elapsedMicros = micros() - startMicros;

  oled.print(elapsedMicros);

/*
  Serial.println("Time variables");
  char timeHour[3];
  strftime(timeHour,3, "%H", &timeinfo);
  Serial.println(timeHour);
  char timeWeekDay[10];
  strftime(timeWeekDay,10, "%A", &timeinfo);
  Serial.println(timeWeekDay);
  Serial.println();

  */
}

int getStoredStringLength() {
  int length = 0;
  for (int i = 0; i < EEPROM_SIZE; i++) {
    char c = EEPROM.read(i);
    if (c == '\0') {
      break;    // Reached the end of the stored string
    }
    length++;
  }
  return length;
}

void printPayloadFromEEPROM(unsigned int length) {
  char payload[length + 1]; // Crear un arreglo de caracteres para almacenar el payload

  for (int i = 0; i < length; i++) {
    payload[i] = char(EEPROM.read(i)); // Leer byte a byte desde la EEPROM y convertirlo a caracter
  }
  payload[length] = '\0'; // Agregar el caracter nulo al final de la cadena para indicar su terminación
  Serial.print("dato almacenado en eeprom: "); 
  Serial.println(payload); // Imprimir la cadena almacenada en la EEPROM
  oled.print("dato almacenado en eeprom: "); 
  oled.println(payload);
}

/*
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  // Switch on the LED if an 1 was received as first character
  if ((char)payload[0] == '1') {
    digitalWrite(2, !digitalRead(2));  // Turn the LED on (Note that LOW is the voltage level
    // but actually the LED is on; this is because
    // it is active low on the ESP-01)
  }
  if (topic=="listas/ambientes/out"){
    StaticJsonDocument<200> jsonBuffer;
    // char json[] = "{\"sensor\":\"gps\",\"time\":1351824120,\"data\":[48.756080,2.302038]}"; 
    char* json[1024] = (char)payload;
    JsonObject& doc = jsonBuffer.parseObject(json);

    if(!root.success()) {
      Serial.println("parseObject() failed");
      return false;
    }
    else {

      //"id":13,"hogar":"casa1_perez_blanco","nombre_amb":"cuarto de ella"
      int id = root["id"];
      const char* hogar = root["hogar"];
      const char* nombre_amb= root["nombre_amb"];
      Serial.println("id = ", id);
      Serial.println("hogar = ", hogar);
      Serial.println("nombre_amb = ",nombre_amb);
    }
    
  }
  
} */

