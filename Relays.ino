#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <PubSubClient.h>

// Aqui van los parámetros que no se deben subir al repo de github (ssid, password, etc ...)
#include "secret.h"

typedef struct {
  int pin;
  int state;
  unsigned long timer;
} Relay;

// #define HW622

#ifdef HW622
#define RELAY0 D2  // Pin 4
#define RELAYS 1   // 1 Solo rele en la placa

Relay Relays[RELAYS] = {
  { RELAY0, 0, 0 }
};
#else
#define RELAY0 D7  // GPIO13
#define RELAY1 D6  // GPIO12
#define RELAY2 D5  // GPIO14
#define RELAY3 D0  // GPIO16 // Está On durante el arranque
#define RELAYS 4   // 4 Reles en la placa

Relay Relays[RELAYS] = {
  { RELAY0, 0, 0 },
  { RELAY1, 0, 0 },
  { RELAY2, 0, 0 },
  { RELAY3, 0, 0 }
};
#endif

#define MQTT_ENABLED

#ifdef MQTT_ENABLED
#define MQTT_MAX_BUFFER_SIZE 64
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
#endif

#define PORT 8888
unsigned long lastCheckMillis = 0;
char ACK[] = "ACK\r\n";

WiFiUDP Udp;

void setup() {
  delay(1000);
  Serial.begin(115200);

  Serial.println("\n\n=== Relays ===");

  Serial.println("Apagamos todos los reles");
  for (int i = 0; i < RELAYS; i++) {
    pinMode(Relays[i].pin, OUTPUT);
  }
  shutdown();

  Serial.printf("MAC: %s\n", WiFi.macAddress().c_str());

  WiFi.mode(WIFI_STA);
  WiFi.begin(STASSID, STAPSK);
  int retries = 0;
  Serial.print("Conectando ...");
  while (WiFi.status() != WL_CONNECTED && retries < 100) {
    Serial.print(".");
    retries++;
    delay(100);
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("Red: %s\n", STASSID);
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Serial.printf("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
    Serial.printf("DNS: %s\n", WiFi.dnsIP().toString().c_str());
    int rssi = WiFi.RSSI();
    Serial.printf("RSSI: %d\n", rssi);

    Udp.begin(PORT);
    Serial.printf("Puerto: UDP/%d\n", PORT);

#ifdef MQTT_ENABLED
    Serial.println("Conectando con el servidor MQTT ...");
    mqttClient.setServer(MQTT_SERVER, MQTT_PORT);
    mqttClient.setCallback(mqttReceive);
    String mqttClientId = "Relay-";
    mqttClientId += String(random(0xffff), HEX);
    if (mqttClient.connect(mqttClientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
      Serial.printf("MQTT Server: %s\n", MQTT_SERVER);
      if (mqttClient.subscribe(MQTT_TOPIC)) {
        Serial.printf("MQTT Topic: %s\n", MQTT_TOPIC);
      }
    }
#endif

  } else {
    String macAddress = WiFi.softAPmacAddress();
    String lastTwoBytes = macAddress.substring(macAddress.length() - 5);
    lastTwoBytes.replace(":", "");
    String ssid = "Relays_" + lastTwoBytes;
    String password = AP_PASSWORD;

    IPAddress local(192, 168, 33, 1);
    IPAddress gateway(192, 168, 33, 1);
    IPAddress subnet(255, 255, 255, 0);

    WiFi.softAPConfig(local, gateway, subnet);
    if (WiFi.softAP(ssid, password)) {
      Serial.printf("AP SSID: %s\n", ssid.c_str());
      Serial.printf("IP: %s\n", WiFi.softAPIP().toString().c_str());
      Udp.begin(PORT);
      Serial.printf("Puerto: UDP/%d\n", PORT);
    };
  }
}

void loop() {

  relayLoop();

  static char buffer[UDP_TX_PACKET_MAX_SIZE + 1];

  // Comprobamos si la WiFi sigue conectada y funcionando
  if (WiFi.getMode() == WIFI_STA) {
    checkWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

#ifdef MQTT_ENABLED
  mqttClient.loop();
#endif

  // Recibimos un paquete UDP
  int size = Udp.parsePacket();
  if (!size) return;

  // Si acabamos de recibir un paquete UDP damos por supuesto que la WiFi funciona bien
  lastCheckMillis = millis();
  buffer[Udp.read(buffer, UDP_TX_PACKET_MAX_SIZE)] = 0;
  parseBuffer(buffer, Udp.remoteIP().toString().c_str());
  sendACK();
}

void parseBuffer(char buffer[], const char src[]) {
  // Leemos los comandos linea a linea
  char *lasts;
  char *line = strtok_r(buffer, "\n\r", &lasts);
  while (line) {

    Serial.printf("Rcv (%s): %s\n", src, line);

    char *command = strtok(line, " ");
    if (!command) {
      line = strtok_r(NULL, "\n\r", &lasts);
      continue;
    }

    if (strcmp(command, "shutdown") == 0) shutdown();
    else {
      // El resto de comandos tienen parametros
      char *params = strtok(NULL, "\n\r");
      if (!params) {
        line = strtok_r(NULL, "\n\r", &lasts);
        continue;
      }

      // Texto
      if (strcmp(command, "relayOn") == 0) relayOn(params);
      else if (strcmp(command, "relayOff") == 0) relayOff(params);
      else if (strcmp(command, "pulse") == 0) pulse(params);
      else if (strcmp(command, "toggle") == 0) toggle(params);
    }

    line = strtok_r(NULL, "\n\r", &lasts);
  }
}

void sendACK() {
  Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
  Udp.write(ACK);
  Udp.endPacket();
}

void relayLoop() {
  unsigned long now = millis();
  for (int i = 0; i < RELAYS; i++) {
    if (Relays[i].timer > 0 && now >= Relays[i].timer) {
      setRelayState(i, LOW);
    }
  }
}

void shutdown() {
  for (int i = 0; i < RELAYS; i++) {
    setRelayState(i, LOW);
  }
}

void relayOn(const char *params) {
  int relay;
  if (sscanf(params, "%d", &relay) == 1 && relay >= 0 && relay < RELAYS) {
    setRelayState(relay, HIGH);
  }
}

void relayOff(const char *params) {
  int relay;
  if (sscanf(params, "%d", &relay) == 1 && relay >= 0 && relay < RELAYS) {
    setRelayState(relay, LOW);
  }
}

void toggle(const char *params) {
  int relay;
  if (sscanf(params, "%d", &relay) == 1 && relay >= 0 && relay < RELAYS) {
    if (Relays[relay].state) {
      setRelayState(relay, LOW);
    } else {
      setRelayState(relay, HIGH);
    }
  }
}

void setRelayState(int relay, int state) {
  if (relay >= 0 && relay < RELAYS) {
    Relays[relay].timer = 0;
    Relays[relay].state = state;
    digitalWrite(Relays[relay].pin, state);
  }
}

void pulse(const char *params) {
  int relay;
  unsigned long duration;

  if (sscanf(params, "%d,%lu", &relay, &duration) == 2 && relay >= 0 && relay < RELAYS) {
    unsigned long targetTime = millis() + duration;
    if (targetTime == 0) {
      targetTime = 1;
    }
    Relays[relay].timer = targetTime;
    Relays[relay].state = HIGH;
    digitalWrite(Relays[relay].pin, HIGH);
  }
}

WiFiClient client;

void checkWiFi() {

  unsigned long now = millis();
  if (now - lastCheckMillis < 5000) return;
  lastCheckMillis = now;

  if (WiFi.status() == WL_CONNECTED) {
    if (client.connect(WiFi.gatewayIP(), 80)) {
      client.stop();
#ifdef MQTT_ENABLED
      if (!mqttClient.connected()) {
        mqttReconnect();
      }
#endif
      return;
    }
    client.stop();
  }

  Serial.println("No hay conexion WiFi!");
  Udp.stopAll();
  WiFi.disconnect();

  Serial.print("Conectando ...");
  WiFi.begin(STASSID, STAPSK);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nConexion WiFi restablecida.");
    Serial.printf("IP: %s\n", WiFi.localIP().toString().c_str());
    Udp.begin(PORT);
    Serial.printf("Puerto: UDP/%d\n", PORT);
  } else {
    Serial.println("\nNo se pudo restablecer la conexion!");
  }

  lastCheckMillis = now;
}

#ifdef MQTT_ENABLED
void mqttReceive(char *topic, byte *payload, unsigned int length) {
  static char buffer[MQTT_MAX_BUFFER_SIZE + 1];

  unsigned int i;
  for (i = 0; i < MQTT_MAX_BUFFER_SIZE && i < length && payload[i]; i++)
    buffer[i] = (char)payload[i];
  buffer[i] = '\0';

  parseBuffer(buffer, topic);
}

void mqttReconnect() {
  if (!mqttClient.connected()) {
    Serial.println("Volviendo a conectar con el servidor MQTT ...");
    String mqttClientId = "Teletype-";
    mqttClientId += String(random(0xffff), HEX);
    if (mqttClient.connect(mqttClientId.c_str(), MQTT_USER, MQTT_PASSWORD)) {
      Serial.printf("MQTT Server: %s\n", MQTT_SERVER);
      if (mqttClient.subscribe(MQTT_TOPIC)) {
        Serial.printf("MQTT Topic: %s\n", MQTT_TOPIC);
      }
    } else {
      Serial.println("Error al conectar con el servidor MQTT");
    }
  }
}
#endif
