/*
 Name:		V_sensor_asm.ino
 Created:	06.03.2020 06:32:32
 Author:	PfeifferP
*/


#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include <ArduinoJson.h>


FS* filesystem = &SPIFFS;
//FS* filesystem = &LittleFS;

#define DBG_OUTPUT_PORT Serial

#ifndef STASSID
#define STASSID "MikroTik-220CF6"
#define STAPSK  "Sanifar123!"
#endif

const char* ssid = STASSID;
const char* password = STAPSK;
const char* host = "esp8266fs";

ESP8266WebServer server(80);
//holds the current upload
File fsUploadFile;

//load config
bool loadConfig() {
    File configFile = filesystem->open("/config.json", "r");
    if (!configFile) {
        DBG_OUTPUT_PORT.println("Failed to open config file");
        return false;
    }

    size_t size = configFile.size();
    if (size > 1024) {
        DBG_OUTPUT_PORT.println("Config file size is too large");
        return false;
    }

    // Allocate a buffer to store contents of the file.
    std::unique_ptr<char[]> buf(new char[size]);

    // We don't use String here because ArduinoJson library requires the input
    // buffer to be mutable. If you don't use ArduinoJson, you may as well
    // use configFile.readString instead.
    configFile.readBytes(buf.get(), size);

    StaticJsonDocument<200> doc;
    auto error = deserializeJson(doc, buf.get());
    if (error) {
        DBG_OUTPUT_PORT.println("Failed to parse config file");
        return false;
    }

    const char* serverName = doc["serverName"];
    const char* accessToken = doc["accessToken"];

    // Real world application would store these values in some variables for
    // later use.

    DBG_OUTPUT_PORT.print("Loaded serverName: ");
    DBG_OUTPUT_PORT.println(serverName);
    DBG_OUTPUT_PORT.print("Loaded accessToken: ");
    DBG_OUTPUT_PORT.println(accessToken);
    return true;
}

//save config or new config
bool saveConfig() {
    StaticJsonDocument<200> doc;
    doc["serverName"] = "api.example.com";
    doc["accessToken"] = "128du9as8du12eoue8da98h123ueh9h98";

    File configFile = filesystem->open("/config.json", "w");
    if (!configFile) {
        DBG_OUTPUT_PORT.println("Failed to open config file for writing");
        return false;
    }

    serializeJson(doc, configFile);
    return true;
}


//format bytes
String formatBytes(size_t bytes) {
    if (bytes < 1024) { return String(bytes) + "B"; }
    else if (bytes < (1024 * 1024)) { return String(bytes / 1024.0) + "KB"; }
    else if (bytes < (1024 * 1024 * 1024)) { return String(bytes / 1024.0 / 1024.0) + "MB"; }
    else { return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB"; }
}

String getContentType(String filename) {
    if (server.hasArg("download")) { return "application/octet-stream"; }
    else if (filename.endsWith(".htm")) { return "text/html"; }
    else if (filename.endsWith(".html")) { return "text/html"; }
    else if (filename.endsWith(".css")) { return "text/css"; }
    else if (filename.endsWith(".js")) { return "application/javascript"; }
    else if (filename.endsWith(".png")) { return "image/png"; }
    else if (filename.endsWith(".gif")) { return "image/gif"; }
    else if (filename.endsWith(".jpg")) { return "image/jpeg"; }
    else if (filename.endsWith(".ico")) { return "image/x-icon"; }
    else if (filename.endsWith(".xml")) { return "text/xml"; }
    else if (filename.endsWith(".pdf")) { return "application/x-pdf"; }
    else if (filename.endsWith(".zip")) { return "application/x-zip"; }
    else if (filename.endsWith(".gz")) { return "application/x-gzip"; }
    return "text/plain";
}

bool handleFileRead(String path) {
    DBG_OUTPUT_PORT.println("handleFileRead: " + path);
    if (path.endsWith("/")) {
        path += "index.htm";
    }
    String contentType = getContentType(path);
    String pathWithGz = path + ".gz";
    if (filesystem->exists(pathWithGz) || filesystem->exists(path)) {
        if (filesystem->exists(pathWithGz)) {
            path += ".gz";
        }
        File file = filesystem->open(path, "r");
        server.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

void handleFileUpload() {
    if (server.uri() != "/edit") {
        return;
    }
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        String filename = upload.filename;
        if (!filename.startsWith("/")) {
            filename = "/" + filename;
        }
        DBG_OUTPUT_PORT.print("handleFileUpload Name: "); DBG_OUTPUT_PORT.println(filename);
        fsUploadFile = filesystem->open(filename, "w");
        filename = String();
    }
    else if (upload.status == UPLOAD_FILE_WRITE) {
        //DBG_OUTPUT_PORT.print("handleFileUpload Data: "); DBG_OUTPUT_PORT.println(upload.currentSize);
        if (fsUploadFile) {
            fsUploadFile.write(upload.buf, upload.currentSize);
        }
    }
    else if (upload.status == UPLOAD_FILE_END) {
        if (fsUploadFile) {
            fsUploadFile.close();
        }
        DBG_OUTPUT_PORT.print("handleFileUpload Size: "); DBG_OUTPUT_PORT.println(upload.totalSize);
    }
}

void handleFileDelete() {
    if (server.args() == 0) {
        return server.send(500, "text/plain", "BAD ARGS");
    }
    String path = server.arg(0);
    DBG_OUTPUT_PORT.println("handleFileDelete: " + path);
    if (path == "/") {
        return server.send(500, "text/plain", "BAD PATH");
    }
    if (!filesystem->exists(path)) {
        return server.send(404, "text/plain", "FileNotFound");
    }
    filesystem->remove(path);
    server.send(200, "text/plain", "");
    path = String();
}

void handleFileCreate() {
    if (server.args() == 0) {
        return server.send(500, "text/plain", "BAD ARGS");
    }
    String path = server.arg(0);
    DBG_OUTPUT_PORT.println("handleFileCreate: " + path);
    if (path == "/") {
        return server.send(500, "text/plain", "BAD PATH");
    }
    if (filesystem->exists(path)) {
        return server.send(500, "text/plain", "FILE EXISTS");
    }
    File file = filesystem->open(path, "w");
    if (file) {
        file.close();
    }
    else {
        return server.send(500, "text/plain", "CREATE FAILED");
    }
    server.send(200, "text/plain", "");
    path = String();
}

void handleFileList() {
    if (!server.hasArg("dir")) {
        server.send(500, "text/plain", "BAD ARGS");
        return;
    }

    String path = server.arg("dir");
    DBG_OUTPUT_PORT.println("handleFileList: " + path);
    Dir dir = filesystem->openDir(path);
    path = String();

    String output = "[";
    while (dir.next()) {
        File entry = dir.openFile("r");
        if (output != "[") {
            output += ',';
        }
        bool isDir = false;
        output += "{\"type\":\"";
        output += (isDir) ? "dir" : "file";
        output += "\",\"name\":\"";
        if (entry.name()[0] == '/') {
            output += &(entry.name()[1]);
        }
        else {
            output += entry.name();
        }
        output += "\"}";
        entry.close();
    }

    output += "]";
    server.send(200, "text/json", output);
}

void handleLogin() {                         // If a POST request is made to URI /login
    if (!server.hasArg("username") || !server.hasArg("password") || server.arg("username") == NULL || server.arg("password") == NULL) { // If the POST request doesn't have username and password data
        server.send(400, "text/plain", "400: Invalid Request");         // The request is invalid, so send HTTP status 400
        return;
    }
    if (server.arg("username") == "John Doe" && server.arg("password") == "password123") { // If both the username and the password are correct
        server.send(200, "text/html", "<h1>Welcome, " + server.arg("username") + "!</h1><p>Login successful</p>");
    }
    else {                                                                              // Username and password don't match
        server.send(401, "text/plain", "401: Unauthorized");
    }
}

void setup(void) {
    DBG_OUTPUT_PORT.begin(115200);
    DBG_OUTPUT_PORT.print("\n");
    DBG_OUTPUT_PORT.setDebugOutput(true);
    filesystem->begin();
    {
        Dir dir = filesystem->openDir("/");
        while (dir.next()) {
            String fileName = dir.fileName();
            size_t fileSize = dir.fileSize();
            DBG_OUTPUT_PORT.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
        }
        DBG_OUTPUT_PORT.printf("\n");
    }
    //Load Config -------------------------------------------
    if (!loadConfig()) {
        DBG_OUTPUT_PORT.println("Failed to load config");
        saveConfig();
    }
    else {
        DBG_OUTPUT_PORT.println("Config loaded");
    }

    //WIFI INIT

    delay(10);

    WiFi.mode(WIFI_STA);
    delay(500);
    WiFi.beginSmartConfig();

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        DBG_OUTPUT_PORT.print(".");
        if (WiFi.smartConfigDone()) {
            DBG_OUTPUT_PORT.println("WiFi Smart Config Done.");
        }
    }

    DBG_OUTPUT_PORT.println("");
    DBG_OUTPUT_PORT.println("");

    WiFi.printDiag(DBG_OUTPUT_PORT);


    /* DBG_OUTPUT_PORT.printf("Connecting to %s\n", ssid);
     if (String(WiFi.SSID()) != String(ssid)) {
         WiFi.mode(WIFI_STA);
         WiFi.begin(ssid, password);
     }

     while (WiFi.status() != WL_CONNECTED) {
         delay(500);
         DBG_OUTPUT_PORT.print(".");
     }
     DBG_OUTPUT_PORT.println("");
     DBG_OUTPUT_PORT.print("Connected! IP address: ");
     DBG_OUTPUT_PORT.println(WiFi.localIP());
     */

    MDNS.begin(host);
    DBG_OUTPUT_PORT.print("Open http://");
    DBG_OUTPUT_PORT.print(host);
    DBG_OUTPUT_PORT.println(".local/edit to see the file browser");


    //SERVER INIT
    //server login 
    server.on("/login", HTTP_POST, []() {
        server.send(200, "text/plain", "");
        }, handleLogin);
    //list directory
    server.on("/list", HTTP_GET, handleFileList);
    //load editor
    server.on("/edit", HTTP_GET, []() {
        if (!handleFileRead("/edit.htm")) {
            server.send(404, "text/plain", "FileNotFound");
        }
        });
    //create file
    server.on("/edit", HTTP_PUT, handleFileCreate);
    //delete file
    server.on("/edit", HTTP_DELETE, handleFileDelete);
    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    server.on("/edit", HTTP_POST, []() {
        server.send(200, "text/plain", "");
        }, handleFileUpload);

    //called when the url is not defined here
    //use it to load content from SPIFFS
    server.onNotFound([]() {
        if (!handleFileRead(server.uri())) {
            server.send(404, "text/plain", "FileNotFound");
        }
        });

    //get heap status, analog input value and all GPIO statuses in one json call
    server.on("/all", HTTP_GET, []() {
        String json = "{";
        json += "\"heap\":" + String(ESP.getFreeHeap());
        json += ", \"analog\":" + String(analogRead(A0));
        json += ", \"gpio\":" + String((uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16)));
        json += "}";
        server.send(200, "text/json", json);
        json = String();
        });
    server.begin();
    DBG_OUTPUT_PORT.println("HTTP server started");



}

void loop(void) {
    server.handleClient();
    MDNS.update();
}