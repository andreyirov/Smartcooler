#include <ESP8266WiFi.h>
#include <MQTTClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>
#include <EEPROM.h>
#include "HX711.h" //

// -For BlueMix connect----
#define ORG "kwxqcy" // имя организации
#define DEVICE_TYPE "SmartCooler" // тип устройства
#define TOKEN "12345678" // - задаешь в IOT хабе

// EEPROM - разделение области памяти
#define ModeoffSet 0 // начальный байт: 0 байт - режим загрузки устройства
#define SizeMode 1 // - размер памяти Mode байт -1
#define SSIDoffSet 1 // начальный байт: SSID
#define SizeSSID 32 // размер памяти SSID байт -32
#define PasswordoffSet 33 //  начальный байт Password
#define SizePassword 32 //  размер памяти   Password
#define DeviceIDoffset 66 // начальный байт Device ID
#define SizeDeviceID 32 // размер памяти Device ID байт
#define DeviceTypeoffset 99 // начальный байт Device ID
#define SizeDeviceType 32 // размер памяти Device ID байт
#define OrgIDoffset 132 // начальный байт Device ID
#define SizeOrgID 32 // размер памяти Device ID байт
#define DWoffSet 165 //  начальный байт: 65-75 байт- значение сухого весеа
#define SizeDW  10 // размер памяти DW
#define FWoffSet 176 //  начальный байт:  значение полного весеа
#define SizeFW  10 // размер памяти вес бутылки
#define BoffSet 187 //  начальный байт: вес бутылки
#define SizeB  5 // размер памяти Полный вес
#define BoffSetV 193 //  начальный байт: обьем бутылки
#define SizeBV  5 // размер памяти обьем


char mqttserver[] = ORG ".messaging.internetofthings.ibmcloud.com"; // подключаемся к Blumix
char topic[] = "iot-2/evt/status/fmt/json";
char restopic[] = "iot-2/cmd/rele/fmt/json";
char authMethod[] = "use-token-auth";
char token[] = TOKEN;
String clientID = "d:" ORG ":" DEVICE_TYPE ":";
char  cID[100]; // используется для формирования конечного Client ID
float dryWeight;
float FullWeight;
//------ Весовая часть------

int DOUT = 5;   // HX711.DOUT Данные с датчика веса
int SCK1 = 4;  // HX711.PD_SCK Тактирование датчика веса
int RELE = 14; // RELE на D05  в лекгой версии - светодид
int BUTTON = 12; // Кнопка Setup
float Vvoter = 0;
float oldVvoter = 500;
HX711 ves; // создаем переменную для работы с АЦП 711
char ssid[32];
char password[32];
char* cfg_ssid = "sc01"; // SSID передаваемое в режиме AP кулером
char* cfg_password = "87654321"; // Password для доступа в Cooler

long lastMsg = 0;
long starttime; // момент перехода в режим 0

String content;
String dwEPRPM;
String fwEPRPM;
String stip;
String stpsw;
String DeviceID;
String IDclient;
String Bottle;
String BottleV;
char   dvid[32];
char  dwRead[10];
char  fwRead[10];
char  BRead[5];
char  BReadV[5];
int statusCode;

int MODE; // Режим загрузки

ESP8266WebServer server(80);
WiFiClientSecure net; // sec
MQTTClient client;
unsigned long lastMillis = 0;

void myconnect(); // <- predefine connect() for setup()
				  //void APSetup(); // <- predefine APSetup for setup()
void EEread(char* eeprval, byte offSet, byte Size);
void EEwrite(String indata, byte offSet, byte Size);
void myreboot();


void setup() {
	Serial.begin(115200);
	while (!Serial) {
		// wait serial port initialization
	}
	pinMode(BUTTON, INPUT_PULLUP); // Устанавливем пин кнопки на чтение подтягиваем к +
	attachInterrupt(BUTTON, myreboot, HIGH); // Устанавливаем прерывание на пин
	EEPROM.begin(512);
	MODE = EEPROM.read(0); // определяем режим загрузки   читаем 0 байт EEPROM- если 0 - режим конфигурации если 1 -рабочий режим
	EEread(dwRead, DWoffSet, SizeDW); //загрузаем значение сухого веса из EEPROM
	EEread(fwRead, FWoffSet, SizeFW); //загрузаем значение полного  веса из EEPROM
	EEread(BRead, BoffSet, SizeB); //загрузаем значение  веса бутылки из EEPROM
	EEread(BReadV, BoffSetV, SizeBV);
	EEread(dvid, DeviceIDoffset, SizeDeviceID); // загружаем значение  Device ID;

	if (MODE == 1) // Нормальный режим ___________________________________________________________
	{
		Serial.println("Mode-1 - Normal-  Start");
		//Читаем SSID и Password из EEPROM:++++++++++
		EEread(ssid, SSIDoffSet, SizeSSID);
		EEread(password, PasswordoffSet, SizePassword);
		Serial.println("SSID");
		Serial.println(ssid);
		Serial.println("Password");
		Serial.println(password);
		Serial.println("ID client:");
		//Погдотавливаем Client ID и записчваем в macив, т.к. String MQTTClient не принимает
		clientID += String(dvid);
		int str_len = clientID.length() + 1;
		clientID.toCharArray(cID, 50);
		Serial.println(cID);
		delay(1);
		WiFi.begin(ssid, password);
		pinMode(RELE, OUTPUT); // настройка пина RELE на выход
		digitalWrite(RELE, LOW); // выключение реле
		ves.begin(DOUT, SCK1, 128); // инициализация АЦП
		client.begin(mqttserver, 8883, net); // 8883-sec 1883 -no sec
		myconnect();
	}

	if (MODE == 0)
	{
		Serial.println("Mode-0 -Config mode- Start");
		WiFi.printDiag(Serial);
		ves.begin(DOUT, SCK1, 128);
		//WiFi.mode(WIFI_AP); // Здесь можно насторить режим AP, выдаваемый IP Gateway Subnet ip ADRESS, нас устраивет значение по умолчанию 192.168.4.1

		IPAddress APIP(10, 0, 0, 100);
		IPAddress gateway(10, 0, 0, 1);
		IPAddress subnet(255, 255, 255, 0);
		WiFi.softAPConfig(APIP, gateway, subnet);
		WiFi.softAP(cfg_ssid, cfg_password);
		starttime = millis(); // сохраняем время перехода в режим 0, через 5 мин
		launchWebAP(0);//OK
					   //return;
	}
}

void myconnect() {
	Serial.print("checking wifi...");
	while (WiFi.status() != WL_CONNECTED) {
		Serial.print(".");
		delay(1000);
	}
	Serial.print("\nconnecting...");
	while (!client.connect(cID, authMethod, token)) {
		Serial.print("+");
		delay(1000);
	}
	Serial.println("\nconnected!");
	client.subscribe(restopic);
}

// В номрмальном режиме
void messageReceived(String restopic, String payload, char * bytes, unsigned int length) {
	Serial.println("inpuuuuut");
	if (payload == "{\"rel\":1}") {  // Включить реле
		digitalWrite(RELE, HIGH);
		Serial.println("RELE_ON");
	}
	else if (payload == "{\"rel\":0}") { // Выключить реле
		digitalWrite(RELE, LOW);
		Serial.println("RELE_OFF");
	}

	//else if (payload == "{\"rel\":8}") { // Прошивка по воздуху
	//									 //String uploadfile = String(dvid);
	//									 //uploadfile += ".bin";
	//	t_httpUpdate_return ret = ESPhttpUpdate.update("10.0.0.167", 80, "x1.bin");
	//	Serial.println("NO Update OTA");
	//}

	else {
		Serial.println("no_action");
	}
}
// В нормальном режиме
String outmessage(float V, char* DeviceID)
{
	String pl = "{ \"d\" : {\"deviceid\":\"";
	pl += DeviceID;
	pl += "\",\"curv\":\"";
	pl += V;
	pl += "\",\"maxv\":\"";
	pl += BReadV;
	pl += "\"}}";
	return pl;
}
void reconnect() {
	// Loop until we're reconnected
	while (!client.connected()) {
		Serial.print("Attempting MQTT connection...");
		// Attempt to connect
		if (client.connect(cID, authMethod, token)) {
			Serial.println("connected");
			// Once connected, publish an announcement...

			String payload = outmessage(Vvoter, dvid);
			client.publish(topic, (char*)payload.c_str());
			// ... and resubscribe
			client.subscribe(restopic);
		}
		else {
			Serial.print("failed, rc=");
			// Serial.print(client.state());
			Serial.println(" try again in 5 seconds");
			// Wait 5 seconds before retrying
			delay(5000);
		}
	}
}
// В нормальном режиме

//_____________________________________________________________

void launchWebAP(int webtype) {
	createWebServer(webtype);
	server.begin();
}
void createWebServer(int webtype)
{
	if (webtype == 0) {
		server.on("/", []() {
			EEread(ssid, SSIDoffSet, SizeSSID);
			EEread(password, PasswordoffSet, SizePassword);
			EEread(dwRead, DWoffSet, SizeDW);
			EEread(fwRead, FWoffSet, SizeFW);
			EEread(BRead, BoffSet, SizeB);
			EEread(BReadV, BoffSetV, SizeBV);
			EEread(dvid, DeviceIDoffset, SizeDeviceID);
			//_______________________________________________________
			content = "<!DOCTYPE HTML>\r\n<html>  <head> <meta http-equiv=\"Content - Type\" content=\"text / html; charset = utf-8\"> </head> <h1>Smart Cooler настройка</h1> <h2>Текущие значения:</h2>";
			content += "SSID=";
			content += ssid;
			content += "  Password:";
			content += password;
			content += "<br>  Dry Weight :";
			content += dwRead;
			content += "Full Weight :";
			content += fwRead;
			content += "Bottle Weight :";
			content += BRead;
			content += "Bottle V:";
			content += BReadV;
			content += "  Device ID :";
			content += dvid;
			//_______________________________________________________
			content += " <hr><h2> Введите новые значения SSID и  Password </h2>";
			content += "<form method='get' action='setting'>";
			content += "<label>SSID: </label><input name='ip' length=32><br><br>";
			content += "<label>PASSWORD: </label><input name='password' length=32><br><br>";
			content += "<input type='submit' value='Сохранить SSID/Password'></form>";
			//______________________________________________________
			content += "<hr><h2>Введите новый идентификатор устройства </h2> ";
			content += "<form method='get' action='deviceidsetting'>";
			content += "<label>DeviceID: </label><input name='DeviceID' length=32><br><br>";
			content += "<input type='submit' value='Сохранить Device ID'></form>";
			//______________________________________________________
			content += "<h2> Калибровка веса пустрого кулера </h2>";
			content += "<p> Установите пустой кулер ровно на подставку, нажмите кнопку Save Dry Weight. </p>";
			content += "<p><font  color=\"red\"> Не нажимайте кнопку сохранить сухой вес если в кулере есть вода.</font> </p>";
			content += "<p><font  color=\"blue\"> Если вы случайно сохранили сухой вес при наличии воды: <br> 1)снимите бутыль с кулера,<br> 2)слейте остатки воды из кулера <br> 3)нажмите кнопку сохранить сухой вес.</font> </p>";
			content += "<form method='get' action='dw'><input type='submit' value='Save Dry Weight'></form>";
			content += "<h2> Калибровка веса Полного  кулера </h2>";
			content += "<p> Установите полную бутылку в  кулер, подождите 10 секунд , нажмите кнопку Save Full Weight. </p>";
			content += "<form method='get' action='fw'><input type='submit' value='Save Full Weight'></form>";
			//______________________________________________________

			content += "<hr><h3>Изменение обьема и веса емкости </h3> ";
			content += "<form method='get' action='bot'>";
			content += "<label>Введите вес емкости : </label><input type='text' name='BotW' value='19.83' length=6><br><br>";
			content += "<input type='submit' value='Сохранить вес емкости'></form>";
			content += "<form method='get' action='botV'>";
			content += "<label>Введите объем емкости : </label><input type='text' name='BotV' value='19' length=6><br><br>";
			content += "<input type='submit' value='Сохранить объем емкости'></form>";
			//______________________________________________________
			content += "<hr><h2> Переключение в рабочий режим </h2>";
			content += "<p><font  color=\"red\"> После переключения перезагрузите устройство.</font> </p>";
			content += "<form method='get' action='changemode'><input type='submit' value='Переключиться в рабочий режим'></form>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/changemode", []() {
			EEPROM.write(0, 1); // устанавливаем Режим нормальной загрузки
			EEPROM.commit();
			content = "<!DOCTYPE HTML>\r\n<html>";
			content = "<p> \"Устройство переведено в рабочий режим, перезагрузите устройство\"</p></br>}";
			content += "<a href='/'>Вернуться к конфигурации</a>";
			content += "</html>";
			server.send(200, "text/html", content);
			ESP.restart();
		});
		server.on("/setting", []() {
			String stip = server.arg("ip");
			String stpsw = server.arg("password");
			if (stip.length() > 0) {
				EEwrite(stip, SSIDoffSet, SizeSSID);
				EEwrite(stpsw, PasswordoffSet, SizePassword);
				content = "<!DOCTYPE HTML>\r\n<html>";
				content = "<p>SSID Password сохранены </p>";
				content += "</br>";
				content += "<a href='/'>Вернуться к конфигурации</a>";
				content += "</html>";
				server.send(200, "text/html", content);
				Serial.println("Sending 200 -SSID PASWD-OK");
			}
			else {
				content = "{\"Error\":\"404 not found\"}";
				statusCode = 404;
				Serial.println("Sending 404");
			}
			server.send(statusCode, "application/json", content);

		});

		server.on("/dw", []() {
			dryWeight = ves.read_average(30);
			dwEPRPM = String(dryWeight);
			EEwrite(dwEPRPM, DWoffSet, SizeDW);
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p>Dry Wight calibrate OK <br> </p>";
			content += "<a href='/'>Вернуться к конфигурации</a>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/fw", []() {
			FullWeight = ves.read_average(30);
			fwEPRPM = String(FullWeight);
			EEwrite(fwEPRPM, FWoffSet, SizeFW);
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p>Full Wight calibrate OK <br> </p>";
			content += "<a href='/'>Вернуться к конфигурации</a>";
			content += "</html>";
			server.send(200, "text/html", content);

		});

		server.on("/deviceidsetting", []() {
			DeviceID = server.arg("DeviceID");
			EEwrite(DeviceID, DeviceIDoffset, SizeDeviceID);
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p> Deviceid Settings <br>";
			content += "</p>";
			content += "<a href='/'>Вернуться к конфигурации</a>";
			content += "</html>";
			server.send(200, "text/html", content);
		});

		server.on("/bot", []() {
			Bottle = server.arg("BotW");
			EEwrite(Bottle, BoffSet, SizeB);
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p> Вес емкости сохранен <br>";
			content += "</p>";
			content += "<a href='/'>Вернуться к конфигурации</a>";
			content += "</html>";
			server.send(200, "text/html", content);

		});

		server.on("/botV", []() {
			BottleV = server.arg("BotV");
			EEwrite(BottleV, BoffSetV, SizeBV);
			content = "<!DOCTYPE HTML>\r\n<html>";
			content += "<p> Объем емкости сохранен <br>";
			content += "</p>";
			content += "<a href='/'>Вернуться к конфигурации</a>";
			content += "</html>";
			server.send(200, "text/html", content);

		});
	}
}
void myreboot() // Перезагрузка в режим конфигурирования при нажатии кнопки
{
	EEPROM.write(0, 0); // устанавливаем Режим в 0
	EEPROM.commit();
	Serial.println("Reboot");
	detachInterrupt(BUTTON);
	//ESP.reset();
	ESP.restart();
}
void EEread(char* eeprval, byte offSet, byte Size) // Чтение зи EEPROM
{
	for (int i = 0; i <= Size; ++i)
	{
		eeprval[i] = char(EEPROM.read(i + offSet));
		if (char(EEPROM.read(i + offSet)) == '\0')
		{
			break;
		}
	}
}

void EEwrite(String indata, byte offSet, byte Size) // Запись в EEPROM
{
	if (indata.length() > 0 && Size >= indata.length())
	{
		for (int i = 0; i <= indata.length(); ++i)
		{
			EEPROM.write(i + offSet, indata[i]);
		}
		EEPROM.commit();
	}
}

//_____________________________________________________________

void loop() {
	if (MODE == 1)
	{
	if ((atof(fwRead) - atof(dwRead)) != 0)
		{
			Vvoter = (ves.read_average(10) - atof(dwRead))*atof(BRead) / (atof(fwRead) - atof(dwRead));
			long now = millis();
			if (Vvoter < oldVvoter - 0.2 || Vvoter > oldVvoter + 19 || now - lastMsg > 300000) // передаем сообщение при изменении массы или по  таймауту 5  сек (еще 5 сек ниже).
			{
				delay(5000); // ждем пока  закочатся колебания вызваные изменением веса 
				Vvoter = (ves.read_average(10) - atof(dwRead))*atof(BRead) / (atof(fwRead) - atof(dwRead));

				if (!client.connected()) {
					reconnect();
				}
				lastMsg = now;
				oldVvoter = Vvoter;
				String payload =outmessage(Vvoter, dvid);
				Serial.print("Publish message: ");
				Serial.println(payload);
				client.publish(topic, (char*)payload.c_str());
				Serial.println("Ver-A: ");
			}
		}
		else
		{
			Serial.print("Error! FullWeight = DryWeight");
		}
			client.loop();
			delay(10);
		}
	if (MODE != 1)
	{
		server.handleClient();
		long now = millis();
		if (now - starttime > 600000)  // Через 10 мин нахождения в конфигурационном режиме, перезагружаемся в основной режим
		{
			EEPROM.write(0, 1);
			EEPROM.commit();
			Serial.println("Reboot");
			ESP.restart();
		}
	}
}




