/*
 Name:		_03SCH_Camara.ino
 Created:	10/13/2020 11:33:48 AM
 Author:	mrodriguez

*/


#include <WiFi.h>
#include <SoftwareSerial.h>
#include "FS.h"
#include "SD.h"
#include <SPI.h>
#include <ArduinoJson.h>
#include <DateTime.h>
#include <PubSubClient.h>
#include <HTTPClient.h>
#include <DHT.h>
//#include <LiquidCrystal_I2C.h>
#include <Adafruit_LEDBackpack.h>


#define SD_CS		5						// Define CS pin para modulo SD
#define PUERTA1		25
#define PUERTA2		33
#define RX			17
#define TX			16
#define BUZZER		12
#define ESTROBO		32
#define DHTPIN1		14
#define DHTPIN2		27
#define DHTPIN3		26
#define DHTTYPE		DHT22
//#define lcdCOLUMNS  16
//#define lcdROWS		2

//int lcdColumns = 16;
//int lcdRows = 2;

File schFile;
boolean okSD = 0, okNET = 0;
boolean error = 0;
String servidorAPI;
String servidorMQTT;
String servidorMQTTGlobal;
WiFiClient espClient;
WiFiClient espClientGlobal;
PubSubClient client(espClient);
PubSubClient clientGlobal(espClientGlobal);
SoftwareSerial swserial_O(RX, TX);
DHT dht1(DHTPIN1, DHTTYPE);
DHT dht2(DHTPIN2, DHTTYPE);
DHT dht3(DHTPIN3, DHTTYPE);
Adafruit_7segment lcd = Adafruit_7segment();
//LiquidCrystal_I2C lcd(0x27, lcdColumns, lcdRows);

byte tipo = 1;
bool debug = 0;
bool p1Abierta = 0, p2Abierta = 0;
int ledRojo = 4;
int ledVerde = 2;
int ledAzul = 15;
int	pue1 = 0, pue2 = 0;  //Puertas
int h_avg, h1, h2, h3, h_max = 100, h_min = 85;                //Humedad
float t_avg, t1, t2, t3, t_max = 2, t_min = -2;                //Temperatura
unsigned long millis_previos_p1 = 0, millis_previos_p2 = 0, millis_previos_p3 = 0, millis_previos_p4 = 0;
unsigned long millis_previos_precios = 0, millis_previos_activo = 0;
int inervalo_precios = 3600000, inervalo_activo = 300000;      // Intervalos de tiempo para Millis

String schAPI;
String carniceria;
String iddispositivo;
String tiempo = "";
String TopAvgTemp, topTemp1, topTemp2, topTemp3;
String TopAvgHum, topHum1, topHum2, topHum3;
String topPue1, topPue2;


void setup() {
	pinMode(ledRojo, OUTPUT);
	pinMode(ledVerde, OUTPUT);
	pinMode(ledAzul, OUTPUT);
	pinMode(ESTROBO, OUTPUT);
	pinMode(BUZZER, OUTPUT);
	pinMode(PUERTA1, INPUT);
	pinMode(PUERTA2, INPUT);
	lcd.begin(0x70);
	lcd.setBrightness(8);
	lcd.clear();
	ledFalla();
	ledComunicacion();
	dht1.begin();
	dht2.begin();
	dht3.begin();
	swserial_O.begin(9600);
	Serial.begin(115200);
	debug = debugActivar();
	debug ? Serial.println("Debug activado!") : false;
	intro();
	iniciarMCU() == true ? Serial.println("MCU Listo!") : Serial.println("MCU Falla!");
	ledOK();
	leerTemperatura();
}

void loop() {
	revisarPuertas();

	// Tareas cada minuto //
	unsigned long millies_atcuales_activo = millis();
	if (millies_atcuales_activo - millis_previos_activo > inervalo_activo) {
		millis_previos_activo = millies_atcuales_activo;
		debug ? Serial.println("Ha pasado un minuto!") : false;
		leerTemperatura();

		if (!enviar_a_API(dato_a_JSON())) {
			debug ? Serial.println("Falla envio a API...") : false;
			ledFalla();
			SD_escribirLog(dato_a_JSON());
			debug ? Serial.println("Datos almacenados: " + dato_a_JSON()) : false;
			if (debug)
				delay(5000);
		}
		else {
			debug ? Serial.println("Envio a API Ok!!!") : false;
			ledOK();
			debug ? Serial.println("Verificando existencia de registros sin enviar...") : false;
			SD_leerLog();
		}
	}

	// Tareas cada hora //
	unsigned long millies_atcuales_precios = millis();
	if (millies_atcuales_precios - millis_previos_precios > inervalo_precios) {
		millis_previos_precios = millies_atcuales_precios;
	}

}



boolean iniciarMCU() {
	/*
	Funcion responsable de realizar la configuración de red del MCU
	Toma como parametros las variables de micro SD para autoconfigurar SSID, Contrasena, MAC, Direcionamiento IP y el NTP
	*/
	byte  ipA, ipB, ipC, ipD;
	byte  gwA, gwB, gwC, gwD;
	byte  msA, msB, msC, msD;
	byte  dns1A, dns1B, dns1C, dns1D;
	byte  dns2A, dns2B, dns2C, dns2D;
	String red, contrred, apiser, cronos;
	String serNTP;

	// Iniciando SD
	Serial.println("\n\n\nCONFIGURANDO MCU...\nIniciando SD...");
	okSD = 1;
	SD.begin(SD_CS);
	if (!SD.begin(SD_CS)) {
		Serial.println("Error modulo SD!");
		okSD = 0;
	}
	uint8_t cardType = SD.cardType();
	if (cardType == CARD_NONE) {
		Serial.println("Error tarjeta SD!");
		okSD = 0;
	}
	if (!SD.begin(SD_CS)) {
		Serial.println("ERROR - Falla en tarjeta SD!");
		okSD = 0;
	}
	schFile = SD.open("/schconf.json", FILE_READ);
	if (schFile) {
		String confLine;
		while (schFile.available()) {
			confLine += schFile.readString();
		}
		schFile.close();
		char JSONMessage[confLine.length() + 1];
		confLine.toCharArray(JSONMessage, confLine.length() + 1);
		debug ? Serial.println(JSONMessage) : false;

		DynamicJsonDocument doc(1024);
		DeserializationError error = deserializeJson(doc, JSONMessage);
		if (error) {
			Serial.print("Error en configuraciones!");
			Serial.println(error.c_str());
			okSD = 0;
		}

		String carni = doc["carniceria"];
		byte dev_tipo = doc["tipodispositivo"];
		String device = doc["iddispositivo"];
		ipA = int(doc["ipA"]); ipB = int(doc["ipB"]); ipC = int(doc["ipC"]); ipD = int(doc["ipD"]);
		gwA = int(doc["gwA"]); gwB = int(doc["gwB"]); gwC = int(doc["gwC"]); gwD = int(doc["gwD"]);
		msA = int(doc["msA"]); msB = int(doc["msB"]); msC = int(doc["msC"]); msD = int(doc["msD"]);
		dns1A = int(doc["dns1A"]); dns1B = int(doc["dns1B"]); dns1C = int(doc["dns1C"]); dns1D = int(doc["dns1D"]);
		dns2A = int(doc["dns2A"]); dns2B = int(doc["dns2B"]); dns2C = int(doc["dns2C"]); dns2D = int(doc["dns2D"]);
		String textwifi = doc["wifi"];
		String textpasswifi = doc["passwifi"];
		String textapi = doc["API"];
		String textntp = doc["NTP"];
		red = textwifi;
		contrred = textpasswifi;
		apiser = textapi;
		cronos = textntp;
		String mosquitto = doc["MQTT"];
		String mosquittoGlobal = doc["MQTTGlobal"];
		String tempAVG = doc["avgTemp"];
		String temp1 = doc["topTem1"];
		String temp2 = doc["topTem2"];
		String temp3 = doc["topTem3"];
		String humeAVG = doc["avgHum"];
		String hume1 = doc["topHum1"];
		String hume2 = doc["topHum2"];
		String hume3 = doc["topHum3"];
		String puerta1 = doc["topPue1"];
		String puerta2 = doc["topPue2"];
		carniceria = carni;
		tipo = dev_tipo;
		iddispositivo = device;
		servidorMQTT = mosquitto;
		servidorMQTTGlobal = mosquitto;
		TopAvgTemp = tempAVG, topTemp1 = temp1;  topTemp2 = temp2; topTemp3 = temp3;
		TopAvgHum = humeAVG, topHum1 = hume1; topHum2 = hume2; topHum3 = hume3;
		topPue1 = puerta1; topPue2 = puerta2;
		t_max = float((doc["t_max"]));
		t_min = float((doc["t_min"]));
		h_max = int((doc["h_max"]));
		h_min = int((doc["h_min"]));
		okSD = 1;
		ledOK();
	}
	else {
		Serial.println("Error al abrir configuración de parametros!");
		ledFalla();
		return false;
	}


	if (okSD == 1) {
		okNET = 0;
		Serial.println("SD OK!");
		// Configuraciones de direccionamiento IP
		IPAddress local_IP(ipA, ipB, ipC, ipD);
		IPAddress gateway(gwA, gwB, gwC, gwD);
		IPAddress subnet(msA, msB, msC, msD);
		IPAddress DNS1(dns1A, dns1B, dns1C, dns1D);
		IPAddress DNS2(dns2A, dns2B, dns2C, dns2D);
		//uint8_t   mac[6]{ mac1, mac2, mac3, mac4, mac5, mac6 };
		char ssid[red.length() + 1];
		red.toCharArray(ssid, red.length() + 1);
		char password[contrred.length() + 1];
		contrred.toCharArray(password, contrred.length() + 1);
		char servidorNTP[cronos.length() + 1];
		cronos.toCharArray(servidorNTP, cronos.length() + 1);
		servidorAPI = apiser;

		WiFi.disconnect();
		delay(500);
		Serial.println("MAC: " + WiFi.macAddress());
		debug ? Serial.println("Iniciando conexion.") : false;
		WiFi.begin(ssid, password);
		debug ? Serial.println("Asignando IP.") : false;
		if (!WiFi.config(local_IP, gateway, subnet, DNS1, DNS2)) {
			Serial.println("Error al configurar IP...");
			okNET = 0;
		}
		delay(500);
		int i = 0;
		Serial.print("Conectando a WiFi ...");
		while (WiFi.status() != WL_CONNECTED) {
			WiFi.disconnect();
			Serial.print(".");
			WiFi.begin(ssid, password);
			delay(500);
			if (!WiFi.config(local_IP, gateway, subnet, DNS1, DNS2))
				Serial.println("Error al configurar IP...");
			i == 20 ? ESP.restart() : delay(1000);
			i++;
		}
		Serial.print("WiFi OK: ");
		okNET = 1;
		Serial.println(WiFi.localIP());
		Serial.println("Buscando NTP...");
		DateTime.setTimeZone(-6);
		DateTime.setServer(servidorNTP);
		DateTime.begin();
		delay(500);
		int intento = 0;
		while (!DateTime.isTimeValid() && intento < 9) {
			Serial.println("NTP Error!");
			delay(500);
			intento++;
		}
		//Ajuste de horario de verano por mes
		String mes = "";
		String fecha = DateTime.toString();
		mes = mes + fecha[5]; mes = mes + fecha[6];
		((mes.toInt()) > 3 && (mes.toInt()) < 11) ? DateTime.setTimeZone(-5) : DateTime.setTimeZone(-6);
		DateTime.forceUpdate();
		Serial.print("Tiempo: ");
		Serial.println(DateTime.toString());

		char mqtt[servidorMQTT.length() + 1];
		servidorMQTT.toCharArray(mqtt, servidorMQTT.length() + 1);
		debug ? Serial.println(mqtt) : false;
		client.setServer(mqtt, 1883);

		client.connect("SUCAHERSA");
		client.setKeepAlive(180);
		Serial.print("Estado de MQTT de arranque: ");
		Serial.println(client.state());


		char mqttGlobal[servidorMQTTGlobal.length() + 1];
		servidorMQTTGlobal.toCharArray(mqttGlobal, servidorMQTTGlobal.length() + 1);
		debug ? Serial.println(mqttGlobal) : false;
		clientGlobal.setServer(mqttGlobal, 1883);

		clientGlobal.connect("SUCAHERSA_Global");
		clientGlobal.setKeepAlive(180);
		Serial.print("Estado de MQTT Global de arranque: ");
		Serial.println(clientGlobal.state());

		delay(5000);
	}
	return okNET;
}

String dato_a_JSON() {
	/*
	Funcion responsable de formatear los datos generados, modificar dependiendo de dispositivo
	Toma como parametros los datos generales de indentificación de la carnicería
	Y recibe como parametros los Buffers de tiempo, temperatura y humedad
	No utilizar Buffers de mas de 30 elementos
	https://httpbin.org/anything
	*/
	String requestBody;
	delay(500);

	StaticJsonDocument<4096> paquete;
	//DynamicJsonDocument paquete(4096);
	//StaticJsonDocument<1024> sensor;
	//JsonArray cuerpoDatos = paquete.createNestedArray("cuerpoDatos");

	paquete["carniceria"] = carniceria;
	paquete["tipodispositivo"] = tipo;
	paquete["iddispositivo"] = iddispositivo;
	paquete["fecha"] = DateTime.toString();

	paquete["h_avg"] = h_avg;
	paquete["h1"] = h1;
	paquete["h2"] = h2;
	paquete["h3"] = h3;
	paquete["t_avg"] = t_avg;
	paquete["t1"] = t1;
	paquete["t2"] = t2;
	paquete["t3"] = t3;
	paquete["pue1"] = pue1;
	paquete["pue2"] = pue2;

	serializeJson(paquete, requestBody);
	debug ? Serial.println("Cadena a enviar: ") : false;
	debug ? Serial.println(requestBody) : false;
	return requestBody;
}

int enviar_a_API(String dato) {
	/*
	Funcion responsable de enviar paquetes formateados a servidor WebAPI mediante POST
	Toma de los parametros globales la dirección del servidor WebAPI
	Recibe como parametro una cadena de texto serializada en formato JSON
	*/
	debug ? Serial.println("Entrando a función enviar_a_API") : false;
	boolean enviado = 0;
	int intentos = 9;
	String requestBody = dato;

	while (!enviado || intentos > 0)
	{
		if (WiFi.status() == WL_CONNECTED) {
			debug ? Serial.print("Servidor API: ") : false;
			debug ? Serial.println(servidorAPI) : false;

			HTTPClient http;
			http.begin(servidorAPI);
			http.addHeader("Content-Type", "application/json");
			int httpResponseCode = http.POST(requestBody);
			delay(1000);
			ledComunicacion();

			debug ? Serial.print("\nhttpResponseCode: ") : false;
			debug ? Serial.println(httpResponseCode) : false;
			if (httpResponseCode > 0) {
				String response = http.getString();
				//Serial.println(httpResponseCode);
				debug ? Serial.println(response) : false;

				http.end();
				enviado = 1;
				intentos = 0;
				ledOK();
				delay(100);
			}
			else {
				debug ? Serial.println("Error al enviar HTTP POST") : false;
				beep(1);

				if (intentos > 1) {
					ledFalla();
					debug ? Serial.println("Reintentando HTTP POST") : false;
					delay(100);
				}
				if (intentos == 1)
				{
					ledFalla();
					debug ? Serial.println("Almacenando en SD") : false;
					delay(500);
					if (httpResponseCode == -1)
					{
						ledFalla();
						http.end();
						return 0;
					}
				}

				intentos--;
				http.end();
				delay(500);
			}
		}
		else {
			debug ? Serial.println("WiFi no disponible!") : false;
			debug ? Serial.println("Error al enviar HTTP POST") : false;
			beep(1);
			delay(500);
			ledFalla();
			iniciarMCU();
			intentos--;
		}
	}

	return enviado ? 1 : 0;
}

bool SD_validar() {
	SD.begin(SD_CS);
	if (!SD.begin(SD_CS)) {
		Serial.println("Error modulo SD!");
		return false;
	}
	uint8_t cardType = SD.cardType();
	if (cardType == CARD_NONE) {
		Serial.println("Error tarjeta SD!");
		return false;
	}
	if (!SD.begin(SD_CS)) {
		Serial.println("ERROR - Falla en tarjeta SD!");
		return false;
	}
	return 1;
}

bool SD_leerLog() {
	if (SD_validar()) {
		File dataLog = SD.open("/log.txt", FILE_READ);
		if (dataLog) {
			bool enviado = 0;
			String linea;
			dataLog.position();
			while (dataLog.available()) {
				linea = dataLog.readStringUntil('\n');
				debug ? Serial.println("Enviando registro en almacenamiento local...") : false;
				enviado = enviar_a_API(linea);
				if (enviado == false) {
					return false;
				}
			}
			dataLog.close();
			if (SD_borrarLog()) {
				return 1;
			}
		}
		else
			debug ? Serial.println("Nada por enviar!") : false;
	}
	return false;
}

bool SD_escribirLog(String cadena) {
	if (SD_validar()) {
		SD.begin(SD_CS);
		File dataLog = SD.open("/log.txt", FILE_APPEND);
		if (dataLog) {
			dataLog.println(cadena);
			dataLog.close();
			debug ? Serial.println(cadena) : false;
			return 1;
		}
	}
	return false;
}

bool SD_borrarLog() {
	if (SD_validar()) {
		SD.begin(SD_CS);
		if (SD.remove("/log.txt")) {
			Serial.println("Registro borrado.");
			schFile.close();
			return 1;
		}
	}
	return false;
}

bool debugActivar() {
	if (SD_validar()) {
		File dataLog = SD.open("/debug", FILE_READ);
		if (dataLog) {
			debug ? Serial.println("El dispositivo esta en modo DEBUG.") : false;
			dataLog.close();
			ledOK();
			return true;
		}
	}
	return false;
}

void ledOK() {
	digitalWrite(ledRojo, LOW);
	digitalWrite(ledVerde, LOW);
	digitalWrite(ledAzul, LOW);
	for (int i = 1; i <= 3; i++) {
		digitalWrite(ledVerde, HIGH);
		delay(150);
		digitalWrite(ledVerde, LOW);
		delay(100);
	}
	digitalWrite(ledVerde, LOW);
}

void ledFalla() {
	digitalWrite(ledRojo, LOW);
	digitalWrite(ledVerde, LOW);
	digitalWrite(ledAzul, LOW);
	for (int i = 1; i <= 3; i++) {
		digitalWrite(ledRojo, HIGH);
		delay(150);
		digitalWrite(ledRojo, LOW);
		delay(100);
	}
	digitalWrite(ledRojo, LOW);
}

void ledComunicacion() {
	digitalWrite(ledRojo, LOW);
	digitalWrite(ledVerde, LOW);
	digitalWrite(ledAzul, LOW);
	for (int i = 1; i <= 5; i++) {
		digitalWrite(ledAzul, HIGH);
		delay(100);
		digitalWrite(ledAzul, LOW);
		delay(50);
	}
	digitalWrite(ledAzul, LOW);
}

bool leerTemperatura() {
	bool estatus = 0;
	float tempT;
	int tempH;
	tempH = dht1.readHumidity();
	tempT = dht1.readTemperature();
	if (isnan(tempH) || isnan(tempT)) {
		debug ? Serial.println("No se peuede leer temperatura/humedad sensor 01") : false;
		ledFalla();
		estatus = 0;
		delay(2000);
	}
	else {
		h1 = tempH;
		t1 = tempT;
	}

	tempH = dht2.readHumidity();
	tempT = dht2.readTemperature();
	if (isnan(tempH) || isnan(tempT)) {
		debug ? Serial.println("No se peuede leer temperatura/humedad sensor 02") : false;
		ledFalla();
		estatus = 0;
		delay(2000);
	}
	else {
		h2 = tempH;
		t2 = tempT;
	}

	tempH = dht3.readHumidity();
	tempT = dht3.readTemperature();
	if (isnan(tempH) || isnan(tempT)) {
		debug ? Serial.println("No se peuede leer temperatura/humedad sensor 03") : false;
		ledFalla();
		estatus = 0;
		delay(2000);
	}
	else {
		h3 = tempH;
		t3 = tempT;
	}
	h_avg = (h1 + h2 + h3) / 3;
	t_avg = (t1 + t2 + t3) / 3;
	debug ? Serial.println("Tiempo		Humedad") : false;
	debug ? Serial.println(String(t1) + " °C\t\t" + String(h1) + " %") : false;
	debug ? Serial.println(String(t2) + " °C\t\t" + String(h2) + " %") : false;
	debug ? Serial.println(String(t3) + " °C\t\t" + String(h3) + " %") : false;
	estatus = 1;
	estatus ? ledOK() : ledFalla();

	displayLCD(t_avg);
	if (t_avg >= t_max || t_avg <= t_min) {
		digitalWrite(ESTROBO, HIGH);
		ledFalla();
		beep(1);
		ledFalla();
		beep(1);
		debug ? Serial.println("Fuera de rangos aceptables de temperatura!!!") : false;
		ledFalla();
		beep(1);
		delay(2000);
		digitalWrite(ESTROBO, LOW);
	}
	if (h_avg >= h_max || h_avg <= h_min) {
		ledFalla();
		beep(1);
		debug ? Serial.println("Fuera de rangos aceptables de humedad!!!") : false;
		ledFalla();
		beep(1);
	}


	debug ? Serial.print("Estado de MQTT: ") : false;
	debug ? Serial.println(client.state()) : false;


	if (client.state() != 0) {
		ledFalla();
		debug ? Serial.println("Reconectando MQTT...") : false;
		reconnect();
	}

	if (client.state() == 0) {
		char t1String[8];
		dtostrf(t1, 1, 2, t1String);
		char tempe1[topTemp1.length() + 1];
		topTemp1.toCharArray(tempe1, topTemp1.length() + 1);
		client.publish(tempe1, t1String);

		char t2String[8];
		dtostrf(t2, 1, 2, t2String);
		char tempe2[topTemp2.length() + 1];
		topTemp2.toCharArray(tempe2, topTemp2.length() + 1);
		client.publish(tempe2, t2String);

		char t3String[8];
		dtostrf(t3, 1, 2, t3String);
		char tempe3[topTemp3.length() + 1];
		topTemp3.toCharArray(tempe3, topTemp3.length() + 1);
		client.publish(tempe3, t1String);

		char h1String[8];
		dtostrf(h1, 1, 2, h1String);
		char humed1[topHum1.length() + 1];
		topHum1.toCharArray(humed1, topHum1.length() + 1);
		client.publish(humed1, h1String);

		char h2String[8];
		dtostrf(h2, 1, 2, h2String);
		char humed2[topHum2.length() + 1];
		topHum2.toCharArray(humed2, topHum2.length() + 1);
		client.publish(humed2, h2String);

		char h3String[8];
		dtostrf(h3, 1, 2, h3String);
		char humed3[topHum3.length() + 1];
		topHum3.toCharArray(humed3, topHum3.length() + 1);
		client.publish(humed3, h3String);

		char HString[8];
		dtostrf(h_avg, 1, 2, HString);
		char humed[TopAvgHum.length() + 1];
		TopAvgHum.toCharArray(humed, TopAvgHum.length() + 1);
		clientGlobal.publish(humed, HString);

		char TString[8];
		dtostrf(t_avg, 1, 2, TString);
		char temper[TopAvgTemp.length() + 1];
		TopAvgTemp.toCharArray(temper, TopAvgTemp.length() + 1);
		clientGlobal.publish(temper, TString);
	}

	return estatus;
}

void beep(int nivel) {
	int tiempo = nivel * 100;
	debug ? Serial.println("En Beep " + String(nivel)) : false;
	for (int i = 0; i < (3 * nivel); i++) {
		digitalWrite(ledRojo, HIGH);
		digitalWrite(BUZZER, HIGH);
		delay(tiempo);
		digitalWrite(ledRojo, LOW);
		digitalWrite(BUZZER, LOW);
		delay(tiempo);
	}
}

bool revisarPuertas() {
	bool p1, p2;

	/*
	bool p1Abierta = 0, p2Abierta = 0, p3Abierta = 0, p4Abierta = 0;;
	unsigned long millis_previos_p1 = 0, millis_previos_p2 = 0, millis_previos_p3 = 0, millis_previos_p4 = 0;
	int inervalo_precios = 3600000, inervalo_activo = 60000;
	*/

	unsigned long deltaP1 = 0, deltaP2 = 0;
	unsigned long millies_atcuales_p1 = millis(), millies_atcuales_p2 = millis(), millies_atcuales_p3 = millis(), millies_atcuales_p4 = millis();

	p1 = digitalRead(PUERTA1);
	p2 = digitalRead(PUERTA2);

	debug ? Serial.println("Estado de puertas.") : false;
	debug ? Serial.println(p1 ? "P1 CERRADO" : "P1 ABIERTO") : false;
	debug ? Serial.println(p2 ? "P2 CERRADO" : "P2 ABIERTO") : false;

	!p1 ? deltaP1 = (millies_atcuales_p1 - millis_previos_p1) / 1000 : millis_previos_p1 = millies_atcuales_p1;
	debug ? Serial.println("Segundos P1 abierta: " + String(deltaP1)) : false;
	!p2 ? deltaP2 = (millies_atcuales_p2 - millis_previos_p2) / 1000 : millis_previos_p2 = millies_atcuales_p2;
	debug ? Serial.println("Segundos P2 abierta: " + String(deltaP2)) : false;
	p1Abierta = p1;
	p2Abierta = p2;

	deltaP1 == 0 ? deltaP1++ : false;
	deltaP2 == 0 ? deltaP2++ : false;

	pue1 = deltaP1;
	pue2 = deltaP2;

	if ((deltaP1 % 300 == 0) || (deltaP2 % 300 == 0))
		beep(5);
	else if ((deltaP1 % 240 == 0) || (deltaP2 % 240 == 0))
		beep(4);
	else if ((deltaP1 % 180 == 0) || (deltaP2 % 180 == 0))
		beep(3);
	else if ((deltaP1 % 120 == 0) || (deltaP2 % 120 == 0))
		beep(2);
	else if ((deltaP1 % 60 == 0) || (deltaP2 % 60 == 0)) {
		beep(1);
		if ((deltaP1 > 305) || (deltaP2 > 305))
			beep(5);
	}

	if (client.state() != 0) {
		reconnect();
	}
	if (client.state() == 0) {

		char p1String[8];
		dtostrf(p1Abierta, 1, 2, p1String);
		char puert1[topPue1.length() + 1];
		topPue1.toCharArray(puert1, topPue1.length() + 1);
		client.publish(puert1, p1String);

		char p2String[8];
		dtostrf(p2Abierta, 1, 2, p2String);
		char puert2[topPue2.length() + 1];
		topPue2.toCharArray(puert2, topPue2.length() + 1);
		client.publish(puert2, p2String);

	}
	return 1;
}

void reconnect() {

	char mqtt[servidorMQTT.length() + 1];
	servidorMQTT.toCharArray(mqtt, servidorMQTT.length() + 1);
	debug ? Serial.println(mqtt) : false;
	client.setServer(mqtt, 1883);

	client.connect("SUCAHERSA");
	client.setKeepAlive(180);
	debug ? Serial.print("Estado de MQTT de arranque: ") : false;
	debug ? Serial.println(client.state()) : false;


	char mqttGlobal[servidorMQTTGlobal.length() + 1];
	servidorMQTTGlobal.toCharArray(mqttGlobal, servidorMQTTGlobal.length() + 1);
	debug ? Serial.println(mqttGlobal) : false;
	clientGlobal.setServer(mqttGlobal, 1883);

	clientGlobal.connect("SUCAHERSA_Global");
	clientGlobal.setKeepAlive(180);
	debug ? Serial.print("Estado de MQTT Global de arranque: ") : false;
	debug ? Serial.println(clientGlobal.state()) : false;



	int i = 0;

	while (!client.connected()) {
		Serial.print("Intentando enlazar MQTT...");
		if (client.connect("SUCAHERSA")) {
			Serial.println("connected");
		}
		else {
			client.disconnect();
			Serial.println(client.state());
			Serial.print("Falla, rc=");
			Serial.println(client.state());
			Serial.println(" intentando en 5 seconds");
			client.disconnect();
			client.connect("SUCAHERSA");
			Serial.println(client.state());
			i++;
			if (i == 1) {
				Serial.println("Omitiendo conexión a MQTT...");
				delay(500);
				break;
			}
			delay(100);
		}
	}
}

void intro() {
	Serial.println("Iniciando...");
	delay(2000);
	Serial.println("\n");
	Serial.println("CIATEC, A.C.");
	Serial.println("DIRECCION DE INVESTIGACION Y SOLUCIONES TECNOLOGICAS");
	Serial.println("SERVICIOS TECNOLOGICOS DE APOYO A LA SALUD");
	Serial.println("SALUD 4.0");
	Serial.println("www.ciatec.mx");
	Serial.println("\nSistema de monitoreo de camaras de conservacion y congelacion.");
	debug ? Serial.println("mrodriguez@ciatec.mx") : false;
	Serial.println("\n\n\n");
	delay(2000);
}


void displayLCD(float valor) {
	if (valor > -10.0 && valor < 99.0) {
		lcd.printFloat(valor, 2, DEC);
		lcd.writeDisplay();
		debug ? Serial.println("Imprimiendo en display:" + String(valor)) : false;
	}
	else {
		int temp = valor;
		lcd.print(temp);
		lcd.writeDisplay();
		debug ? Serial.println("Imprimiendo en display:" + String(temp)) : false;
	}
}