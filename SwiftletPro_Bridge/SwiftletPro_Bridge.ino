#include <Arduino.h>

#include <ArduinoJson.h>
#include <LinkedList.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>

#define EDGE_DISCOVERY_MESSAGE "Register Edge"
#define EDGE_DISCOVERY_NOTIF "Edge discovery finish"
#define REPORT_URL "http://103.236.201.63/api/v1/ReportData"
#define STATUS_URL "http://103.236.201.63/api/v1/MyBridge/?/status"

class Edge{
  String serial;
  String ip;
  String name;
  public:
    void set(String s, String i, String n){
      serial = s;
      ip = i;
      name = n;
    }
    String getEdgeSerial(){return serial;};
    String getEdgeIP(){return ip;};
    String getEdgeName(){return name;};
};

/*B : Bridge,
001 : 10^3 total devices,
H : Humidity*/
const char BRIDGE_ID[6] = "B001H";
const unsigned int LOCAL_UDP_PORT = 55056;
const unsigned int APP_PORT = 5037;
const unsigned int APP_PORT_2 = 5038;
const unsigned int EDGE_PORT = 55057;
const unsigned int ACTUATOR_PIN = 4; // D4 is GPIO 4 in Arduino. Is the same
unsigned long previousMillis = 0;
const long interval = 10000;
const IPAddress APIIPAddress(103,236,201,63);
boolean isAutomate = false;
boolean isActuate = false;

IPAddress AppIPAddress;
WiFiUDP Udp;
LinkedList<Edge> edges;
LinkedList<int> aggregateHumidity;

void setup()
{
  Serial.begin(115200);
  Serial.println();
  pinMode(ACTUATOR_PIN,OUTPUT);
  WiFi.begin("Michael's Open Network", "");

  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println();

  Serial.print("Connected to : ");
  Serial.println(WiFi.SSID());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());
  Serial.print("Connected, Local IP address: ");
  Serial.println(WiFi.localIP());
  Udp.begin(LOCAL_UDP_PORT);
  Serial.print("Begin Listening To App ");

}

void loop() {
    //Re-Initialize Packet Buffer
    char incomingPacket[255];
    //Listening
    int packetSize = Udp.parsePacket();
    if (packetSize)
    {
      Serial.printf("Received %d bytes from %s, port %d\n", packetSize,
      Udp.remoteIP().toString().c_str(), Udp.remotePort());
      int len = Udp.read(incomingPacket, 255);
      if (len > 0){incomingPacket[len] = 0;}
      //Serial.printf("UDP packet contents: %s\n", incomingPacket);

      //App broadcast receiver for bridge discovery
      if(Udp.remotePort() == APP_PORT ){
        char replyPacket[255];
        Udp.beginPacket(Udp.remoteIP(), Udp.remotePort());
        StaticJsonBuffer<400> jsonBuffer;
        JsonObject& successJson = jsonBuffer.createObject();
        successJson["serial"] = BRIDGE_ID;
        successJson["ip"] = WiFi.localIP().toString();
        successJson["name"] = "Swiftlet-Humidity-Pro";
        successJson.printTo(replyPacket,sizeof(replyPacket));
        successJson.printTo(Serial);
        Udp.write(replyPacket);
        Udp.endPacket();
        Serial.println("END PACKET");
      //App broadcast receiver for edge disovery
      //By first find the bridges, using its Serial ID
      }else if(Udp.remotePort() == APP_PORT_2){
        AppIPAddress = Udp.remoteIP();
        StaticJsonBuffer<400> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(incomingPacket);
        const char* serialNumber = root["serial"];
        if(strcmp(serialNumber,BRIDGE_ID)==0){
          Serial.println("MATCHED");
          IPAddress ipmulti(255,255,255,255);
          Udp.beginPacketMulticast(ipmulti,EDGE_PORT,WiFi.localIP());
          Udp.write(EDGE_DISCOVERY_MESSAGE);
          Udp.endPacket();
          Serial.println("END PACKET");
        }else{
          Serial.println("NOT MATCHED");
        }
      //Accept edge's responds
      }else if(Udp.remotePort() == EDGE_PORT){
        //Process the previous packet sent by the first edge
        //Read request value
        StaticJsonBuffer<200> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(incomingPacket);
        //Edge Registration
        if(strcmp(root["request"],"register")==0){
          Serial.println("Incoming Edge Request : REGISTER");
          LinkedList<Edge> discoveredEdges;
          const char* ss = root["serial"];String s = String(ss);
          const char* ii = root["ip"];String i = String(ii);
          const char* nn = root["name"];String n = String(nn);
          Edge e = createNewEdge(s,i,n);
          discoveredEdges.add(e);
          //Waiting for other edges by 5 second
          Serial.println("Populating Devices");
          int secondCounter = 0;
          while(secondCounter < 5){
            char edgeIncomingPacket[255];
            secondCounter++;
            delay(1000);
            int packetSize = Udp.parsePacket();
            if (packetSize){
              Serial.printf("Received %d bytes from %s, port %d\n", packetSize,
               Udp.remoteIP().toString().c_str(), Udp.remotePort());
              int len = Udp.read(edgeIncomingPacket, 255);
              if (len > 0){ edgeIncomingPacket[len] = 0;}
              Serial.printf("UDP packet contents: %s\n", edgeIncomingPacket);
              //Add the other responding edges
              StaticJsonBuffer<200> jBuffer;
              JsonObject& root2 = jBuffer.parseObject(edgeIncomingPacket);
              ss = root2["serial"];s = String(ss);
              ii = root2["ip"];i = String(ii);
              nn = root2["name"];n = String(nn);
              Edge edge = createNewEdge(s,i,n);
              discoveredEdges.add(edge);
            }
            Serial.print("Listening CountDown : ");
            Serial.println(secondCounter);
          }

          StaticJsonBuffer<500> edgeObjectJsonBuffer;
          String returnPacket;
          JsonObject& edgeJsonRoot = edgeObjectJsonBuffer.createObject();
          JsonArray& edgeJsonData = edgeJsonRoot.createNestedArray("data");
          Serial.println("Edges Found : ");
          for(int i=0;i<discoveredEdges.size();i++){
            JsonObject& edgeJson = edgeJsonData.createNestedObject();
            edgeJson["serial"] = discoveredEdges.get(i).getEdgeSerial();
            edgeJson["ip"] = discoveredEdges.get(i).getEdgeIP();
            edgeJson["name"] = discoveredEdges.get(i).getEdgeName();
            Serial.printf("Edge #%d\n", i);
            Serial.printf("Serial ID : %s\n",discoveredEdges.get(i).getEdgeSerial().c_str());
            Serial.printf("Name : %s\n",discoveredEdges.get(i).getEdgeName().c_str());
            Serial.printf("IP : %s\n",discoveredEdges.get(i).getEdgeIP().c_str());
          }
          Serial.print("Sending To : ");Serial.println(AppIPAddress.toString());
          Udp.beginPacket(AppIPAddress, APP_PORT_2);
          edgeJsonRoot.printTo(returnPacket);
          edgeJsonRoot.printTo(Serial);
          Udp.write(returnPacket.c_str());
          Udp.endPacket();

          IPAddress ipmulti(255,255,255,255);
          Udp.beginPacketMulticast(ipmulti,EDGE_PORT,WiFi.localIP());
          Udp.write(EDGE_DISCOVERY_NOTIF);
          Udp.endPacket();

        //Edges Report
      }else if(strcmp(root["request"],"report")==0){
          Serial.println("Incoming Edge Request : REPORT");
          //Read report data
          StaticJsonBuffer<500> jBuffer;
          String reportJson;
          JsonObject& reportJsonRoot = jBuffer.createObject();
          JsonObject& reportJsonData = reportJsonRoot.createNestedObject("data");
          reportJsonRoot["serial"] = root["serial"];
          reportJsonRoot["ip"] = root["ip"];
          reportJsonData["h"] = root["data"]["humidity"];
          reportJsonData["t"] = root["data"]["temperature"];
          Serial.println("\nSent Report : ");
          reportJsonRoot.printTo(Serial);
          reportJsonRoot.printTo(reportJson);

          if(aggregateHumidity.size() < 5){
            aggregateHumidity.add(root["data"]["humidity"]);
          }else{
            aggregateHumidity.shift();
            aggregateHumidity.add(root["data"]["humidity"]);
          }

          Serial.print("List Size : ");
          Serial.println(aggregateHumidity.size());

          HTTPClient http;
          http.begin(REPORT_URL);
          http.addHeader("Content-Type", "application/json");
          http.POST(reportJson);
          http.writeToStream(&Serial);
          http.end();
        }
      }
    }
    //Check status on interval
    if(isStatusTime()){
      String url = String(STATUS_URL);
      url.replace("?",BRIDGE_ID);
      Serial.print("URL : ");Serial.println(url);
      HTTPClient http;
      http.begin(url);
      if(http.GET() > 0 ){
        String response = http.getString();
        Serial.println(response);
        StaticJsonBuffer<200> jsonBuffer;
        JsonObject& root = jsonBuffer.parseObject(response);
        if(root["data"]["automate"]){isAutomate = true;}else{isAutomate = false;}
        if(root["data"]["actuate"]){isActuate= true;}else{isActuate = false;}
      }
      http.end();
      Serial.print("is Automate : ");Serial.println(isAutomate);
      Serial.print("is Actuate : ");Serial.println(isActuate);
      if(isActuate){Serial.println("Actuate is true");}else{Serial.println("Actuate is false");};

    }

    if((isActuate == 1) && (isAutomate == 0)){
      digitalWrite(ACTUATOR_PIN, HIGH);
    }else if((isAutomate == 1) && (isActuate == 0) && (aggregateHumidity.size() > 0)){
      //retrive using last value
      int totalHumidity = 0;
      int relativeHumidity = 0;
      int dataSize = aggregateHumidity.size();
      for(int i = 0; i < dataSize ; i++){
        totalHumidity += aggregateHumidity.get(i);
      }
      relativeHumidity = totalHumidity / dataSize;
      if((relativeHumidity > 40)){
        //dehumidify
        Serial.println("Auto HIGH");
        Serial.print("Total Humidity : ");
        Serial.println(totalHumidity);
        Serial.print("Relative Humidity : ");
        Serial.println(relativeHumidity);
        digitalWrite(ACTUATOR_PIN, HIGH);
      }else{
        //humidify
        Serial.println("Auto LOW");
        Serial.print("Total Humidity : ");
        Serial.println(totalHumidity);
        Serial.print("Relative Humidity : ");
        Serial.println(relativeHumidity);
        digitalWrite(ACTUATOR_PIN, LOW);
      }
    }else{
      digitalWrite(ACTUATOR_PIN, LOW);
    }

}

Edge createNewEdge(String serial, String ip, String name){
  Edge e;
  e.set(serial, ip, name);
  return e;
}

bool isStatusTime(){
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    return true;
  }
  return false;
}
