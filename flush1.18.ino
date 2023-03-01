#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>
#include <ArduinoJson.h>
#include <ESP8266httpUpdate.h>
#include <RCSwitch.h>

// AP 名称
const char *AP_NAME = "Esp-Flush";
const char *APP_VERSION = "1.19";
bool apStarted = false;
RCSwitch mySwitch = RCSwitch();
// DNS端口号
const byte DNS_PORT = 53;
// AP-IP地址
IPAddress apIP(192, 168, 4, 1);
// 创建dnsServer实例
DNSServer dnsServer;
// 创建WebServer
ESP8266WebServer server(80);
// 上次上报信号强度时间戳
unsigned long lastRSSIUpTs;
// MQTT客户端
WiFiClient espClient;
PubSubClient client(espClient);
char mqttRspTopic[20] = {0};

int startUpgrade = 0;
char upgradeUrl[256] = {0};

// 当前是否处于对码状态
int matchCodeState = 0;
int matchCodeRoute = 0;

// 自己插座板子
// int RELAY_IO = 13;
// int SWITCH_IO = 12;

// // 小葱板子
// int RELAY_IO = 14;      // 继电器IO
// int SWITCH_IO = 5;      // 物理开关IO
// int LIGHT_IO = 16;      // 指示灯IO
// int INVERTED_IO = 1;    // 反转指示灯
// int RELAY_INVERTED = 0; // 反转继电器

// 上次连接wifi时间
unsigned long lastConnectTs = 0;
// 上次控制时间
unsigned long lastCtlTs = millis();

void connectNewWifi(void);

struct switch_config_t
{
  int relay_io;             // 继电器IO
  int switch_io;            // 开关IO
  int light_io;             // 指示灯IO
  int state;                // 当前开关状态
  int enable;               // 启用状态
  unsigned long pressed_ts; // 上次按下时间
};

// 持久配置
struct config_t
{
  char stassid[32];   // 定义配网得到的WIFI名长度(最大32字节)
  char stapsw[64];    // 定义配网得到的WIFI密码长度(最大64字节)
  char device_id[32]; // 设备id
  char code1[32];     // 射频码1
  char code2[32];     // 射频码2
  char code3[32];     // 射频码3

  int relay_inverted; // 翻转继电器
  int light_inverted; // 翻转指示灯
  int remember;       // 记忆状态
  int type;           // 几路

  switch_config_t sw1; // 一路
  switch_config_t sw2; // 二路
  switch_config_t sw3; // 三路
};

// 声明定义内容
config_t config;

void on(switch_config_t *sc);
void off(switch_config_t *sc);
void loopCtrl(switch_config_t *sc);

// 保存配置
void saveConfig()
{
  EEPROM.begin(512); //向系统申请1024字节 ROM
  //开始写入
  uint8_t *p = (uint8_t *)(&config);
  for (int i = 0; i < sizeof(config); i++)
  {
    EEPROM.write(i, *(p + i)); //在闪存内模拟写入
  }
  EEPROM.end(); //执行写入ROM
}

// 加载配置
void loadConfig()
{
  EEPROM.begin(512);
  uint8_t *p = (uint8_t *)(&config);
  for (int i = 0; i < sizeof(config); i++)
  {
    *(p + i) = EEPROM.read(i);
  }
  EEPROM.end();
}

// 配网页面代码
static const char HTML[] PROGMEM = R"KEWL(
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <title>WIFI配网</title>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width, initial-scale=1.0'>
    <style>
        html,
        body,
        .box-cont {
            height: 100%;
        }

        .box-cont {
            display: flex;
            flex-direction: row;
            flex-wrap: nowrap;
            align-content: center;
            justify-content: center;
            /* align-items: center; */
        }

        .input-item {
            margin-top: 10px;
        }

        .input-item.button {
            width: 100%;
        }

        .input-item input {
            border: 1px #cbcbcb solid;
            padding: 5px;
        }

        .input-item.button input {
            width: 100%;
            height: 35px;
            background: #3599ea;
            color: white;
            font-family: 微软雅黑;
            border: none;
        }
        .sw2,.sw3{
            display: none;
        }
    </style>
</head>

<body>
    <div class="box-cont">
        <form name='input' action='/' method='POST'>
            <div style="text-align: center;">
                <image
                    src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAYAAACqaXHeAAAACXBIWXMAAAsTAAALEwEAmpwYAAAGv2lUWHRYTUw6Y29tLmFkb2JlLnhtcAAAAAAAPD94cGFja2V0IGJlZ2luPSLvu78iIGlkPSJXNU0wTXBDZWhpSHpyZVN6TlRjemtjOWQiPz4gPHg6eG1wbWV0YSB4bWxuczp4PSJhZG9iZTpuczptZXRhLyIgeDp4bXB0az0iQWRvYmUgWE1QIENvcmUgNy4wLWMwMDAgNzkuMjE3YmNhNiwgMjAyMS8wNi8xNC0xODoyODoxMSAgICAgICAgIj4gPHJkZjpSREYgeG1sbnM6cmRmPSJodHRwOi8vd3d3LnczLm9yZy8xOTk5LzAyLzIyLXJkZi1zeW50YXgtbnMjIj4gPHJkZjpEZXNjcmlwdGlvbiByZGY6YWJvdXQ9IiIgeG1sbnM6eG1wPSJodHRwOi8vbnMuYWRvYmUuY29tL3hhcC8xLjAvIiB4bWxuczp4bXBNTT0iaHR0cDovL25zLmFkb2JlLmNvbS94YXAvMS4wL21tLyIgeG1sbnM6c3RFdnQ9Imh0dHA6Ly9ucy5hZG9iZS5jb20veGFwLzEuMC9zVHlwZS9SZXNvdXJjZUV2ZW50IyIgeG1sbnM6ZGM9Imh0dHA6Ly9wdXJsLm9yZy9kYy9lbGVtZW50cy8xLjEvIiB4bWxuczpwaG90b3Nob3A9Imh0dHA6Ly9ucy5hZG9iZS5jb20vcGhvdG9zaG9wLzEuMC8iIHhtcDpDcmVhdG9yVG9vbD0iQWRvYmUgUGhvdG9zaG9wIDIyLjQgKFdpbmRvd3MpIiB4bXA6Q3JlYXRlRGF0ZT0iMjAyMS0xMi0yN1QxNjo1NjoyMCswODowMCIgeG1wOk1ldGFkYXRhRGF0ZT0iMjAyMS0xMi0zMVQyMDo1Mzo0MCswODowMCIgeG1wOk1vZGlmeURhdGU9IjIwMjEtMTItMzFUMjA6NTM6NDArMDg6MDAiIHhtcE1NOkluc3RhbmNlSUQ9InhtcC5paWQ6OTc1OWRmYTMtNzNlYy1mNDQ1LWJlM2YtYzFhODIwOWQ4NDYxIiB4bXBNTTpEb2N1bWVudElEPSJhZG9iZTpkb2NpZDpwaG90b3Nob3A6MDg1Nzg5NjQtY2I1Yi04ODRjLWE2YjEtOGFhZjk1NTRjMjMxIiB4bXBNTTpPcmlnaW5hbERvY3VtZW50SUQ9InhtcC5kaWQ6NjYwNDA3YzYtNTBjOC03MzQzLTg3NGItMjEzOGFlNjNjNjIxIiBkYzpmb3JtYXQ9ImltYWdlL3BuZyIgcGhvdG9zaG9wOkNvbG9yTW9kZT0iMyIgcGhvdG9zaG9wOklDQ1Byb2ZpbGU9InNSR0IgSUVDNjE5NjYtMi4xIj4gPHhtcE1NOkhpc3Rvcnk+IDxyZGY6U2VxPiA8cmRmOmxpIHN0RXZ0OmFjdGlvbj0iY3JlYXRlZCIgc3RFdnQ6aW5zdGFuY2VJRD0ieG1wLmlpZDo2NjA0MDdjNi01MGM4LTczNDMtODc0Yi0yMTM4YWU2M2M2MjEiIHN0RXZ0OndoZW49IjIwMjEtMTItMjdUMTY6NTY6MjArMDg6MDAiIHN0RXZ0OnNvZnR3YXJlQWdlbnQ9IkFkb2JlIFBob3Rvc2hvcCAyMi40IChXaW5kb3dzKSIvPiA8cmRmOmxpIHN0RXZ0OmFjdGlvbj0ic2F2ZWQiIHN0RXZ0Omluc3RhbmNlSUQ9InhtcC5paWQ6OTM4ODk5YmQtY2MwOC1hNjQzLWJlZWYtZmY3YzM1N2M3MmE3IiBzdEV2dDp3aGVuPSIyMDIxLTEyLTI3VDE2OjU2OjIwKzA4OjAwIiBzdEV2dDpzb2Z0d2FyZUFnZW50PSJBZG9iZSBQaG90b3Nob3AgMjIuNCAoV2luZG93cykiIHN0RXZ0OmNoYW5nZWQ9Ii8iLz4gPHJkZjpsaSBzdEV2dDphY3Rpb249InNhdmVkIiBzdEV2dDppbnN0YW5jZUlEPSJ4bXAuaWlkOjk3NTlkZmEzLTczZWMtZjQ0NS1iZTNmLWMxYTgyMDlkODQ2MSIgc3RFdnQ6d2hlbj0iMjAyMS0xMi0zMVQyMDo1Mzo0MCswODowMCIgc3RFdnQ6c29mdHdhcmVBZ2VudD0iQWRvYmUgUGhvdG9zaG9wIDIyLjQgKFdpbmRvd3MpIiBzdEV2dDpjaGFuZ2VkPSIvIi8+IDwvcmRmOlNlcT4gPC94bXBNTTpIaXN0b3J5PiA8L3JkZjpEZXNjcmlwdGlvbj4gPC9yZGY6UkRGPiA8L3g6eG1wbWV0YT4gPD94cGFja2V0IGVuZD0iciI/Pm09kV0AABXWSURB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">
                </image>
            </div>
            <div class="input-item">
                WIFI名称: <br>
                <input type='text' value="ZINGWIFI" name='ssid'>
            </div>
            <div class="input-item">
                WIFI密码:<br>
                <input type='text' value="1234567890" name='password'>
            </div>
            <div class="input-item">
                设备序列号:<br>
                <input type='text' value="123" name='device_id'>
            </div>
            <div class="input-item">
                产品:<br>
                <select class="product" name="product" onchange="selectChange()" style="
                width: 100%;
                height: 30px;
            ">
                    <option value="1">墙壁开关</option>
                    <option value="2">小葱插座</option>
                </select>
            </div>
            <div class="input-item">
                开关类型:<br>
                <select class="type" name="type" onchange="selectChange()" style="
                width: 100%;
                height: 30px;
            ">
                    <option value="1">一开</option>
                    <option value="2">二开</option>
                    <option value="3">三开</option>
                </select>
            </div>
            <div class="input-item">
                指示灯IO翻转:<br>
                <select class="light_inverted" name="light_inverted" style="
                width: 100%;
                height: 30px;
            ">
                    <option value="0">关</option>
                    <option value="1">开</option>
                </select>
            </div>
            <div class="input-item">
                继电器IO翻转:<br>
                <select class="relay_inverted" name="relay_inverted" style="
                width: 100%;
                height: 30px;
            ">
                    <option value="0">关</option>
                    <option value="1">开</option>
                </select>
            </div>
            <div class="input-item">
                记忆状态:<br>
                <select name="remember" style="
                width: 100%;
                height: 30px;
            ">
                    <option value="0">关</option>
                    <option value="1">开</option>
                </select>
            </div>
            <div class="input-item sw1">
                一开指示灯IO:<br>
                <input class="sw1-light" type='text' value="-1" name='sw1_light_io'>
            </div>
            <div class="input-item sw1">
                一开按键IO:<br>
                <input class="sw1-switch" type='text' value="4" name='sw1_switch_io'>
            </div>
            <div class="input-item sw1">
                一开继电器IO:<br>
                <input class="sw1-relay" type='text' value="12" name='sw1_relay_io'>
            </div>
            <div class="input-item sw2">
                二开指示灯IO:<br>
                <input class="sw2-light" type='text' value="-1" name='sw2_light_io'>
            </div>
            <div class="input-item sw2">
                二开按键IO:<br>
                <input class="sw2-switch" type='text' value="" name='sw2_switch_io'>
            </div>
            <div class="input-item sw2">
                二开继电器IO:<br>
                <input class="sw2-relay" type='text' value="" name='sw2_relay_io'>
            </div>
            <div class="input-item sw3">
                三开指示灯IO:<br>
                <input class="sw3-light" type='text' value="-1" name='sw3_light_io'>
            </div>
            <div class="input-item sw3">
                三开按键IO:<br>
                <input class="sw3-switch" type='text' value="" name='sw3_switch_io'>
            </div>
            <div class="input-item sw3">
                三开继电器IO:<br>
                <input class="sw3-relay" type='text' value="" name='sw3_relay_io'>
            </div>
            <div class="input-item button">
                <input type='submit' value='保存'>
            </div>
        </form>
    </div>
    <script>
        function selectChange() {
            let product;
            let type;
            Array.from(document.getElementsByClassName("product")).forEach(v => {
                product = v.value
            });
            Array.from(document.getElementsByClassName("type")).forEach(v => {
                type = v.value
            });
            if(product == 2){
                Array.from(document.getElementsByClassName("light_inverted")).forEach(v => {
                    v.value = '1'
                });
                Array.from(document.getElementsByClassName("sw1-light")).forEach(v => {
                    v.value = '16'
                });
                Array.from(document.getElementsByClassName("sw1-switch")).forEach(v => {
                    v.value = '5'
                });

                Array.from(document.getElementsByClassName("sw1-relay")).forEach(v => {
                    v.value = '14'
                });
            }else{
                Array.from(document.getElementsByClassName("light_inverted")).forEach(v => {
                    v.value = '0'
                });
                Array.from(document.getElementsByClassName("sw1-light")).forEach(v => {
                    v.value = '-1'
                });
                Array.from(document.getElementsByClassName("sw1-switch")).forEach(v => {
                    v.value = type == 1 ? '4' : '0'
                    console.log(v)
                });
                Array.from(document.getElementsByClassName("sw1-relay")).forEach(v => {
                    v.value = type == 1 ? '12' : '13'
                    console.log(v)
                });
            }
            console.log(product)
            Array.from(document.getElementsByClassName("sw2")).forEach(v => {
                v.style.display = type == 1 ? 'none' : 'block'
            });
            Array.from(document.getElementsByClassName("sw3")).forEach(v => {
                v.style.display = (type == 1 || type == 2) ? 'none' : 'block'
            });
            Array.from(document.getElementsByClassName("sw2-switch")).forEach(v => {
                v.value = type == 1 ? '' : type == 2 ?  '2' : '4'
            });
            Array.from(document.getElementsByClassName("sw2-relay")).forEach(v => {
                v.value = type == 1 ? '' : type == 2 ?  '14' : '12'
            });
            Array.from(document.getElementsByClassName("sw3-switch")).forEach(v => {
                v.value = type == 3 ? '2' : ''
            });
            Array.from(document.getElementsByClassName("sw3-relay")).forEach(v => {
                v.value = type == 3 ? '14' : ''
            });
        }
    </script>
</body>
</html>
)KEWL";

// 访问主页回调函数
void handleRoot()
{
  server.send(200, "text/html", HTML);
}

void handleRootPost()
{ // Post回调函数
  Serial.println("handleRootPost");
  if (server.hasArg("ssid"))
  { // 判断是否有账号参数
    Serial.print("got ssid:");
    strcpy(config.stassid, server.arg("ssid").c_str()); // 将账号参数拷贝到my_ssid中
    Serial.println(config.stassid);
  }
  else
  { // 没有参数
    Serial.println("error, not found ssid");
    server.send(200, "text/html", "<meta charset='UTF-8'>error, not found ssid"); // 返回错误页面
    return;
  }
  if (server.hasArg("password"))
  {
    Serial.print("got password:");
    strcpy(config.stapsw, server.arg("password").c_str());
    Serial.println(config.stapsw);
  }
  else
  {
    Serial.println("error, not found password");
    server.send(200, "text/html", "<meta charset='UTF-8'>error, not found password");
    return;
  }

  if (server.hasArg("device_id"))
  {
    Serial.print("got device_id:");
    strcpy(config.device_id, server.arg("device_id").c_str());
    Serial.println(config.device_id);
  }
  else
  {
    Serial.println("error, not found device_id");
    server.send(200, "text/html", "<meta charset='UTF-8'>error, not found device_id");
    return;
  }

  Serial.print("got light_inverted:");
  config.light_inverted = (int)(server.arg("light_inverted").toInt());
  Serial.println(config.light_inverted);

  Serial.print("got relay_inverted:");
  config.relay_inverted = (int)(server.arg("relay_inverted").toInt());
  Serial.println(config.relay_inverted);

  Serial.print("got remember:");
  config.remember = (int)(server.arg("remember").toInt());
  Serial.println(config.remember);

  int type = (int)(server.arg("type").toInt());
  config.type = type;

  config.sw1.enable = 1;
  config.sw2.enable = (type >= 2) ? 1 : 0;
  config.sw3.enable = (type == 3) ? 1 : 0;

  Serial.print("got sw1_light_io:");
  config.sw1.light_io = (int)(server.arg("sw1_light_io").toInt());
  Serial.println(config.sw1.light_io);

  Serial.print("got sw1_switch_io:");
  config.sw1.switch_io = (int)(server.arg("sw1_switch_io").toInt());
  Serial.println(config.sw1.switch_io);

  Serial.print("got sw1_relay_io:");
  config.sw1.relay_io = (int)(server.arg("sw1_relay_io").toInt());
  Serial.println(config.sw1.relay_io);

  if (type >= 2)
  {
    Serial.print("got sw2_light_io:");
    config.sw2.light_io = (int)(server.arg("sw2_light_io").toInt());
    Serial.println(config.sw2.light_io);

    Serial.print("got sw2_switch_io:");
    config.sw2.switch_io = (int)(server.arg("sw2_switch_io").toInt());
    Serial.println(config.sw2.switch_io);

    Serial.print("got sw2_relay_io:");
    config.sw2.relay_io = (int)(server.arg("sw2_relay_io").toInt());
    Serial.println(config.sw2.relay_io);
  }

  if (type == 3)
  {
    Serial.print("got sw3_light_io:");
    config.sw3.light_io = (int)(server.arg("sw3_light_io").toInt());
    Serial.println(config.sw3.light_io);

    Serial.print("got sw3_switch_io:");
    config.sw3.switch_io = (int)(server.arg("sw3_switch_io").toInt());
    Serial.println(config.sw3.switch_io);

    Serial.print("got sw3_relay_io:");
    config.sw3.relay_io = (int)(server.arg("sw3_relay_io").toInt());
    Serial.println(config.sw3.relay_io);
  }

  server.send(200, "text/html", "<meta charset='UTF-8'>保存成功"); // 返回保存成功页面
  saveConfig();
  delay(2000);
  // 重启联网
  ESP.restart();
}

void initSoftAP(void)
{ // 初始化AP模式
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  if (WiFi.softAP(AP_NAME))
  {
    Serial.println("esp32 SoftAP is right");
  }
}

void initWebServer(void)
{                                            // 初始化WebServer
  server.on("/", HTTP_GET, handleRoot);      // 设置主页回调函数
  server.onNotFound(handleRoot);             // 设置无法响应的http请求的回调函数
  server.on("/", HTTP_POST, handleRootPost); // 设置Post请求回调函数
  server.begin();                            // 启动WebServer
  Serial.println("WebServer started!");
}

void initDNS(void)
{ //初始化DNS服务器
  if (dnsServer.start(DNS_PORT, "*", apIP))
  { //判断将所有地址映射到esp32的ip上是否成功
    Serial.println("start dnsserver success.");
  }
  else
    Serial.println("start dnsserver failed.");
}

void callback(char *topic, byte *payload, unsigned int length)
{
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  // 回复收到
  strcpy(mqttRspTopic, "");
  strcat(mqttRspTopic, "/rsp/");
  memcpy(mqttRspTopic + 5, topic + 5, strlen(topic) - 5);
  Serial.println(mqttRspTopic);

  char pay_str[256];
  memcpy(pay_str, payload, length);
  pay_str[length] = '\0';

  Serial.println(pay_str);

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, String(pay_str));
  JsonObject obj = doc.as<JsonObject>();

  String cmd = obj[String("cmd")];
  Serial.println("cmd = " + cmd);

  if (cmd == "on") // 一开默认调用
  {
    Serial.println("exe on");
    on(&config.sw1);
  }
  else if (cmd == "off") // 一开默认调用
  {
    off(&config.sw1);
    Serial.println("exe off");
  }
  else if (cmd == "upgrade") // 固件升级
  {
    if (obj[String("param")])
    {
      String url = (obj[String("param")][String("url")]) ? (obj[String("param")][String("url")]) : String("");
      if (url != "")
      {
        sprintf(upgradeUrl, "%s", url.c_str());
        startUpgrade = true;
      }
    }
  }
  else if (cmd == "b1") // 供巴法云调用
  {
    if (obj[String("param")] == "on")
    {
      on(&config.sw1);
    }
    else if (obj[String("param")] == "off")
    {
      off(&config.sw1);
    }
  }
  else if (cmd == "b2") // 供巴法云调用
  {
    if (obj[String("param")] == "on")
    {
      on(&config.sw2);
    }
    else if (obj[String("param")] == "off")
    {
      off(&config.sw2);
    }
  }
  else if (cmd == "b3") // 供巴法云调用
  {
    if (obj[String("param")] == "on")
    {
      on(&config.sw3);
    }
    else if (obj[String("param")] == "off")
    {
      off(&config.sw3);
    }
  }
  else if (cmd == "sw1")
  {
    loopCtrl(&config.sw1);
  }
  else if (cmd == "sw2")
  {
    loopCtrl(&config.sw2);
  }
  else if (cmd == "sw3")
  {
    loopCtrl(&config.sw3);
  }
  else if (cmd == "matchCode") // 遥控器对码
  {
    if (obj[String("param")])
    {
      int route = obj[String("param")][String("route")];
      matchCodeRoute = route;
      int clear = obj[String("param")][String("clear")];
      if (clear == 1)
      {
        switch (route)
        {
        case 1:
        {
          strcpy(config.code1, "");
          saveConfig();
          break;
        }
        case 2:
        {
          strcpy(config.code2, "");
          saveConfig();
          break;
        }
        case 3:
        {
          strcpy(config.code3, "");
          saveConfig();
          break;
        }
        default:
          break;
        }
        client.publish(mqttRspTopic, "{\"ok\":true;}");
      }
      else
      {
        matchCodeState = 1;
      }
    }
    return;
  }
  else if (cmd == "f")
  {
    memset(&config, 0, sizeof(config_t));
    saveConfig();
    ESP.restart();
  }

  client.publish(mqttRspTopic, "{\"ok\":true;}");
}

void connectNewWifi(void)
{
  lastConnectTs = millis();
  WiFi.mode(WIFI_STA); // 切换为STA模式
  // 关闭自动连接 防止重置后自动连接wifi
  WiFi.setAutoConnect(false);   // 关闭自动连接
  WiFi.setAutoReconnect(false); // 关闭自动重连
  if (config.type != 0)
  {
    WiFi.setAutoConnect(true);   // 设置自动连接
    WiFi.setAutoReconnect(true); // 设置自动重连
    WiFi.begin(config.stassid, config.stapsw);
    Serial.print("Connect to wifi");
    int count = 0;
    while (WiFi.status() != WL_CONNECTED)
    {
      // 尝试连接10秒
      if (count > 20)
      {
        break;
      }
      Serial.print(".");
      count++;
      delay(500);
    }
  }

  Serial.println("");
  if (WiFi.status() == WL_CONNECTED)
  { // 如果连接上 就输出IP信息 防止未连接上break后会误输出
    Serial.println("WIFI Connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    dnsServer.stop();
    server.stop();
    apStarted = false;
  }
  else
  {
    initSoftAP();
    initWebServer();
    initDNS();
    apStarted = true;
  }
}

void mqttReconnect()
{
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    if (client.connect(config.device_id))
    {
      Serial.println("MQTT connected");
      // 连接后发送当前开关状态
      char body[50] = {0};
      sprintf(body, "{\"sw1State\":%d}", config.sw1.state);
      client.publish("/attr", body);

      if (config.type >= 2)
      {
        char body[50] = {0};
        sprintf(body, "{\"sw2State\":%d}", config.sw2.state);
        client.publish("/attr", body);
      }

      if (config.type == 3)
      {
        char body[50] = {0};
        sprintf(body, "{\"sw3State\":%d}", config.sw3.state);
        client.publish("/attr", body);
      }

      // 发送当前APP版本
      char versionJson[100] = {0};
      sprintf(versionJson, "{\"version\":\"%s\"}", APP_VERSION);
      client.publish("/attr", versionJson);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void on(switch_config_t *sc)
{
  lastCtlTs = millis();
  Serial.println("on");
  sc->state = 1;
  digitalWrite(sc->relay_io, config.relay_inverted ? LOW : HIGH);
  if (sc->light_io != -1)
  {
    Serial.println("on ctrl led");
    digitalWrite(sc->light_io, config.light_inverted ? LOW : HIGH);
  }
  char body[50] = {0};
  sprintf(body, "{\"%s\":1}", sc == &config.sw1 ? "sw1State" : (sc == &config.sw2 ? "sw2State" : "sw3State"));
  client.publish("/attr", body);
  saveConfig();
}

void off(switch_config_t *sc)
{
  lastCtlTs = millis();
  Serial.println("off");
  sc->state = 0;
  digitalWrite(sc->relay_io, config.relay_inverted ? HIGH : LOW);
  if (sc->light_io != -1)
  {
    Serial.println("off ctrl led");
    digitalWrite(sc->light_io, config.light_inverted ? HIGH : LOW);
  }
  char body[50] = {0};
  sprintf(body, "{\"%s\":0}", sc == &config.sw1 ? "sw1State" : (sc == &config.sw2 ? "sw2State" : "sw3State"));
  client.publish("/attr", body);
  saveConfig();
}

void loopCtrl(switch_config_t *sc)
{
  int state = digitalRead(sc->relay_io);
  if (config.relay_inverted ? !state : state)
  {
    off(sc);
  }
  else
  {
    on(sc);
  }
}

void switch_interrupt(switch_config_t *sc)
{
  unsigned long now = millis();
  int swState = digitalRead(sc->switch_io);
  if (!swState)
  {
    sc->pressed_ts = now;
    return;
  }
  else
  {
    if (now - sc->pressed_ts > 5000)
    {
      // 重置配网
      memset(&config, 0, sizeof(config_t));
      saveConfig();
      ESP.restart();
      return;
    }
    else
    {
      if (millis() - lastCtlTs > 500)
      {
        Serial.println("switch_interrupt");
        int state = digitalRead(sc->relay_io);
        if (config.relay_inverted ? !state : state)
        {
          off(sc);
        }
        else
        {
          on(sc);
        }
      }
    }
  }
}

// 一路中断
IRAM_ATTR void sw1_interrupt()
{
  switch_interrupt(&config.sw1);
}

// 二路中断
IRAM_ATTR void sw2_interrupt()
{
  switch_interrupt(&config.sw2);
}

// 三路中断
IRAM_ATTR void sw3_interrupt()
{
  switch_interrupt(&config.sw3);
}

void update_started()
{
  Serial.println("CALLBACK:  HTTP update process started");
}

void update_finished()
{
  Serial.println("CALLBACK:  HTTP update process finished");
}

void update_progress(int cur, int total)
{
  Serial.printf("CALLBACK:  HTTP update process at %d of %d bytes...\n", cur, total);
}

void update_error(int err)
{
  Serial.printf("CALLBACK:  HTTP update fatal error code %d\n", err);
}

void sw_init(switch_config_t *sc, void (*interrupt)(void))
{
  if (sc->enable)
  {
    pinMode(sc->relay_io, OUTPUT);                                                                                                         // 设置继电器模式
    digitalWrite(sc->relay_io, config.remember ? (config.relay_inverted ? !sc->state : sc->state) : (config.relay_inverted ? HIGH : LOW)); // 恢复继电器状态
    if (sc->switch_io != -1)
    {
      pinMode(sc->switch_io, INPUT_PULLUP);
      attachInterrupt(sc->switch_io, interrupt, CHANGE);
    }
    if (sc->light_io != -1)
    {
      pinMode(sc->light_io, OUTPUT);
      digitalWrite(sc->light_io, config.remember ? (config.light_inverted ? !sc->state : sc->state) : (config.light_inverted ? HIGH : LOW)); // 恢复指示灯状态
    }
  }
}

void setup()
{
  Serial.begin(115200);
  WiFi.hostname("Smart-esp");

  mySwitch.enableReceive(5);

  // 加载配置
  loadConfig();
  sw_init(&config.sw1, sw1_interrupt);
  sw_init(&config.sw2, sw2_interrupt);
  sw_init(&config.sw3, sw2_interrupt);

  // 尝试连接wifi
  connectNewWifi();

  // 设置MQTT服务器
  client.setServer("eiot.link", 1883);
  client.setCallback(callback);
}

// 上次收到433信号时间戳
unsigned long last433RecTs = 0;

void loop()
{
  // 处理433 信号
  unsigned long now = millis();
  if (mySwitch.available())
  {
    unsigned long code = mySwitch.getReceivedValue();
    if ((now - last433RecTs) > 500)
    {
      if (matchCodeState)
      {
        Serial.println("start match 433 code");

        switch (matchCodeRoute)
        {
        case 1:
        {
          sprintf(config.code1, "%ld", code);
          saveConfig();
          break;
        }
        case 2:
        {
          sprintf(config.code2, "%ld", code);
          saveConfig();
          break;
        }
        case 3:
        {
          sprintf(config.code3, "%ld", code);
          saveConfig();
          break;
        }
        default:
          break;
        }
        matchCodeState = 0;
        // 回复mqtt
        Serial.println("mqtt rsp topic");
        Serial.println(mqttRspTopic);
        client.publish(mqttRspTopic, "{\"ok\":true;}");
      }
      else
      {
        Serial.println("start match contrl code");
        if (code == atol(config.code1))
        {
          int state = digitalRead(config.sw1.relay_io);
          if (config.relay_inverted ? !state : state)
          {
            off(&config.sw1);
          }
          else
          {
            on(&config.sw1);
          }
        }
        else if (code == atol(config.code2))
        {
          int state = digitalRead(config.sw2.relay_io);
          if (config.relay_inverted ? !state : state)
          {
            off(&config.sw2);
          }
          else
          {
            on(&config.sw2);
          }
        }
        else if (code == atol(config.code3))
        {
          int state = digitalRead(config.sw3.relay_io);
          if (config.relay_inverted ? !state : state)
          {
            off(&config.sw3);
          }
          else
          {
            on(&config.sw3);
          }
        }
      }
      last433RecTs = now;
    }
    Serial.print("Received ");
    Serial.println(code);
    // Serial.print(" / ");
    // Serial.print(mySwitch.getReceivedBitlength());
    // Serial.print("bit ");
    // Serial.print("Protocol: ");
    // Serial.println(mySwitch.getReceivedProtocol());

    mySwitch.resetAvailable();
  }

  if (apStarted)
  {
    server.handleClient();
    dnsServer.processNextRequest();

    // 判断ap等待时间 ap开启3分钟尝试连接wifi 解决断电后路由器启动慢无限ap模式问题
    if ((millis() - lastConnectTs) > (1000 * 60 * 3))
    {
      if (WiFi.status() != WL_CONNECTED)
      {
        connectNewWifi();
      }
    }
  }

  if (WiFi.status() == WL_CONNECTED)
  {
    // 判断是否需要在线升级
    if (startUpgrade)
    {
      WiFiClient upgradeClient;
      ESPhttpUpdate.onStart(update_started);
      ESPhttpUpdate.onEnd(update_finished);
      ESPhttpUpdate.onProgress(update_progress);
      ESPhttpUpdate.onError(update_error);
      Serial.println(upgradeUrl);
      t_httpUpdate_return ret = ESPhttpUpdate.update(upgradeClient, upgradeUrl);
      switch (ret)
      {
      case HTTP_UPDATE_FAILED:
        Serial.printf("HTTP_UPDATE_FAILD Error (%d): %s\n", ESPhttpUpdate.getLastError(), ESPhttpUpdate.getLastErrorString().c_str());
        break;

      case HTTP_UPDATE_NO_UPDATES:
        Serial.println("HTTP_UPDATE_NO_UPDATES");
        break;

      case HTTP_UPDATE_OK:
        Serial.println("HTTP_UPDATE_OK");
        break;
      }
      startUpgrade = false;
    }

    if (!client.connected())
    {
      mqttReconnect();
    }
    else
    {
      // MQTT连接成功后 每五秒上报一下wifi状态
      if (now - lastRSSIUpTs > 5000)
      {
        lastRSSIUpTs = now;
        int8_t rssi = WiFi.RSSI();
        char payload[50] = {0};
        sprintf(payload, "{\"rssi\":%d}", rssi);
        client.publish("/attr", payload);
      }
    }
    client.loop();
  }
}