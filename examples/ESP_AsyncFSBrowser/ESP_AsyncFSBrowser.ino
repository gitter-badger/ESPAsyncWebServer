#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>

// WEB HANDLER IMPLEMENTATION
class SPIFFSEditor: public AsyncWebHandler {
  private:
    String _username;
    String _password;
    bool _uploadAuthenticated;
  public:
    SPIFFSEditor(String username=String(), String password=String()):_username(username),_password(password),_uploadAuthenticated(false){}
    bool canHandle(AsyncWebServerRequest *request){
      if(request->method() == HTTP_GET && request->url() == "/edit" && (SPIFFS.exists("/edit.htm") || SPIFFS.exists("/edit.htm.gz")))
        return true;
      else if(request->method() == HTTP_GET && request->url() == "/list")
        return true;
      else if(request->method() == HTTP_GET && (request->url().endsWith("/") || SPIFFS.exists(request->url()) || (!request->hasParam("download") && SPIFFS.exists(request->url()+".gz"))))
        return true;
      else if(request->method() == HTTP_POST && request->url() == "/edit")
        return true;
      else if(request->method() == HTTP_DELETE && request->url() == "/edit")
        return true;
      else if(request->method() == HTTP_PUT && request->url() == "/edit")
        return true;
      return false;
    }

    void handleRequest(AsyncWebServerRequest *request){
      if(_username.length() && (request->method() != HTTP_GET || request->url() == "/edit" || request->url() == "/list") && !request->authenticate(_username.c_str(),_password.c_str()))
        return request->requestAuthentication();

      if(request->method() == HTTP_GET && request->url() == "/edit"){
        request->send(SPIFFS, "/edit.htm");
      } else if(request->method() == HTTP_GET && request->url() == "/list"){
        if(request->hasParam("dir")){
          String path = request->getParam("dir")->value();
          Dir dir = SPIFFS.openDir(path);
          path = String();
          String output = "[";
          while(dir.next()){
            File entry = dir.openFile("r");
            if (output != "[") output += ',';
            bool isDir = false;
            output += "{\"type\":\"";
            output += (isDir)?"dir":"file";
            output += "\",\"name\":\"";
            output += String(entry.name()).substring(1);
            output += "\"}";
            entry.close();
          }
          output += "]";
          request->send(200, "text/json", output);
          output = String();
        }
        else
          request->send(400);
      } else if(request->method() == HTTP_GET){
        String path = request->url();
        if(path.endsWith("/"))
          path += "index.htm";
        request->send(SPIFFS, path, String(), request->hasParam("download"));
      } else if(request->method() == HTTP_DELETE){
        if(request->hasParam("path", true)){
          ESP.wdtDisable(); SPIFFS.remove(request->getParam("path", true)->value()); ESP.wdtEnable(10);
          request->send(200, "", "DELETE: "+request->getParam("path", true)->value());
        } else
          request->send(404);
      } else if(request->method() == HTTP_POST){
        if(request->hasParam("data", true, true) && SPIFFS.exists(request->getParam("data", true, true)->value()))
          request->send(200, "", "UPLOADED: "+request->getParam("data", true, true)->value());
        else
          request->send(500);
      } else if(request->method() == HTTP_PUT){
        if(request->hasParam("path", true)){
          String filename = request->getParam("path", true)->value();
          if(SPIFFS.exists(filename)){
            request->send(200);
          } else {
            File f = SPIFFS.open(filename, "w");
            if(f){
              f.write(0x00);
              f.close();
              request->send(200, "", "CREATE: "+filename);
            } else {
              request->send(500);
            }
          }
        } else
          request->send(400);
      }
    }

    void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
      if(!index){
        if(!_username.length() || request->authenticate(_username.c_str(),_password.c_str()))
          _uploadAuthenticated = true;
        request->_tempFile = SPIFFS.open(filename, "w");
      }
      if(_uploadAuthenticated && request->_tempFile && len){
        ESP.wdtDisable(); request->_tempFile.write(data,len); ESP.wdtEnable(10);
      }
      if(_uploadAuthenticated && final)
        if(request->_tempFile) request->_tempFile.close();
    }
};


// SKETCH BEGIN
AsyncWebServer server(80);

const char* ssid = "**********";
const char* password = "************";
const char* http_username = "admin";
const char* http_password = "admin";


extern "C" void system_set_os_print(uint8 onoff);
extern "C" void ets_install_putc1(void* routine);

//Use the internal hardware buffer
static void _u0_putc(char c){
  while(((U0S >> USTXC) & 0x7F) == 0x7F);
  U0F = c;
}

void initSerial(){
  Serial.begin(115200);
  ets_install_putc1((void *) &_u0_putc);
  system_set_os_print(1);
}

void setup(){
  initSerial();
  SPIFFS.begin();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("STA: Failed!\n");
    WiFi.disconnect(false);
    delay(1000);
    WiFi.begin(ssid, password);
  }
  ArduinoOTA.begin();
  //Serial.printf("format start\n"); SPIFFS.format();  Serial.printf("format end\n");

  server.serveStatic("/fs", SPIFFS, "/");

  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.addHandler(new SPIFFSEditor(http_username,http_password));

  server.onNotFound([](AsyncWebServerRequest *request){
    os_printf("NOT_FOUND: ");
    if(request->method() == HTTP_GET)
      os_printf("GET");
    else if(request->method() == HTTP_POST)
      os_printf("POST");
    else if(request->method() == HTTP_DELETE)
      os_printf("DELETE");
    else if(request->method() == HTTP_PUT)
      os_printf("PUT");
    else if(request->method() == HTTP_PATCH)
      os_printf("PATCH");
    else if(request->method() == HTTP_HEAD)
      os_printf("HEAD");
    else if(request->method() == HTTP_OPTIONS)
      os_printf("OPTIONS");
    else
      os_printf("UNKNOWN");
    os_printf(" http://%s%s\n", request->host().c_str(), request->url().c_str());

    if(request->contentLength()){
      os_printf("_CONTENT_TYPE: %s\n", request->contentType().c_str());
      os_printf("_CONTENT_LENGTH: %u\n", request->contentLength());
    }

    int headers = request->headers();
    int i;
    for(i=0;i<headers;i++){
      AsyncWebHeader* h = request->getHeader(i);
      os_printf("_HEADER[%s]: %s\n", h->name().c_str(), h->value().c_str());
    }

    int params = request->params();
    for(i=0;i<params;i++){
      AsyncWebParameter* p = request->getParam(i);
      if(p->isFile()){
        os_printf("_FILE[%s]: %s, size: %u\n", p->name().c_str(), p->value().c_str(), p->size());
      } else if(p->isPost()){
        os_printf("_POST[%s]: %s\n", p->name().c_str(), p->value().c_str());
      } else {
        os_printf("_GET[%s]: %s\n", p->name().c_str(), p->value().c_str());
      }
    }

    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index)
      os_printf("UploadStart: %s\n", filename.c_str());
    os_printf("%s", (const char*)data);
    if(final)
      os_printf("UploadEnd: %s (%u)\n", filename.c_str(), index+len);
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
    if(!index)
      os_printf("BodyStart: %u\n", total);
    os_printf("%s", (const char*)data);
    if(index + len == total)
      os_printf("BodyEnd: %u\n", total);
  });
  server.begin();
}

void loop(){
  ArduinoOTA.handle();
}

