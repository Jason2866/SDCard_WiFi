/*
 * Alternative firmware for BTT TF Cloud devices
 *
 * 2021 - Albrecht Lohofener (albrechtloh@gmx.de)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include "Version.h"
#include "ESPWebDAV.h"
#include "ESPFtpServer.h"
#include "WebOTA.h"

#define HOSTNAME "SDCard_WiFi"

// SD card
#define SD_CS 5

// Webserver Infopage, Firmwareupdate
#define WEB_SERVER_PORT 80

// WebDAV server
#define WEBDAV_SERVER_PORT 8080
ESPWebDAV dav;
String statusMessage;
bool initFailed = false;

// FTP server
FtpServer ftpSrv;

// BOOT button
#define BOOT_BUTTON_PIN 0
void handleBootButton(void);

// Wifi
WiFiManager wifiManager;

// LED
#define LED_PIN 2
void handleLED(void);

void setup()
{
	Serial.begin(115200);
	Serial.println("");
	Serial.println("");
	Serial.println("**********************************************");
	Serial.print(AFW_FULL_NAME);
	Serial.print(" Version ");
	Serial.println(AFW_VERSION);
	Serial.println("**********************************************");

	// Start WiFi
	Serial.println("");
	Serial.println("--------------------------------");
	Serial.println("Connect to WiFi");
	Serial.println("--------------------------------");
	WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
	/*wifiManager.setConfigPortalBlocking(false);
	wifiManager.setConnectTimeout(60);
	wifiManager.setConnectRetries(10);
	wifiManager.setWiFiAutoReconnect(true);*/
	bool res = wifiManager.autoConnect(HOSTNAME);

	if(!res) {
        Serial.println("Running config portal");
        // ESP.restart();
    }
    else {
        //if you get here you have connected to the WiFi
        Serial.println("connected...yeey :)");
    }

	// Init OTA firmware updater
	Serial.println("");
	Serial.println("--------------------------------");
	Serial.println("Start firmware update server");
	Serial.println("--------------------------------");
	webota.init_wifi(HOSTNAME);
	webota.init(WEB_SERVER_PORT, "/webota");

	// Start the WebDAV server
	Serial.println("");
	Serial.println("--------------------------------");
	Serial.println("Start WebDAV server");
	Serial.println("--------------------------------");
	if (!dav.init(WEBDAV_SERVER_PORT))
	{
		statusMessage = "An error occured while initialization of WebDAV server";
		Serial.print("ERROR: ");
		Serial.println(statusMessage);
		initFailed = true;
	}
	Serial.println("WebDAV server started");

	// Start FTP server
	Serial.println("");
	Serial.println("--------------------------------");
	Serial.println("Start FTP server");
	Serial.println("--------------------------------");
	ftpSrv.begin("anonymous", "", SD_CS, SPI_FULL_SPEED); //username, password for ftp.  set ports in ESP8266FtpServer.h  (default 21, 50009 for PASV)
	Serial.println("FTP server started");

	// Setup FLASH button and LED
	pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, HIGH);

	Serial.println("");
}

void loop()
{
	// WebDAV
	if (dav.isClientWaiting())
	{
		Serial.println("Client connected");
		if (initFailed)
			return dav.rejectClient(statusMessage);

		// call handle if server was initialized properly

		dav.initSD(SD_CS, SPI_FULL_SPEED);
		dav.handleClient();
	}

	// FTP
	ftpSrv.handleFTP(); //make sure in loop you call handleFTP()!!

	// Web OTA update
	webota.handle();

	// Handle WiFi Manager
	// wifiManager.process();
	// handleLED();
	handleBootButton();
}

// The flash button is used to reset the WiFi settings
void handleBootButton(void)
{
	if(digitalRead(BOOT_BUTTON_PIN) == 0)
	{
		delay(50); // Debounce

		if(digitalRead(BOOT_BUTTON_PIN) == 0)
		{
			Serial.println("BOOT button pressed");

			delay(5000); // Wait 5 seconds

			if(digitalRead(BOOT_BUTTON_PIN) == 0)
			{
				Serial.println("BOOT button pressed for 5 seconds ");

				// Let the LED 5 seconds blinking
				for(int i = 0; i< 25; i++)
				{
					digitalWrite(LED_PIN, LOW);
					delay(200);
					digitalWrite(LED_PIN, HIGH);
					delay(200);
				}

				Serial.println("Reset WiFi settings");
				wifiManager.resetSettings();

				Serial.println("Restarting ...");
				ESP.restart();
			}
		}
	}
}

// The LED blinks of the WiFi needs to be configured
// void handleLED(void)
// {
// 	static uint32_t millisLED = 0;
// 	static bool isTimeout = true;
// 	static bool toggle = false;

// 	if(wifiManager.getConfigPortalActive())
// 	{
// 		if(isTimeout)
// 		{
// 			millisLED = millis() + 0.5 * 1000; // 1 seconds
// 			isTimeout = false;
// 		}
// 	}
// 	else
// 	{
// 		// Switch off LED
// 		digitalWrite(LED_PIN, HIGH);
// 	}

// 	// Wait for timeout
// 	if(!(int32_t)(millisLED - millis()) > 0)
// 	{
// 		if(!isTimeout)
// 		{
// 			isTimeout = true;

// 			// Let the LED blink
// 			toggle = !toggle;
// 			digitalWrite(LED_PIN, toggle);
// 		}
// 	}
// }
