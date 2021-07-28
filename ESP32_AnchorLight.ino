/****************************************************************************************************************************
 * Copyright notice:
 *
 * The MIT License (MIT)
 * Copyright (c) 2021 Mario Pirich
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Author: 		Mario Pirich
 * Version: 	1.0
 * Date: 		25.07.2021
 * Synopsis:	Sketch turns on and off the anchor light (respectively relay module) triggered by a Telegram message.
 * 				Device sets up WiFi network for configuration.
 * 				The sketch has an implemented OTA routine. Go to config page to configure OTA endpoint.
 *
 * 				Connect to ESP32 AcessPoint and open: esp32.local or 192.168.4.1
 *
 * 				Fill out credentials for Access Point and complete config form, as applicable.
 *
 * Wiring:		PIN G23 connected to control relay.
 *
 * Licenses and credits:
 *
 * 				Arduino.h - Main include file for the Arduino SDK (c) 2005-2013 Arduino Team GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				Wifi - esp32 Wifi support Copyright (c) 2011-2014 Arduino, Hristo Gochkov, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				esp_task_wdt (c) 2015-2016 Espressif Systems (Shanghai) PTE LTD Licensed under the Apache License, Version 2.0 (the "License");
 * 				ESPAsyncWebServer - Asynchronous WebServer library for Espressif MCUs (c) 2011-2014 Arduino, Hristo Gochkov, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				ESP32httpUpdate - ESP32 Http Updater (c) 2017 Matej Sychra GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				AsyncTCP - Asynchronous TCP library  (c) 2011-2014 Arduino, Hristo Gochkov, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				WiFiClientSecure - WiFiClientSecure.h - Base class that provides Client SSL to ESP32 (c) 2011 Adrian McEwen, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				WiFiClient - Client.h - Base class that provides Client (c) 2011 Adrian McEwen, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				HTTPClient - This file is part of the HTTPClient for Arduino. (c) 2015 Markus Sattler, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				UniversalTelegramBot Copyright (c) 2016-2020 Brian Lough brian.d.lough@gmail.com, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				ArduinoJson - (c) Benoit Blanchon 2014-2021, MIT License
 * 				ESPmDNS - ESP8266 Multicast DNS (port of CC3000 Multicast DNS library) (c) 2013 Tony DiCola (tony@tonydicola.com), MIT license)
 * 				SPIFFS - (c) 2015-2016 Espressif Systems (Shanghai) PTE LTD, Apache License, Version 2.0
 * 				FS - File system wrapper (c) 2015 Ivan Grokhotkov, GNU Lesser General Public License as published by the Free Software Foundation v2.1
 * 				Skeleton V2.0.4 * Copyright 2014, Dave Gamache  * www.getskeleton.com
 *
 ****************************************************************************************************************************/

#include "Arduino.h"
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <ESPAsyncWebServer.h>
#include <ESP32httpUpdate.h>
#include <AsyncTCP.h>
#include <WiFiClientSecure.h>
#include <WiFiClient.h>

#include <HTTPClient.h>

#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

#include <ESPmDNS.h>
#include <SPIFFS.h>
#include <FS.h>

#define SKETCH_VERSION "ESP32_AnchorLight.1.0" // Must be following the pattern: <NAME>.Major.Minor

// WiFi variables
String remote_wifi_ssid 		= "";
String remote_wifi_password 	= "";

String local_wifi_ssid			= "ESP32";
String local_wifi_password		= "00000000";

// Webserver variables
AsyncWebServer server(80);

char config_page_html[] 	= "/confpage.htm";	// HTML containing the configuration page
char ota_form_html[] 		= "/otaform.htm";   // HTML for updating the firmware via OTA
char noota_html []			= "/noota.htm";		// Response HTML if no new software is available
char update_ok_html[]	 	= "/updok.htm";		// Response HTML when software update is done
char default_html[]			= "/confpage.htm";	// Default HTML containing the payload. Here: no default page is needed, so confpage can be re-used.

// OTA configuration
String update_url			= ""; 	//URL to the update script e.g. "http://riopiri.ch/arduino/update.php".
String path_to_update_file = ""; 	// URL to the update file. Will be provided by the webserver (if there is an update available), in the function checkForSoftwareUpdate()
bool do_the_update_now = false;

// Configuration file name in EEPROM
char config_file[] = "/config.txt";

// Initialize Telegram BOT
String BOTtoken; // your Bot Token (Get from Botfather)

// Use @myidbot to find out the chat ID of an individual or a group
// Also note that you need to click "start" on a bot before it can message you

String ADMIN_CHAT_ID;	// My chat ID.
String allowed_chat_ids; // Chat Ids which will be permitted to send commands to the Bot

const int ledPin = 23;	// PIN controlled by the Telegram message. Connect relays here.
bool ledState = LOW;

WiFiClientSecure client;
UniversalTelegramBot bot(BOTtoken, client);

const unsigned long DELAY = 1000;  // Delay between scans for new messages
unsigned long lastTimeBotRan;

String device_IP;

void setup()
{
	Serial.begin(9600);
	Serial.print(SKETCH_VERSION); Serial.println(" started.");

	Serial.println("---------------- Setup --------------------");

	// Launch SPIFFS file system
	if(!SPIFFS.begin())
	{
		Serial.println("An Error has occurred while mounting SPIFFS");
		Serial.println("I will try to format the file system first...");
		Serial.print("Formatting file system ok: "); Serial.println(SPIFFS.format());
		if(!SPIFFS.begin()) Serial.println("Fatal error while mounting SPIFFS.");
	}
	else
	{
		Serial.print("SPIFFS Free: "); Serial.println(humanReadableSize((SPIFFS.totalBytes() - SPIFFS.usedBytes())));
		Serial.print("SPIFFS Used: "); Serial.println(humanReadableSize(SPIFFS.usedBytes()));
		Serial.print("SPIFFS Total: "); Serial.println(humanReadableSize(SPIFFS.totalBytes()));
		parseConfigFile();
	}

	// Initialize relais PIN
	pinMode(ledPin, OUTPUT);
	digitalWrite(ledPin, ledState);

	// Connect to WiFi
	connectToWiFi();

	Serial.println(WiFi.localIP());

	// Setup webserver
	server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", handleDisplayConfigFormHTML()); });
	server.on("/default", 			HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", handleDisplayDefaultHTML()); });
	server.on("/displayConfigForm", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", handleDisplayConfigFormHTML()); });
	server.on("/displayOTAUpdateForm", displayOTAUpdateForm); // Display form confirming software update
	server.on("/handleDeleteFile", 	HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", handleDeleteFileRequest(request)); });
	server.on("/handleConfigForm", 	HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", handleConfigForm(request)); });
	server.on("/handleOTAUpdate", handleOTARequest);  // Process OTA and show result
	server.on("/handleViewFile", 	HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/html", handleViewFileRequest(request)); });

	server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){ request->send(200, "text/plain", String(ESP.getFreeHeap())); });

	server.on("/reboot", handleRebootRequest);
	server.on("/uploadDocumentForm", HTTP_POST, [](AsyncWebServerRequest *request) {}, [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) { handleUpload(request, filename, index, data, len, final);});

	server.begin();


	Serial.print("Initialize Telegram bot: "); Serial.println(BOTtoken);
	client.setCACert(TELEGRAM_CERTIFICATE_ROOT); // Add root certificate for api.telegram.org

	bot.updateToken(BOTtoken);
	if(ADMIN_CHAT_ID.length()>0) bot.sendMessage(ADMIN_CHAT_ID, "Telegram bot started up", ""); // Sending message to ADMIN, notifying that the bot started.

	Serial.println("------------- Setup completed -------------");
}

void loop()
{
	if (millis() > lastTimeBotRan + DELAY)  {

		watchdog(); // Run watchdog periodically. (To reconnect to WiFi, in case AccessPoint has been turned off temporarily.)

		int numNewMessages = bot.getUpdates(bot.last_message_received + 1);

		while(numNewMessages) {
			handleNewMessages(numNewMessages);
			numNewMessages = bot.getUpdates(bot.last_message_received + 1);
		}

		lastTimeBotRan = millis();
	}

	// Check global variable if update shall be processed
	if(do_the_update_now==true)
	{
		processUpdate();
		do_the_update_now=false; // reset to false, otherwise will try to upload new firmware every second.
	}

}


// Method creates a HTTP request querying whether there is a software update on the repository for this MAC ID.
// required META tags are added to the client request to identify the device.
int checkForSoftwareUpdate()
{
	Serial.println("checkForSoftwareUpdate()");
	WiFiClient wificlient; // @suppress("Abstract class cannot be instantiated")
	//	wificlient.setInsecure();
	HTTPClient client;

	// Headers
	char XMACAddress[] 		= "X-ESP32-MAC";		// -> WiFi.macAddress();
	char XSketchSize[] 		= "X-ESP32-SKETCH-SIZE"; // -> sketchSize
	char XSketchVersion[] 	= "X-ESP32-SKETCH-VERSION"; // -> sketchVersion
	char XchipSize[] 		= "X-ESP32-CHIP-SIZE";
	char XFreeSpace[] 		= "X-ESP32-FREE-SPACE";
	char XsketchMD5[] 		= "X-ESP32-SKETCH-MD5";


	client.setUserAgent("ESP32HTTPClient");
	client.addHeader(XMACAddress, WiFi.macAddress());
	client.addHeader(XSketchSize, (String)ESP.getSketchSize());
	client.addHeader(XSketchVersion, SKETCH_VERSION);
	client.addHeader(XchipSize, (String)ESP.getFlashChipSize());
	client.addHeader(XFreeSpace, (String)ESP.getFreeSketchSpace());
	client.addHeader(XsketchMD5, ESP.getSketchMD5());

	int responseCode = (int)client.POST("NodeMCU OTA UpdateRequest.");

	Serial.print("Response code: "); Serial.println(responseCode);

	if(responseCode==200)
	{
		path_to_update_file = client.getString();
		Serial.print("Path to update file: "); Serial.println(path_to_update_file);
		return responseCode;
	}

	client.end();

	return responseCode;
}

// Routine to connect to WiFi hotspot. If a connection cannot be set up, the device creates a hotspot.
// Configuration parameters in config.txt
void connectToWiFi()
{
	// Try to connect to configured hotspot
	// Connect to WiFi network
	WiFi.mode(WIFI_STA);
	WiFi.begin(remote_wifi_ssid.c_str(), remote_wifi_password.c_str());
	Serial.println("");

	// Wait for connection
	for(int i=0; i<60; i++) // Trying for 30 seconds
	{
		if(WiFi.status() != WL_CONNECTED)
		{
			delay(500);
			Serial.print(".");
		}
		else
		{
			device_IP = WiFi.localIP().toString();

			Serial.println("");
			Serial.println("---WiFi connected---");
			Serial.print("IP address: ");  Serial.println(WiFi.localIP());
			Serial.print("SSID: "); Serial.println(WiFi.SSID());
			Serial.print("MAC address: "); Serial.println(WiFi.macAddress());
			Serial.print("Gateway IP: "); Serial.println(WiFi.gatewayIP());

			break;
		}
	}

	if(WiFi.status() != WL_CONNECTED) // Connection to remote access point could not be established.
	{
		// A local access point is created and the configuration page will be displayed under the servers IP
		Serial.print("\nCould not connect to AccessPoint:\t "); Serial.println(remote_wifi_ssid);
		Serial.println("Make sure SSID and password of AccessPoint are configured correctly.");
		Serial.println("Starting local AccessPoint with local configuration instead!");
		Serial.print("SSID: "); Serial.println(local_wifi_ssid);
		Serial.print("PWD: "); Serial.println(local_wifi_password);

		WiFi.mode(WIFI_AP);
		WiFi.softAP(local_wifi_ssid.c_str(), local_wifi_password.c_str());

		device_IP = WiFi.softAPIP().toString();

		Serial.print("Local IP: "); Serial.println(device_IP);

		delay(500);

		// Start mDNS service
		if (MDNS.begin("esp32"))
		{
			MDNS.addService("http", "tcp", 80);
			Serial.println("Go to: http://esp32.local");
		}
		else Serial.println("MDNS service could not be started.");
	}
}


// Display OTA update form
void displayOTAUpdateForm(AsyncWebServerRequest *request)
{
	Serial.println("displayOTAUpdateForm()");

	// Check for update on server.
	// Show OTA Form in case there is an update

	int OTA_available = checkForSoftwareUpdate();

	if(OTA_available==-1)
	{

		String html= readFile(noota_html);

		html.replace("$server_response", "No route to host. Check AccessPoint configuration");
		html.replace("$esp_info", getESPInfo());

		request->send(200, "text/html", html);

		Serial.println("No route to host. Check AccessPoint configuration and if there is internet connectivity.");

	}
	else if(OTA_available==200)
	{

		String html= readFile(ota_form_html);

		Serial.println("OTA is available. Send OTA confirmation page to client.");
		request->send(200, "text/html", html);
	}
	else // Show NO UPDATE AVAILABE if no new software is available
	{
		String html= readFile(noota_html);


		Serial.println("NO OTA available. ");

		html.replace("$server_response", (String)OTA_available);
		html.replace("$esp_info", getESPInfo());

		request->send(200, "text/html", html);
	}


}


// Returns some basic prameters regarding theuilt in ESP32 moldule
String getESPInfo()
{

	//	<div class="two columns">Boot mode</div><div class="ten columns">ESP.getBootMode()</div>

	String s = "";
	s.concat("<div class=\"row\"><div class=\"two columns\">Current SW version:</div><div class=\"ten columns\">"); s.concat(SKETCH_VERSION); s.concat("</div></div>");
	s.concat("<div class=\"row\"><div class=\"two columns\">Device MAC ID:</div><div class=\"ten columns\">"); s.concat(WiFi.macAddress()); s.concat("</div></div>");
	s.concat("<div class=\"row\"><div class=\"two columns\">Flash chip size:</div><div class=\"ten columns\">"); s.concat(ESP.getFlashChipSize()); s.concat("</div></div>");
	s.concat("<div class=\"row\"><div class=\"two columns\">Free sketch space:</div><div class=\"ten columns\">"); s.concat(ESP.getFreeSketchSpace()); s.concat("</div></div>");
	s.concat("<div class=\"row\"><div class=\"two columns\">Sketch size: </div><div class=\"ten columns\">"); s.concat(ESP.getSketchSize()); s.concat("</div></div>");
	s.concat("<div class=\"row\"><div class=\"two columns\">Sketch MD5: </div><div class=\"ten columns\">"); s.concat(ESP.getSketchMD5()); s.concat("</div></div>");
	s.concat("<div class=\"row\"><div class=\"two columns\">Free heap:</div><div class=\"ten columns\">"); s.concat(ESP.getFreeHeap()); s.concat("</div></div>");

	return s;
}


// Routine for handling the submitted configuration form
String handleConfigForm(AsyncWebServerRequest *request)
{
	Serial.println("handleConfigForm()");
	// Get all parameters from form and update variables

	File f = SPIFFS.open(config_file, FILE_WRITE); // @suppress("Abstract class cannot be instantiated")

	if (!f) {
		Serial.print("Could not open file:"); Serial.print(config_file); Serial.println(" for writing.");
	}
	else
	{
		// Prepare new config file, with updated variables
		String new_config = "";

		// variable = (condition) ? expressionTrue : expressionFalse;

		Serial.println("NEW CONFIGURATION:");
		Serial.println(new_config);

		new_config.concat("remote_wifi_ssid=");			new_config.concat((request->arg("remote_wifi_ssid").length() > 0) ? 		request->arg("remote_wifi_ssid") : remote_wifi_ssid); 						new_config.concat("\n");
		new_config.concat("remote_wifi_password="); 	new_config.concat((request->arg("remote_wifi_password").length() > 0) ? 	request->arg("remote_wifi_password") : remote_wifi_password); 				new_config.concat("\n");
		new_config.concat("local_wifi_ssid="); 			new_config.concat((request->arg("local_wifi_ssid").length() > 0) ? 			request->arg("local_wifi_ssid") : local_wifi_ssid);							new_config.concat("\n");
		new_config.concat("local_wifi_password="); 		new_config.concat((request->arg("local_wifi_password").length() > 0) ? 		request->arg("local_wifi_password") : local_wifi_password); 				new_config.concat("\n");
		new_config.concat("BOTtoken="); 				new_config.concat((request->arg("BOTtoken").length() > 0) ? 				request->arg("BOTtoken") : BOTtoken); 										new_config.concat("\n");
		new_config.concat("ADMIN_CHAT_ID="); 			new_config.concat((request->arg("ADMIN_CHAT_ID").length() > 0) ? 			request->arg("ADMIN_CHAT_ID") : ADMIN_CHAT_ID); 							new_config.concat("\n");
		new_config.concat("allowed_chat_ids="); 		new_config.concat((request->arg("allowed_chat_ids").length() > 0) ? 		request->arg("allowed_chat_ids") : allowed_chat_ids); 						new_config.concat("\n");
		new_config.concat("update_url="); 				new_config.concat((request->arg("update_url").length() > 0) ? 				request->arg("update_url") : update_url); 									new_config.concat("\n");

		// Write new config file
		Serial.print("Write to flash: "); Serial.println(config_file);
		Serial.println(new_config);

		f.print(new_config);
		f.close();

		// Reload configuration.
		parseConfigFile();
	}

	// Display form again
	return handleDisplayConfigFormHTML();
}

// User clicked on DELETE button on config page
String handleDeleteFileRequest(AsyncWebServerRequest *request)
{
	Serial.println("handleDeleteFileRequest()");
	Serial.print("Deleting file: "); Serial.println(request->arg("deleteFileFromSPIFF"));

	if( SPIFFS.remove(request->arg("deleteFileFromSPIFF").c_str()) == true)
	{
		Serial.print("File: "); Serial.print(request->arg("deleteFileFromSPIFF")); Serial.println(" removed from SPIFF");
	}
	else
	{
		Serial.print("Error: Could not delete file: "); Serial.println(request->arg("deleteFileFromSPIFF"));
	}

	return handleDisplayConfigFormHTML();

}

// User opend configuration page
String handleDisplayConfigFormHTML()
{
	Serial.println("handleDisplayConfigFormHTML()");

	// Read configuration html from file

	String sConfigFileHTML = readFile(config_page_html);
	// If the config HTML file has not yet been uploaded send basic upload form.
	if( sConfigFileHTML.length() < 1) sConfigFileHTML =  PROGMEM R"=====(<!DOCTYPE html><html><head></head><body><div><h2>ESP32 configuration page - upload html content first!</h2><h4>Files section</h4><label for="files_on_server">Files on server</label>$files_on_server<form action = "/uploadDocumentForm" method="post" enctype="multipart/form-data"><div><div class="ten columns"><label>Upload file</label><input type="file" label="Select file" name="file" name="file"><input type="submit" value="Upload"></div></div></form><hr/></div></body></html>)=====";

	// Replace placeholders by variable content
	sConfigFileHTML.replace("$SKETCH_VERSION", 		SKETCH_VERSION);
	sConfigFileHTML.replace("$remote_wifi_ssid", 		remote_wifi_ssid);
	sConfigFileHTML.replace("$remote_wifi_password", 	remote_wifi_password);
	sConfigFileHTML.replace("$local_wifi_ssid", 		local_wifi_ssid);
	sConfigFileHTML.replace("$local_wifi_password", 	local_wifi_password);
	sConfigFileHTML.replace("$update_url", 				update_url);
	sConfigFileHTML.replace("$BOTtoken", 				BOTtoken);
	sConfigFileHTML.replace("$ADMIN_CHAT_ID", 			ADMIN_CHAT_ID);
	sConfigFileHTML.replace("$allowed_chat_ids", 		allowed_chat_ids);
	sConfigFileHTML.replace("$files_on_server", 		listFilesInSPIFF());

	return sConfigFileHTML;
}


// Function displays default page
String handleDisplayDefaultHTML()
{
	Serial.println("handleDisplayDefaultHTML()");

	// Read configuration html from file
	String sDefaultHTML = readFile(default_html);

	// Replace placeholders by variable content
	sDefaultHTML.replace("$SKETCH_VERSION", SKETCH_VERSION);


	// Send HTML to client
	return sDefaultHTML;
}


// Routine for handling the OTA update request
// Is called when the user clicks on "Update" button
void handleOTARequest(AsyncWebServerRequest *request)
{
	Serial.println("handleOTARequest()");
	String html;

	int OTA_available = checkForSoftwareUpdate(); // Doublechecking if the update is still available.

	if(OTA_available==-1)
	{
		Serial.println("No route to host. Check AccessPoint configuration.");
		html = readFile(noota_html);
		html.replace("$server_response", "No route to host. Check AccessPoint configuration");
		request->send(200, "text/html", html);

	}
	else if(OTA_available==200) // PHP script for checking if there is an update returned 200 OK.
	{
		// Update is availiable on server
		Serial.println("Found new software update. Processing update now.");

		html = readFile(update_ok_html);

		do_the_update_now = true;
		// The variable do_the_update_now indicates whether an update shall be processed or not.

		request->send(200, "text/html", html);

	}
	else
	{
		Serial.println("No software update found.");
		String sNoOtaHTML = readFile(noota_html);
		Serial.print("RESPONSECODE");Serial.println((String)OTA_available);
		sNoOtaHTML.replace("$server_response", (String)OTA_available);
		request->send(200, "text/html", sNoOtaHTML);

	}
}



// Is called when user clicks on REBOOT button on configuration page
void handleRebootRequest(AsyncWebServerRequest *request)
{
	Serial.println("handleRebootRequest()");
	String html = readFile(default_html);

	html.replace("$SKETCH_VERSION", SKETCH_VERSION);

	request->send(200, "text/html", html);

	Serial.print("Rebooting in 3 seconds.");
	for(int i=0;i<30; i++)
	{
		esp_task_wdt_reset(); // Feeding the watchdog...
		Serial.print(".");
		delay(100);
	}

	ESP.restart();
}

// Handle Telegram messages
void handleNewMessages(int numNewMessages) {

	Serial.println("handleNewMessages");

	for (int i=0; i<numNewMessages; i++)
	{
		String chat_id = String(bot.messages[i].chat_id);
		String text = bot.messages[i].text;

		Serial.print("Request received from: "); Serial.print(bot.messages[i].chat_id); Serial.print(" , request: "); Serial.println(text);

		//		if (chat_id != CHAT_ID) {
		if (allowed_chat_ids.indexOf(chat_id)<0)
		{
			bot.sendMessage(chat_id, "Sorry, but you are not authorized to talk to me!", "");
			bot.sendMessage(ADMIN_CHAT_ID, "Unauthorized user with chatID=" + chat_id + " tried to contact the Anchorlight Bot!", "");
			continue;
		}

		String from_name = bot.messages[i].from_name;

		if (text == "/start") {
			String welcome = "Welcome, " + from_name + ".\n";
			welcome += "Use the following commands to control your boats anchor light.\n\n";
			welcome += "/anchor_on to turn anchor light ON \n";
			welcome += "/anchor_off to turn anchor light OFF \n";
			welcome += "/state to request current state of the anchor light.\n";

			bot.sendMessage(chat_id, welcome, "");
		}

		else if (text == "/anchor_on") {
			bot.sendMessage(chat_id, "Anchor light is switched ON", "");
			ledState = HIGH;
			digitalWrite(ledPin, ledState);
		}
		else if (text == "/anchor_off") {
			bot.sendMessage(chat_id, "Anchor light is switched OFF", "");
			ledState = LOW;
			digitalWrite(ledPin, ledState);
		}
		else if (text == "/state")
		{
			if (digitalRead(ledPin)){
				bot.sendMessage(chat_id, "Anchor light is ON", "");
			}
			else{
				bot.sendMessage(chat_id, "Anchor light is OFF", "");
			}
		}
		else
		{
			bot.sendMessage(chat_id, "What the heck do you want from me? Go away! :-)", "");
		}
	}
}


// File upload function
void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){

	Serial.println("handleUpload()");
	if(!index){
		Serial.println((String)"UploadStart: " + filename);
		request->_tempFile = SPIFFS.open("/" + filename, FILE_WRITE);
	}

	if(len) {
		Serial.println("Writing data....");

		request->_tempFile.write(data, len); // @suppress("Method cannot be resolved")
	}
	else
	{
		Serial.print("File length: "); Serial.println(len);
	}

	if(final){
		request->_tempFile.close(); // @suppress("Method cannot be resolved")
		request->send(200, "text/html", handleDisplayConfigFormHTML());
	}
}


// User clicked on FILE Link on config page to display file
String handleViewFileRequest(AsyncWebServerRequest * request)
{

	Serial.println("handleViewFileRequest()");

	char *cstr = new char[request->arg("readFileFromSPIFF").length() + 1];
	strcpy(cstr, request->arg("readFileFromSPIFF").c_str());

	return readFile(cstr);
}



// Make size of files human readable
// source: https://github.com/CelliesProjects/minimalUploadAuthESP32
String humanReadableSize(const size_t bytes) {
	if (bytes < 1024) return String(bytes) + " B";
	else if (bytes < (1024 * 1024)) return String(bytes / 1024.0) + " KB";
	else if (bytes < (1024 * 1024 * 1024)) return String(bytes / 1024.0 / 1024.0) + " MB";
	else return String(bytes / 1024.0 / 1024.0 / 1024.0) + " GB";
}

// Function checks if the configuration is availiable.
bool isConfigured()
{
	if(readFile(config_file).length()>0) return true;
	return false;
}

// Function reads all files in SPIFFS
String listFilesInSPIFF()
{
	File root = SPIFFS.open("/", FILE_READ); // @suppress("Abstract class cannot be instantiated") // @suppress("Invalid arguments") // @suppress("Method cannot be resolved")
	String filesOnServer = "";
	while (File file = root.openNextFile(FILE_READ)) // @suppress("Abstract class cannot be instantiated")
	{
		filesOnServer.concat("<div class=\"row\">");
		filesOnServer.concat("<div class=\"three columns\"><a href=\"/handleViewFile?readFileFromSPIFF=");filesOnServer.concat(file.name());filesOnServer.concat("\">"); filesOnServer.concat(file.name());filesOnServer.concat("</a>&nbsp;");filesOnServer.concat(file.size());filesOnServer.concat("&nbsp;bytes"); filesOnServer.concat("</div>\n");
		filesOnServer.concat("<div class=\"nine columns\"><a href=\"/handleDeleteFile?deleteFileFromSPIFF=");filesOnServer.concat(file.name()); filesOnServer.concat("\"><input class=\"button-primary\" type=\"submit\" value=\"Delete\"></a></div>\n");
		filesOnServer.concat("</div>");
	}

	return filesOnServer;
}

// Parse configuratin file and update variables and parameters with values from file.
void parseConfigFile()
{
	Serial.println("parseConfigFile()");

	String line;

	File f = SPIFFS.open(config_file, FILE_READ);

	if (!f) {
		Serial.print("Could not read file:");
		Serial.println(config_file);
	}
	else
	{
		int i=0;

		while(f.available())
		{
			i++;
			line = f.readStringUntil('\n');

			String name = line.substring(0, line.indexOf('='));
			name.trim();

			String value = line.substring(line.indexOf('=')+1, line.length());
			value.trim();

			if(name.equalsIgnoreCase("remote_wifi_ssid"))
			{
				remote_wifi_ssid=value;
			}
			else if(name.equalsIgnoreCase("remote_wifi_password"))
			{
				remote_wifi_password=value;
			}
			else if(name.equalsIgnoreCase("local_wifi_ssid"))
			{
				local_wifi_ssid=value;
			}
			else if(name.equalsIgnoreCase("local_wifi_password"))
			{
				local_wifi_password=value;
			}
			else if(name.equalsIgnoreCase("BOTtoken"))
			{
				BOTtoken=value;
			}
			else if(name.equalsIgnoreCase("ADMIN_CHAT_ID"))
			{
				ADMIN_CHAT_ID=value;
			}
			else if(name.equalsIgnoreCase("allowed_chat_ids"))
			{
				allowed_chat_ids=value;
			}
			else if(name.equalsIgnoreCase("update_url"))
			{
				update_url=value;
			}
			else
			{
				Serial.print("Configuration parameter:");
				Serial.print(name);
				Serial.println(" unknown. No change to config file.");
				Serial.print("Parameter length: "); Serial.println(name.length());
				Serial.print("Value: "); Serial.print(value); Serial.print(" length: "); Serial.println(value.length());
				return;
			}
		}
		Serial.print("--> ");Serial.print(i);Serial.print(" <--");Serial.println(" config entries found in file.");
		f.close();
	}

	Serial.println("----- Configuration -----");
	Serial.print("remote_wifi_ssid:\t " ); 		Serial.println(remote_wifi_ssid);
	Serial.print("remote_wifi_pwd:\t " ); 		Serial.println(remote_wifi_password);
	Serial.print("local_wifi_ssid:\t " ); 		Serial.println(local_wifi_ssid);
	Serial.print("local_wifi_password:\t " ); 	Serial.println(local_wifi_password);
	Serial.print("update_url:\t " ); 			Serial.println(update_url);
	Serial.print("BOTtoken:\t " ); 				Serial.println(BOTtoken);
	Serial.print("ADMIN_CHAT_ID:\t " ); 		Serial.println(ADMIN_CHAT_ID);
	Serial.print("allowed_chat_ids:\t " ); 		Serial.println(allowed_chat_ids);

	Serial.println("--- Configuration end ---");
}


// Routine for downloading the binary and applying the update.
// Return true if update was successful
// Return false if update failed
HTTPUpdateResult processUpdate()
{
	Serial.println("processUpdate()");

	// Add optional callback notifiers
	//	ESPhttpUpdate.onStart(update_started);
	//	ESPhttpUpdate.onEnd(update_finished);
	//	ESPhttpUpdate.onProgress(update_progress);
	//	ESPhttpUpdate.onError(update_error);

	ESPhttpUpdate.rebootOnUpdate(true);

	// Read update file from server
	Serial.print("Update file located in: "); Serial.println(path_to_update_file);
	Serial.print("Processing update now!");

	HTTPUpdateResult res = ESPhttpUpdate.update(path_to_update_file);
	Serial.print("Update ok:"); Serial.println(res);

	return res;

}

// Function reads file line by line into String variable and returns the String
String readFile(char* filename)
{
	Serial.print("readFile() -> "); Serial.println(filename);

	File f = SPIFFS.open(filename, FILE_READ);
	String s;

	if (!f) {
		Serial.print("Could not read file:"); Serial.println(filename);
	}
	else
	{
		//Data from file
		s=f.readString();
		f.close();
	}

	return s;
}

void watchdog()
{
	// Reconnect to WiFi if there is no connection availiable.
	if(WiFi.status() != WL_CONNECTED && device_IP.length()<=0)
	{
		Serial.print("WiFi Status="); Serial.println(WiFi.status());
		connectToWiFi();
	}
}
