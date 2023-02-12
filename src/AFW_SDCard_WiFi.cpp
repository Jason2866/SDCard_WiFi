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

#include <WiFiManager.h>
#include "Version.h"
#include "WebOTA.h"
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include "SDFS.h"
#include <SPI.h>
#include "sdios.h"
#include <ESPWebDAV.h>

#include <FtpServer.h>
#include <FreeStack.h>

#define HOSTNAME "SDCard_WiFi"

// SD card chip select
#define SD_CS 5
int chipSelect = SD_CS;
#define SPI_SPEED SD_SCK_MHZ(50)
int spi = SD_SCK_MHZ(50);

// Webserver Infopage, Firmwareupdate
#define WEB_SERVER_PORT 80

FS& gfs = SDFS;

// Object for FtpServer
//  Command port and Data port in passive mode can be defined here
// FtpServer ftpSrv( 221, 25000 );
// FtpServer ftpSrv( 421 ); // Default data port in passive mode is 55600
FtpServer ftpSrv; // Default command port is 21 ( !! without parenthesis !! )
#define ftp_user user
#define ftp_pass password

// WebDAV server
#define WEBDAV_SERVER_PORT 8080
WiFiServer tcp(WEBDAV_SERVER_PORT);

ESPWebDAV dav;
String statusMessage;
bool initFailed = false;

// Wifi
WiFiManager wifiManager;

// LED
#define LED_PIN 2


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
        //we have connected to WiFi
        MDNS.begin(HOSTNAME);
	Serial.println("connected...");
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

        SDFSConfig config;
        config.setCSPin(chipSelect);
	SDFS.setConfig(config);
	gfs.begin();
        tcp.begin();
        dav.begin(&tcp, &gfs);
        dav.setTransferStatusCallback([](const char* name, int percent, bool receive)
    {
        Serial.printf("%s: '%s': %d%%\n", receive ? "recv" : "send", name, percent);
    });

	Serial.println("WebDAV server started");
	ftpSrv.begin(ftp_user,ftp_pass);

	// Setup LED
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, HIGH);

	Serial.println("");
}

void loop()
{
	MDNS.update();
	dav.handleClient();
	ftpSrv.handleFTP();
	// Web OTA update
	webota.handle();

	// Handle WiFi Manager
	// wifiManager.process();
}
