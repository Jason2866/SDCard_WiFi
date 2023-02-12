/*
 * FtpServer Arduino esp8266 with WifiManager
 * With SdFat version > 2 (full name and more size)
 *
 * #ifndef DEFAULT_FTP_SERVER_NETWORK_TYPE_ARDUINO
 *	#define DEFAULT_FTP_SERVER_NETWORK_TYPE_ARDUINO 	NETWORK_W5100
 *	#define DEFAULT_STORAGE_TYPE_ARDUINO STORAGE_SDFAT2
 * #endif
 *
 * AUTHOR:  Renzo Mischianti / Johann Obermeier
 *
 * https://www.mischianti.org/2020/02/08/ftp-server-on-esp8266-and-esp32
 *
 */

#include <ESP8266mDNS.h>
#include <WiFiManager.h>  // lib WifiManager https://github.com/tzapu/WiFiManager
#include <SdFat.h>
#include <sdios.h>
#include <FtpServer.h>
#include <FreeStack.h>

#define HOSTNAME "SDCard_WiFi"

// Define Chip Select for your SD card according to hardware
// #define CS_SDCARD 4  // SD card reader of Ehernet shield
#define CS_SDCARD 5 // Chip Select for SD card reader on Due

// Object for File system
SdFat sd;

// Object for FtpServer
//  Command port and Data port in passive mode can be defined here
// FtpServer ftpSrv( 221, 25000 );
// FtpServer ftpSrv( 421 ); // Default data port in passive mode is 55600
FtpServer ftpSrv; // Default command port is 21 ( !! without parenthesis !! )


ArduinoOutStream cout( Serial );

WiFiManager wifiManager;

void setup()
{
    Serial.begin( 115200 );

    // Start WiFi
	Serial.println("");
	Serial.println("--------------------------------");
	Serial.println("Connect to WiFi");
	Serial.println("--------------------------------");
	WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
	//wifiManager.setConnectTimeout(20);
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


    // Mount the SD card memory
    cout << F("Mount the SD card memory... ");
    if( ! sd.begin( CS_SDCARD, SD_SCK_MHZ( 50 )))
    {
        cout << F("Unable to mount SD card") << endl;
        while( true ) ;
    }
    cout << F("ok") << endl;

    // Show capacity and free space of SD card
    cout << F("Capacity of card:   ") << long( sd.card()->sectorCount() >> 1 )
        << F(" kBytes") << endl;
    cout << F("Free space on card: ")
        << long( sd.vol()->freeClusterCount() * sd.vol()->sectorsPerCluster() >> 1 )
        << F(" kBytes") << endl;

    // Initialize the FTP server
    ftpSrv.begin("user","password");
    // ftpSrv.init( externalIp );
    // ftpSrv.init( IPAddress( 11, 22, 33, 44 ));

    // Default username and password are set to 'arduino' and 'test'
    //  but can then be changed by calling ftpSrv.credentials()
    // ftpSrv.credentials( "myname", "123" );

    cout << F("Free stack: ") << FreeStack() << endl;

    cout << "Viaaa!";
}

void loop()
{
  ftpSrv.handleFTP();

  // more processes...
}
