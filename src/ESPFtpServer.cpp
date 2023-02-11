/*
 * FTP Serveur for ESP8266
 * based on FTP Serveur for Arduino Due and Ethernet shield (W5100) or WIZ820io (W5200)
 * based on Jean-Michel Gallego's work
 * based on David Paiva david@nailbuster.com
 * modified to work with BTT TF Cloud devices by Albrecht Lohofener albrechtloh@gmx.de
 *  - Added support for SDFAT
 *  - Added folder support
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

#include "ESPFtpServer.h"

#include <ESP8266WiFi.h>
#include <WiFiClient.h>

using namespace sdfat;

WiFiServer ftpServer(FTP_CTRL_PORT);
WiFiServer dataServer(FTP_DATA_PORT_PASV);

void FtpServer::begin(String uname, String pword, int chipSelectPin, SPISettings spiSettings)
{
  // Tells the ftp server to begin listening for incoming connection
  _FTP_USER = uname;
  _FTP_PASS = pword;

  ftpServer.begin();
  delay(10);
  dataServer.begin();
  delay(10);
  millisTimeOut = (uint32_t)FTP_TIME_OUT * 60 * 1000;
  millisDelay = 0;
  cmdStatus = 0;
  SD_Status = 0;
  iniVariables();

  this->chipSelectPin = chipSelectPin;
  this->spiSettings = spiSettings;
}

void FtpServer::iniVariables()
{
  // Default for data port
  dataPort = FTP_DATA_PORT_PASV;

  // Default Data connection is Active
  dataPassiveConn = true;

  // Set the root directory
  strcpy(cwdName, "/");

  rnfrCmd = false;
  transferStatus = 0;
}

void FtpServer::handleFTP()
{
  if ((int32_t)(millisDelay - millis()) > 0)
    return;

  if (ftpServer.hasClient())
  {
#ifdef FTP_DEBUG
    Serial.println("Stop previous connection");
#endif
    disconnectClient();
    client = ftpServer.available();
    cmdStatus = 1;
  }

  if (cmdStatus == 0)
  {
    if (client.connected())
      disconnectClient();
    cmdStatus = 1;
  }
  else if (cmdStatus == 1) // Ftp server waiting for connection
  {
    abortTransfer();
    iniVariables();
#ifdef FTP_DEBUG
    Serial.println("Ftp server waiting for connection on port " + String(FTP_CTRL_PORT));
#endif
    cmdStatus = 2;
  }
  else if (cmdStatus == 2) // Ftp server idle
  {

    if (client.connected()) // A client connected
    {
      clientConnected();
      millisEndConnection = millis() + 10 * 1000; // wait client id during 10 s.
      cmdStatus = 3;
    }
  }
  else if (readChar() > 0) // got response
  {
    if (cmdStatus == 3) // Ftp server waiting for user identity
      if (userIdentity())
        cmdStatus = 4;
      else
        cmdStatus = 0;
    else if (cmdStatus == 4) // Ftp server waiting for user registration
      if (userPassword())
      {
        cmdStatus = 5;
        millisEndConnection = millis() + millisTimeOut;

        initSD();
      }
      else
        cmdStatus = 0;
    else if (cmdStatus == 5) // Ftp server waiting for user command
      if (!processCommand())
      {
        cmdStatus = 0;
      }
      else
        millisEndConnection = millis() + millisTimeOut;
  }
  else if (!client.connected() || !client)
  {
    cmdStatus = 1;
#ifdef FTP_DEBUG
    Serial.println("client disconnected");
#endif
  }

  if (transferStatus == 1) // Retrieve data
  {
    if (!doRetrieve())
      transferStatus = 0;
  }
  else if (transferStatus == 2) // Store data
  {
    if (!doStore())
      transferStatus = 0;
  }
  else if (cmdStatus > 2 && !((int32_t)(millisEndConnection - millis()) > 0))
  {
    client.println("530 Timeout");
    millisDelay = millis() + 200; // delay of 200 ms
    cmdStatus = 0;
  }
}

void FtpServer::clientConnected()
{
#ifdef FTP_DEBUG
  Serial.println("Client connected!");
#endif
  client.println("220--- Welcome to FTP for ESP8266 ---");
  client.println("220---   By David Paiva, Albrecht Lohofener and others ---");
  client.println("220 --   Version " + String(FTP_SERVER_VERSION) + "   --");
  iCL = 0;
}

void FtpServer::disconnectClient()
{
#ifdef FTP_DEBUG
  Serial.println(" Disconnecting client");
#endif
  abortTransfer();
  client.println("221 Goodbye");
  client.stop();
}

boolean FtpServer::userIdentity()
{
  if (strcmp(command, "USER"))
    client.println("500 Syntax error");
  if (strcmp(parameters, _FTP_USER.c_str()))
    client.println("530 user not found");
  else
  {
    client.println("331 OK. Password required");

    strcpy(cwdName, "/");
    return true;
  }
  millisDelay = millis() + 100; // delay of 100 ms
  return false;
}

boolean FtpServer::userPassword()
{
  if (strcmp(command, "PASS"))
    client.println("500 Syntax error");
  else if (strcmp(parameters, _FTP_PASS.c_str()) && strcmp(_FTP_USER.c_str(), "anonymous"))
    client.println("530 ");
  else
  {
#ifdef FTP_DEBUG
    Serial.println("OK. Waiting for commands.");
#endif
    client.println("230 OK.");
    return true;
  }
  millisDelay = millis() + 100; // delay of 100 ms
  return false;
}

boolean FtpServer::processCommand()
{

  ///////////////////////////////////////
  //                                   //
  //      ACCESS CONTROL COMMANDS      //
  //                                   //
  ///////////////////////////////////////

  //
  //  CDUP - Change to Parent Directory
  //
  if (!strcmp(command, "CDUP"))
  {
    char *ch = strrchr(cwdName, '/');

    // Calc pos
    int pos = ch - cwdName;
    if (pos > 0)
      *ch = '\0'; // Get parent dir
    else
      strcpy(cwdName, "/");

    client.println("250 Ok. Current directory is " + String(cwdName));
  }
  //
  //  CWD - Change Working Directory
  //
  else if (!strcmp(command, "CWD"))
  {
    char path[FTP_CWD_SIZE];

    if (strcmp(parameters, ".") == 0) // 'CWD .' is the same as PWD command
    {
      client.println("257 \"" + String(cwdName) + "\" is your current directory");
    }
    else
    {
      if (parameters[0] == '/') // Estimate full path
      {
        strcpy(path, parameters);
      }
      else
      {
        strcpy(path, cwdName);
        strcat(path, "/");
        strcat(path, parameters);
      }

      if(SD.exists(path))
      {
        // Update current working directory
        strcpy(cwdName, path);

        client.println("250 Ok. Current directory is " + String(cwdName));
      }
      else
      {
        client.println("550 Can't open directory " + String(cwdName));
      }
    }
  }
  //
  //  PWD - Print Directory
  //
  else if (!strcmp(command, "PWD"))
    client.println("257 \"" + String(cwdName) + "\" is your current directory");
  //
  //  QUIT
  //
  else if (!strcmp(command, "QUIT"))
  {
    disconnectClient();
    return false;
  }

  ///////////////////////////////////////
  //                                   //
  //    TRANSFER PARAMETER COMMANDS    //
  //                                   //
  ///////////////////////////////////////

  //
  //  MODE - Transfer Mode
  //
  else if (!strcmp(command, "MODE"))
  {
    if (!strcmp(parameters, "S"))
      client.println("200 S Ok");
    // else if( ! strcmp( parameters, "B" ))
    //  client.println( "200 B Ok\r\n";
    else
      client.println("504 Only S(tream) is suported");
  }
  //
  //  PASV - Passive Connection management
  //
  else if (!strcmp(command, "PASV"))
  {
    if (data.connected())
      data.stop();

    dataIp = client.localIP();
    dataPort = FTP_DATA_PORT_PASV;

#ifdef FTP_DEBUG
    Serial.println("Connection management set to passive");
    Serial.println("Data port set to " + String(dataPort));
#endif
    client.println("227 Entering Passive Mode (" + String(dataIp[0]) + "," + String(dataIp[1]) + "," + String(dataIp[2]) + "," + String(dataIp[3]) + "," + String(dataPort >> 8) + "," + String(dataPort & 255) + ").");
    dataPassiveConn = true;
  }
  //
  //  PORT - Data Port
  //
  else if (!strcmp(command, "PORT"))
  {
    if (data)
      data.stop();
    // get IP of data client
    dataIp[0] = atoi(parameters);
    char *p = strchr(parameters, ',');
    for (uint8_t i = 1; i < 4; i++)
    {
      dataIp[i] = atoi(++p);
      p = strchr(p, ',');
    }
    // get port of data client
    dataPort = 256 * atoi(++p);
    p = strchr(p, ',');
    dataPort += atoi(++p);
    if (p == NULL)
      client.println("501 Can't interpret parameters");
    else
    {

      client.println("200 PORT command successful");
      dataPassiveConn = false;
    }
  }
  //
  //  STRU - File Structure
  //
  else if (!strcmp(command, "STRU"))
  {
    if (!strcmp(parameters, "F"))
      client.println("200 F Ok");
    // else if( ! strcmp( parameters, "R" ))
    //  client.println( "200 B Ok\r\n";
    else
      client.println("504 Only F(ile) is suported");
  }
  //
  //  TYPE - Data Type
  //
  else if (!strcmp(command, "TYPE"))
  {
    if (!strcmp(parameters, "A"))
      client.println("200 TYPE is now ASII");
    else if (!strcmp(parameters, "I"))
      client.println("200 TYPE is now 8-bit binary");
    else
      client.println("504 Unknow TYPE");
  }

  ///////////////////////////////////////
  //                                   //
  //        FTP SERVICE COMMANDS       //
  //                                   //
  ///////////////////////////////////////

  //
  //  ABOR - Abort
  //
  else if (!strcmp(command, "ABOR"))
  {
    abortTransfer();
    client.println("226 Data connection closed");
  }
  //
  //  DELE - Delete a File
  //
  else if (!strcmp(command, "DELE"))
  {
    char path[FTP_CWD_SIZE];
    if (strlen(parameters) == 0)
    {
      client.println("501 No file name");
    }
    else if (makePath(path))
    {
      //try.. if( ! SPIFFS.exists( path ))
      if (!SD.exists(path))
      {
        client.println("550 File " + String(parameters) + " not found");
      }
      else
      {
        //try.. if( SPIFFS.remove( path ))
        if (SD.remove(path))
        {
          client.println("250 Deleted " + String(parameters));
        }
        else
        {
          client.println("450 Can't delete " + String(parameters));
        }
      }
    }
  }
  //
  //  LIST - List
  //
  else if (!strcmp(command, "LIST"))
  {
    client.println("502 Command not implemented");
#ifdef FTP_DEBUG
    Serial.println("Command not implemented");
#endif

    // if( ! dataConnect())
    //   client.println( "425 No data connection");
    // else
    // {
    //   client.println( "150 Accepted data connection");
    //   uint16_t nm = 0;
    //   SdFile dir;
    //   dir.open(SD.vwd(), cwdName, O_READ);
    //   dir.rewind();

    //   data.println( "Type=cdir;Perm=cmpel; " + String(cwdName));
    //   data.println( "Type=pdir;Perm=el; ");
    //   nm = 2;

    //   SdFile entry;
    //   while(entry.openNext(&dir, O_READ))
    //   {
    //     entry.getName(buf, sizeof(buf));
    //     #ifdef FTP_DEBUG
    //     Serial.print("Folder content "); Serial.println(buf);
    //     #endif

    //     // File name
    //     char buf[255];
    //     entry.getName(buf, sizeof(buf));
    //     String fn = String(buf);

    //     // File size
    //     String fs = String(entry.fileSize());

    //     // File date
    //     dir_t dir;
    //     entry.dirEntry(&dir);

    //     tm tmStr;
    //     tmStr.tm_hour = FAT_HOUR(dir.lastWriteTime);
    //     tmStr.tm_min = FAT_MINUTE(dir.lastWriteTime);
    //     tmStr.tm_sec = FAT_SECOND(dir.lastWriteTime);
    //     tmStr.tm_year = FAT_YEAR(dir.lastWriteDate) - 1900;
    //     tmStr.tm_mon = FAT_MONTH(dir.lastWriteDate) - 1;
    //     tmStr.tm_mday = FAT_DAY(dir.lastWriteDate);
    //     time_t t2t = mktime(&tmStr);
    //     tm *gTm = gmtime(&t2t);
    //     sprintf(buf, "%04d%02d%02d%02d%02d%02d", gTm->tm_year + 1900, gTm->tm_mon, gTm->tm_mday, gTm->tm_hour, gTm->tm_min, gTm->tm_sec);
    //     String fileTimeStamp = String(buf);

    //     if(entry.isDir())
    //       data.println( "Type=dir;modify=" + fileTimeStamp + ";Perm=cpmel; " + fn);
    //     else
    //       data.println( "Type=file;Size=" + fs + ";"+"modify=" + fileTimeStamp + ";" + " " + fn);
    //     nm ++;
    //     entry.close();
    //   }

    //   client.println( "226 LIST completed");
    //   /*client.println( "226-options: -a -l");
    //   client.println( "226 " + String(nm) + " matches total");*/

    //   dir.close();
    // }

    // data.stop();
  }
  //
  //  MLSD - Listing for Machine Processing (see RFC 3659)
  //
  else if (!strcmp(command, "MLSD"))
  {
    if (!dataConnect())
      client.println("425 No data connection MLSD");
    else
    {
      client.println("150 Accepted data connection");
      uint16_t nm = 0;
      SdFile dir;
      dir.open(SD.vwd(), cwdName, O_READ);
      dir.rewind();

      data.println("Type=cdir;Perm=cmpel; " + String(cwdName));
      data.println("Type=pdir;Perm=el; ");
      nm = 2;

      SdFile entry;
      while (entry.openNext(&dir, O_READ))
      {
        entry.getName(buf, sizeof(buf));
#ifdef FTP_DEBUG
        Serial.print("Folder content ");
        Serial.println(buf);
#endif

        // File name
        char buf[255];
        entry.getName(buf, sizeof(buf));
        String fn = String(buf);

        // File size
        String fs = String(entry.fileSize());

        // File date
        dir_t dir;
        entry.dirEntry(&dir);

        tm tmStr;
        tmStr.tm_hour = FAT_HOUR(dir.lastWriteTime);
        tmStr.tm_min = FAT_MINUTE(dir.lastWriteTime);
        tmStr.tm_sec = FAT_SECOND(dir.lastWriteTime);
        tmStr.tm_year = FAT_YEAR(dir.lastWriteDate) - 1900;
        tmStr.tm_mon = FAT_MONTH(dir.lastWriteDate) - 1;
        tmStr.tm_mday = FAT_DAY(dir.lastWriteDate);
        time_t t2t = mktime(&tmStr);
        tm *gTm = gmtime(&t2t);
        sprintf(buf, "%04d%02d%02d%02d%02d%02d", gTm->tm_year + 1900, gTm->tm_mon, gTm->tm_mday, gTm->tm_hour, gTm->tm_min, gTm->tm_sec);
        String fileTimeStamp = String(buf);

        if (entry.isDir())
          data.println("Type=dir;modify=" + fileTimeStamp + ";Perm=cpmel; " + fn);
        else
          data.println("Type=file;Size=" + fs + ";" + "modify=" + fileTimeStamp + ";" + " " + fn);
        nm++;
        entry.close();
      }

      client.println("226 MLSD completed");
      /*client.println( "226-options: -a -l");
      client.println( "226 " + String(nm) + " matches total");*/

      dir.close();
    }

    data.stop();
  }
  //
  //  NLST - Name List
  //
  else if (!strcmp(command, "NLST"))
  {
    Serial.println("NLST");
    if (!dataConnect())
      client.println("425 No data connection");
    else
    {
      client.println("150 Accepted data connection");
      uint16_t nm = 0;
      //try.. Dir dir=SPIFFS.openDir(cwdName);
      FatFile dir = SD.open(cwdName);
      //File dir=SD.open(cwdName);
      //try.. if( !SPIFFS.exists( cwdName ))
      if (!SD.exists(cwdName))
        client.println("550 Can't open directory " + String(parameters));
      else
      {
        FatFile entry;
        while (entry.openNext(&dir, O_READ))
        {
          char buf[255];
          entry.getName(buf, sizeof(buf));
          String fileName = String(buf);
          data.println(fileName);
          nm++;
        }
        client.println("226 " + String(nm) + " matches total");
      }
      data.stop();
    }
  }
  //
  //  NOOP
  //
  else if (!strcmp(command, "NOOP"))
  {
    // dataPort = 0;
    client.println("200 Zzz...");
  }
  //
  //  RETR - Retrieve
  //
  else if (!strcmp(command, "RETR"))
  {
    char path[FTP_CWD_SIZE];
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else if (makePath(path))
    {
      //try.. file = SPIFFS.open(path, "r");
      //SdFile file;
      file.open(path, O_READ);
      if (!file.isFile())
        client.println("550 File " + String(parameters) + " not found");
      else if (!file.isFile())
        client.println("450 Can't open " + String(parameters));
      else if (!dataConnect())
        client.println("425 No data connection");
      else
      {
#ifdef FTP_DEBUG
        Serial.println("Sending " + String(parameters));
#endif
        client.println("150-Connected to port " + String(dataPort));
        client.println("150 " + String(file.fileSize() + " bytes to download"));
        millisBeginTrans = millis();
        bytesTransfered = 0;
        transferStatus = 1;
      }
    }
  }
  //
  //  STOR - Store
  //
  else if (!strcmp(command, "STOR"))
  {
    char path[FTP_CWD_SIZE];
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else if (makePath(path))
    {
      //file = SPIFFS.open(path, "w");
      //try.. file = SD.open(path, "w");
      file.open(path, FILE_WRITE);
      //file.open(path, O_CREAT | O_WRITE);
      if (!file.isOpen())
        client.println("451 Can't open/create " + String(parameters));
      else if (!dataConnect())
      {
        client.println("425 No data connection");
        file.close();
      }
      else
      {
#ifdef FTP_DEBUG
        Serial.println("Receiving " + String(parameters));
#endif
        client.println("150 Connected to port " + String(dataPort));
        millisBeginTrans = millis();
        bytesTransfered = 0;
        transferStatus = 2;

        // // high speed raw write implementation
        // // close any previous file
        // file.close();
        // // delete old file
        // SD.remove(path);
      
        // // create a contiguous file
        // const size_t WRITE_BLOCK_CONST = 512;
        // size_t contBlocks = (contentLen/WRITE_BLOCK_CONST + 1);
        // uint32_t bgnBlock, endBlock;

        // if (!file.createContiguous(path, contBlocks * WRITE_BLOCK_CONST))
        //   client.println("550 Can't create File \"create contiguous sections failed\"");

        // // get the location of the file's blocks
        // if (!file.contiguousRange(&bgnBlock, &endBlock))
        //   client.println("550 Can't create File \"Unable to get contiguous range\"");

        // if (!SD.card()->writeStart(bgnBlock, contBlocks))
        //   client.println("550 Can't create File \"Unable to start writing contiguous range\"");
      }
    }
  }
  //
  //  MKD - Make Directory
  //
  else if (!strcmp(command, "MKD"))
  {
    char path[FTP_CWD_SIZE];

    if (parameters[0] == '/') // Estimate full path
    {
      strcpy(path, parameters);
    }
    else
    {
      strcpy(path, cwdName);
      strcat(path, "/");
      strcat(path, parameters);
    }

    // create directory
    if (!SD.mkdir(path, true))
      client.println("550 Can't create \"" + String(parameters));
    else
      client.println("200 Directory " + String(parameters) + " created");
  }
  //
  //  RMD - Remove a Directory
  //
  else if (!strcmp(command, "RMD"))
  {
    char path[FTP_CWD_SIZE];

    if (parameters[0] == '/') // Estimate full path
    {
      strcpy(path, parameters);
    }
    else
    {
      strcpy(path, cwdName);
      strcat(path, "/");
      strcat(path, parameters);
    }

    // delete a directory
    if (!SD.rmdir(path))
      client.println("501 Can't delete \"" + String(parameters));
    else
      client.println("200 Directory " + String(parameters) + " deleted");
  }
  //
  //  RNFR - Rename From
  //
  else if (!strcmp(command, "RNFR"))
  {
    buf[0] = 0;
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else if (makePath(buf))
    {
      //if( ! SPIFFS.exists( buf ))
      if (!SD.exists(buf))
        client.println("550 File " + String(parameters) + " not found");
      else
      {
#ifdef FTP_DEBUG
        Serial.println("Renaming " + String(buf));
#endif
        client.println("350 RNFR accepted - file or folder exists, ready for destination");
        rnfrCmd = true;
      }
    }
  }
  //
  //  RNTO - Rename To
  //
  else if (!strcmp(command, "RNTO"))
  {
    char path[FTP_CWD_SIZE];
    char dir[FTP_FIL_SIZE];
    if (strlen(buf) == 0 || !rnfrCmd)
      client.println("503 Need RNFR before RNTO");
    else if (strlen(parameters) == 0)
      client.println("501 No file name");
    else if (makePath(path))
    {
      if (SD.exists(path))
        client.println("553 " + String(parameters) + " already exists");
      else
      {
#ifdef FTP_DEBUG
        Serial.println("Renaming " + String(buf) + " to " + String(path));
#endif

      if (!SD.rename(buf, path))
        client.println("451 Rename/move from " + String(buf) + " to " + String(path) + " failure"); 
      else
        client.println("200 Rename/move of file or directory from " + String(buf) + " to " + String(path) + " successfully"); 
      }
    }
    rnfrCmd = false;
  }

  ///////////////////////////////////////
  //                                   //
  //   EXTENSIONS COMMANDS (RFC 3659)  //
  //                                   //
  ///////////////////////////////////////

  //
  //  FEAT - New Features
  //
  else if (!strcmp(command, "FEAT"))
  {
    client.println("211-Extensions suported:");
    client.println(" MLSD");
    client.println("211 End.");
  }
  //
  //  MDTM - File Modification Time (see RFC 3659)
  //
  else if (!strcmp(command, "MDTM"))
  {
    client.println("550 Unable to retrieve time");
  }

  //
  //  SIZE - Size of the file
  //
  else if (!strcmp(command, "SIZE"))
  {
    char path[FTP_CWD_SIZE];
    if (strlen(parameters) == 0)
      client.println("501 No file name");
    else if (makePath(path))
    {
      file.open(path, FILE_READ);
      if (!file.isOpen())
        client.println("450 Can't open " + String(parameters));
      else
      {
        client.println("213 " + String(file.fileSize()));
        file.close();
      }
    }
  }
  //
  //  SITE - System command
  //
  else if (!strcmp(command, "SITE"))
  {
    client.println("500 Unknow SITE command " + String(parameters));
  }
  //
  //  Unrecognized commands ...
  //
  else
    client.println("500 Unknow command");

  return true;
}

boolean FtpServer::dataConnect()
{
  unsigned long startTime = millis();
  //wait 5 seconds for a data connection
  if (!data.connected())
  {
    while (!dataServer.hasClient() && millis() - startTime < 10000)
    {
      //delay(100);
      yield();
    }
    if (dataServer.hasClient())
    {
      data.stop();
      data = dataServer.available();
#ifdef FTP_DEBUG
      Serial.println("ftpdataserver client....");
#endif
    }
  }

  return data.connected();
}

boolean FtpServer::doRetrieve()
{
  //int16_t nb = file.readBytes((uint8_t*) buf, FTP_BUF_SIZE );
  //int16_t nb = file.readBytes(buf, FTP_BUF_SIZE);
  int16_t nb = file.read(buf, FTP_BUF_SIZE);
  if (nb > 0)
  {
    data.write((uint8_t *)buf, nb);
    bytesTransfered += nb;
    return true;
  }
  closeTransfer();
  return false;
}

boolean FtpServer::doStore()
{
  if (data.connected())
  {
    int16_t nb = data.readBytes((uint8_t *)buf, FTP_BUF_SIZE);
    if (nb > 0)
    {
      // Serial.println( millis() << " " << nb << endl;
      file.write((uint8_t *)buf, nb);
      bytesTransfered += nb;
    }
    return true;
  }
  closeTransfer();
  return false;
}

void FtpServer::closeTransfer()
{
  uint32_t deltaT = (int32_t)(millis() - millisBeginTrans);
  if (deltaT > 0 && bytesTransfered > 0)
  {
    client.println("226-File successfully transferred");
    client.println("226 " + String(deltaT) + " ms, " + String(bytesTransfered / deltaT) + " kbytes/s");
  }
  else
    client.println("226 File successfully transferred");

  file.close();
  data.stop();
}

void FtpServer::abortTransfer()
{
  if (transferStatus > 0)
  {
    file.close();
    data.stop();
    client.println("426 Transfer aborted");
#ifdef FTP_DEBUG
    Serial.println("Transfer aborted!");
#endif
  }
  transferStatus = 0;
}

// Read a char from client connected to ftp server
//
//  update cmdLine and command buffers, iCL and parameters pointers
//
//  return:
//    -2 if buffer cmdLine is full
//    -1 if line not completed
//     0 if empty line received
//    length of cmdLine (positive) if no empty line received

int8_t FtpServer::readChar()
{
  int8_t rc = -1;

  if (client.available())
  {
    char c = client.read();
    // char c;
    // client.readBytes((uint8_t*) c, 1);
#ifdef FTP_DEBUG
    Serial.print(c);
#endif
    if (c == '\\')
      c = '/';
    if (c != '\r')
      if (c != '\n')
      {
        if (iCL < FTP_CMD_SIZE)
          cmdLine[iCL++] = c;
        else
          rc = -2; //  Line too long
      }
      else
      {
        cmdLine[iCL] = 0;
        command[0] = 0;
        parameters = NULL;
        // empty line?
        if (iCL == 0)
          rc = 0;
        else
        {
          rc = iCL;
          // search for space between command and parameters
          parameters = strchr(cmdLine, ' ');
          if (parameters != NULL)
          {
            if (parameters - cmdLine > 4)
              rc = -2; // Syntax error
            else
            {
              strncpy(command, cmdLine, parameters - cmdLine);
              command[parameters - cmdLine] = 0;

              while (*(++parameters) == ' ')
                ;
            }
          }
          else if (strlen(cmdLine) > 4)
            rc = -2; // Syntax error.
          else
            strcpy(command, cmdLine);
          iCL = 0;
        }
      }
    if (rc > 0)
      for (uint8_t i = 0; i < strlen(command); i++)
        command[i] = toupper(command[i]);
    if (rc == -2)
    {
      iCL = 0;
      client.println("500 Syntax error");
    }
  }
  return rc;
}

// Make complete path/name from cwdName and parameters
//
// 3 possible cases: parameters can be absolute path, relative path or only the name
//
// parameters:
//   fullName : where to store the path/name
//
// return:
//    true, if done

boolean FtpServer::makePath(char *fullName)
{
  return makePath(fullName, parameters);
}

boolean FtpServer::makePath(char *fullName, char *param)
{
  if (param == NULL)
    param = parameters;

  // Root or empty?
  if (strcmp(param, "/") == 0 || strlen(param) == 0)
  {
    strcpy(fullName, "/");
    return true;
  }
  // If relative path, concatenate with current dir
  if (param[0] != '/')
  {
    strcpy(fullName, cwdName);
    if (fullName[strlen(fullName) - 1] != '/')
      strncat(fullName, "/", FTP_CWD_SIZE);
    strncat(fullName, param, FTP_CWD_SIZE);
  }
  else
    strcpy(fullName, param);
  // If ends with '/', remove it
  uint16_t strl = strlen(fullName) - 1;

  if (fullName[strl] == '/' && strl > 1)
  {
    fullName[strl] = 0;
  }
  if (strlen(fullName) < FTP_CWD_SIZE)
  {
    return true;
  }

  client.println("500 Command line too long");
  return false;
}

// Calculate year, month, day, hour, minute and second
//   from first parameter sent by MDTM command (YYYYMMDDHHMMSS)
//
// parameters:
//   pyear, pmonth, pday, phour, pminute and psecond: pointer of
//     variables where to store data
//
// return:
//    0 if parameter is not YYYYMMDDHHMMSS
//    length of parameter + space

uint8_t FtpServer::getDateTime(uint16_t *pyear, uint8_t *pmonth, uint8_t *pday,
                               uint8_t *phour, uint8_t *pminute, uint8_t *psecond)
{
  char dt[15];

  // Date/time are expressed as a 14 digits long string
  //   terminated by a space and followed by name of file
  if (strlen(parameters) < 15 || parameters[14] != ' ')
    return 0;
  for (uint8_t i = 0; i < 14; i++)
    if (!isdigit(parameters[i]))
      return 0;

  strncpy(dt, parameters, 14);
  dt[14] = 0;
  *psecond = atoi(dt + 12);
  dt[12] = 0;
  *pminute = atoi(dt + 10);
  dt[10] = 0;
  *phour = atoi(dt + 8);
  dt[8] = 0;
  *pday = atoi(dt + 6);
  dt[6] = 0;
  *pmonth = atoi(dt + 4);
  dt[4] = 0;
  *pyear = atoi(dt);
  return 15;
}

// Create string YYYYMMDDHHMMSS from date and time
//
// parameters:
//    date, time
//    tstr: where to store the string. Must be at least 15 characters long
//
// return:
//    pointer to tstr

char *FtpServer::makeDateTimeStr(char *tstr, uint16_t date, uint16_t time)
{
  sprintf(tstr, "%04u%02u%02u%02u%02u%02u",
          ((date & 0xFE00) >> 9) + 1980, (date & 0x01E0) >> 5, date & 0x001F,
          (time & 0xF800) >> 11, (time & 0x07E0) >> 5, (time & 0x001F) << 1);
  return tstr;
}

// ------------------------
bool FtpServer::initSD()
{
  // ------------------------
  // initialize the SD card
  if (!isSDInit)
  {
    isSDInit = true;
    bool ret = SD.begin(chipSelectPin, spiSettings);

    if (!ret)
      Serial.println("FTP: Error opening SD card");
    else
      Serial.println("FTP: SD card init was successful");

    return ret;
  }
  else
  {
    return true;
  }
}