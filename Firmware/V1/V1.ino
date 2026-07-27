#include "Arduino.h"
#include "PCF8575.h"              // PCF8575 I2C 扩展芯片库
#include <Wire.h>                 // Wire I2C 通信库
#include <TM1637Display.h>        // TM1637 数码管驱动库

// =========================================================================
// 1. 系统模式与状态定义
// =========================================================================
enum Mode { MODE_IDLE, MODE_TRANSFER, MODE_MANUAL_ADD, MODE_MANUAL_SUB };
Mode currentMode = MODE_IDLE; // 当前系统运行模式

enum AudioState { AUDIO_STATE_OFF, AUDIO_STATE_NORMAL_BGM, AUDIO_STATE_RIICHI_VOICE, AUDIO_STATE_RIICHI_BGM };
AudioState currentAudioState = AUDIO_STATE_OFF; // 音频播放状态机追踪

// =========================================================================
// 2. 引脚与硬件地址定义
// =========================================================================
#define I2C_SDA 13                // I2C 数据引脚
#define I2C_SCL 14                // I2C 时钟引脚
#define PCF8575_ADDRESS 0x20      // PCF8575 芯片 I2C 设备地址

#define FPSERIAL Serial2          // MP3 音频芯片串口绑定
#define TM_CLK 22                 // 4 路数码管共享的 CLK 引脚
const uint8_t tmDIO[4] = {21, 19, 18, 5}; // 4 路数码管独立的 DIO 引脚

// 三麻/四麻物理模式选择开关引脚（ESP32 原生 GPIO）
#define PIN_3PLAYER 2
#define PIN_4PLAYER 15

// 4 路霍尔传感器引脚定义（东、南、西、北）
const uint8_t hallPins[4] = {33, 25, 26, 27};

// 霍尔传感器状态记录（用于瞬间下降沿边沿触发检测）
bool lastHallState[4] = {false, false, false, false};

// 局内立直锁定状态：true 代表本局已立直，锁死不再重复扣分
bool hasRiichi[4] = {false, false, false, false};

// =========================================================================
// 3. 日本麻将核心算法变量
// =========================================================================
int currentDealer = 0;               // 核心指针：当前物理东家/庄家 (0->P1, 1->P2, 2->P3, 3->P4)
int renchanCounter = 0;              // 连庄数 / 本场棒数
bool isFirstHand = true;             // 是否为开机后的第一局

unsigned long diceDisplayTimer = 0;  // 骰子点数显示定时器
int lastDiceResult = 0;              // 最近一次随机掷骰结果 (2-12)
int targetPlayerIdx = -1;            // 骰子开门指向的目标摸牌玩家索引

// 3麻/4麻运行控制变量
bool is3PlayerMode = false;          // 是否为三人麻将模式
bool isPlayerPresent[4] = {true, true, true, true}; // 控制 4 个方位的参与有效性

// 动态显示与超时保护守卫变量
unsigned long lastBlinkTime = 0;     // 得分卡片与转账闪烁定时器
bool blinkState = true;              // 闪烁状态位
unsigned long inputTimeoutTimer = 0;  // 键盘输入 5 秒超时定时器

// 全局音量与音频状态变量
int systemVolume = 30;               // 系统主音量 (0-30)
int activeRiichiTrack = -1;          // 当前触发立直的玩家座位索引
bool isRiichiBGMActive = false;      // 处刑曲播放激活标志
unsigned long audioRestoreTimer = 0;  // 立直宣告人声过渡到处刑曲的时钟

bool isGameActive = false;           // 防误触核心全局游戏激活标志锁

// =========================================================================
// 4. 外设实例与数码管段码定义
// =========================================================================
PCF8575 pcf8575(PCF8575_ADDRESS);
TM1637Display displays[4] = {
  TM1637Display(TM_CLK, tmDIO[0]), 
  TM1637Display(TM_CLK, tmDIO[1]), 
  TM1637Display(TM_CLK, tmDIO[2]), 
  TM1637Display(TM_CLK, tmDIO[3])  
};

// 数码管特殊字符与数字段码 (TM1637 编码)
const uint8_t SEG_BLANK = 0x00;      // 熄灭/空白
const uint8_t SEG_MINUS = 0x40;      // 负号 "-"
const uint8_t SEG_NUMBER[] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F // 0~9 数字段码
};

// =========================================================================
// 5. 计分系统与历史快照存储
// =========================================================================
long scores[4] = {25000, 25000, 25000, 25000}; // 4 家实时点数
int totalsum = 100000;                        // 总分（4人10万点，3人10.5万点）
int ranks[4] = {1, 1, 1, 1};                  // 4 家实时顺位排名

int riichiPool = 0;                            // 场面上累积的立直棒存量 (每根 1000 点)
bool isRiichi[4] = {false, false, false, false};

#define MAX_HANDS 32                          // 最大历史记录局数
long scoreHistory[MAX_HANDS][4];              // 局末分数快照历史表
int dealerHistory[MAX_HANDS];                 // 局末庄家索引历史表
int totalHandsRecorded = 0;                   // 已记录的总局数计数器

bool isLoserSelected[4] = {false, false, false, false}; // 转账模式下的输家点选状态
String inputBuffer = "";                      // 键盘输入数字缓冲区

// =========================================================================
// 6. 物理硬件映射 (键盘矩阵 / 骰子按键 / LED灯)
// =========================================================================
// 4x4 键盘矩阵映射表 ('+' 键支持 2 秒内复按切换加减分，原 '-' 键留空)
char keyMap[4][4] = {
  {'E', '3', '2', '1'}, 
  {'S', '6', '5', '4'}, 
  {'W', '9', '8', '7'}, 
  {'N', '\0', '0', '+'}   
};

// PCF8575 引脚绑定：按键信号输入与庄家指示灯控制
const uint8_t btnPins[4] = {14, 12, 10, 8};  // 玩家大按键输入脚
const uint8_t ledPins[4] = {15, 13, 11, 9};  // 庄家指示灯输出脚
bool lastBtnState[4] = {false, false, false, false}; // 大按键电平守卫

// =========================================================================
// 7. 函数前置声明
// =========================================================================
char scanKeypad();
void handleKey(char key);
int getPlayerIndex(char key);
String getPlayerName(int p);
void calculateRanks();
void updateSingleDisplay(int playerIdx);
void updateAllSystem();
void scanDiceButtons();
void scanHallSensors();
void resetAllRiichi(); 
int getTargetPlayerByDice(int dice); 
void blinkWinner(int winnerIdx); 
void clearTransferBuffer(); 
void updateOyaLeds(); 
String getModeName(Mode mode);
void hardResetMP3ToNormal(); 
void addLog(String msg); 

// =========================================================================
// 8. MH2024K 音频芯片 HEX 串口底层驱动引擎
// =========================================================================
void sendMP3Command(uint8_t cmd, uint8_t para1, uint8_t para2) {
  uint8_t msg[10];
  msg[0] = 0x7E; // 帧头
  msg[1] = 0xFF; // 版本
  msg[2] = 0x06; // 长度
  msg[3] = cmd;  // CMD 指令
  msg[4] = 0x00; // 是否反馈 (0x00=否)
  msg[5] = para1;// 参数1 (文件夹号)
  msg[6] = para2;// 参数2 (曲目号)
  
  uint16_t sum = msg[1] + msg[2] + msg[3] + msg[4] + msg[5] + msg[6];
  uint16_t checksum = ~sum + 1; // 动态计算校验和
  
  msg[7] = (uint8_t)(checksum >> 8);   
  msg[8] = (uint8_t)(checksum & 0xFF); 
  msg[9] = 0xEF; // 帧尾

  FPSERIAL.write(msg, 10);
  delay(30); 
}

void playFileInFolder(uint8_t folder, uint8_t fileIdx) {
  sendMP3Command(0x0F, folder, fileIdx);
}

void loopCurrentTrack() {
  sendMP3Command(0x19, 0x00, 0x00); // 0x19 帧实现当前曲目单曲循环
}

// 全局可控音频轨道配置
uint8_t playerVoiceTracks[4] = {1, 3, 4, 7}; // 4家立直人声 (文件夹2)
uint8_t playerBGMTracks[4]   = {1, 2, 3, 4}; // 4家处刑曲 (文件夹3)
uint8_t defaultBGMTrack       = 2;            // 常规对局BGM (文件夹1)

// 冲阻清洗与常规 BGM 重置引擎
void hardResetMP3ToNormal() {
  sendMP3Command(0x16, 0x00, 0x00); delay(50);  // 停止当前播放
  playFileInFolder(1, defaultBGMTrack); delay(120); // 播放文件夹1的对局BGM
  loopCurrentTrack();               
  currentAudioState = AUDIO_STATE_NORMAL_BGM;
}

// =========================================================================
// 9. 网页服务模块包含 (需放在所有全局变量声明之下)
// =========================================================================
#include "web_server.h" 
WebServer server(80);   

// =========================================================================
// 10. ESP32 系统初始化函数 (setup)
// =========================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================================");
  Serial.println("    键盘&BGM&计分&骰子&霍尔立直系统固件（全网联网精炼版）");
  Serial.println("========================================================");

  // 物理模式开关读取 (已固定为四人麻将模式)
  pinMode(PIN_3PLAYER, INPUT_PULLDOWN);
  pinMode(PIN_4PLAYER, INPUT_PULLDOWN);
  delay(50);

  is3PlayerMode = true;
  totalsum = 105000;
  for(int i = 0; i < 4; i++) {
    scores[i] = (i < 3) ? 35000 : 0;
    isPlayerPresent[i] = (i < 3) ? true : false;
  }
  Serial.println("【🀄 开机模式】-> 已固定为：三人麻将模式");

  // 数码管显示屏亮度配置
  for(int i = 0; i < 4; i++) {
    displays[i].setBrightness(4, true); 
  }

  // 音频串口与 I2C 芯片扩展器初始化
  FPSERIAL.begin(9600, SERIAL_8N1, 16, 17); 
  delay(100);
  
  sendMP3Command(0x06, 0x00, systemVolume); 
  delay(50);
  sendMP3Command(0x0E, 0x00, 0x00); 

  Wire.begin(I2C_SDA, I2C_SCL); 
  if (pcf8575.begin(0xFFFF)) { 
    Serial.println("【⌨️ 芯片配置】-> PCF8575 扩展芯片初始化成功！");
    // 💥【核心硬件冲突修复】因为 pcf8575.begin() 底层会调用 Wire.begin() 误将 I2C 重新绑定回默认的 21/22 引脚，
    // 从而锁死共享同一引脚的东/南数码管（21/22）并导致指示灯控制信号出错。
    // 因此在初始化 PCF8575 成功后，必须立即重新调用 Wire.begin 将 I2C 重定向回 13/14 引脚，释放 21/22 供数码管驱动！
    Wire.begin(I2C_SDA, I2C_SCL);
  } else {
    Serial.println("【❌ 核心错误】-> 未找到 PCF8575 芯片，系统挂起！");
    while(1); 
  }

  // 霍尔传感器与乱数种子配置
  for(int i = 0; i < 4; i++) {
    pinMode(hallPins[i], INPUT_PULLUP);              
    lastHallState[i] = (digitalRead(hallPins[i]) == LOW); 
    hasRiichi[i] = false;
  }

  randomSeed(analogRead(34)); 
  currentDealer = 0;
  renchanCounter = 0;
  isFirstHand = true;
  
  updateOyaLeds(); 
  updateAllSystem();

  // 记录第 0 局初始分快照
  for(int i=0; i<4; i++) scoreHistory[0][i] = scores[i];
  dealerHistory[0] = currentDealer;
  totalHandsRecorded = 1; 

  // 硬件电平初态预读
  for (int i = 0; i < 4; i++) {
    pcf8575.write(btnPins[i], 1); 
  }
  delay(100); 
  for (int i = 0; i < 4; i++) {
    lastBtnState[i] = (pcf8575.read(btnPins[i]) == 0); 
  }

  initWiFiAndWeb(); 
}

// =========================================================================
// 11. 主循环与轮询逻辑 (loop)
// =========================================================================
void loop() {
  updateOyaLeds();

  // 音频状态机：立直宣告真人语音结束 2.5s 后接入处刑曲单曲循环
  if (audioRestoreTimer != 0) {
    if (millis() - audioRestoreTimer >= 2500) { 
      audioRestoreTimer = 0; 
      isRiichiBGMActive = true; 
      currentAudioState = AUDIO_STATE_RIICHI_BGM;
      
      uint8_t targetBGM = playerBGMTracks[activeRiichiTrack];
      playFileInFolder(3, targetBGM); 
      delay(50);
      loopCurrentTrack(); 
    }
  }

  // 5秒键盘输入超时超时守护拦截
  if (currentMode != MODE_IDLE) {
    if (millis() - inputTimeoutTimer >= 5000) {
      clearTransferBuffer(); 
      currentMode = MODE_IDLE;
      updateAllSystem();
    }
  }

  // 转账模式下的得失卡片动态闪烁控制
  if (millis() - lastBlinkTime >= 250) {
    lastBlinkTime = millis();
    blinkState = !blinkState;
    if (currentMode == MODE_TRANSFER && inputBuffer.length() == 0 && diceDisplayTimer == 0) {
      for(int i=0; i<4; i++) {
        if(isLoserSelected[i]) updateSingleDisplay(i); 
      }
    }
  }

  // 掷骰点数数码管暂留计时器 (2秒)
  if (diceDisplayTimer != 0) {
    if (millis() - diceDisplayTimer >= 2000) {
      diceDisplayTimer = 0;
      updateAllSystem();
    }
  }

  // 物理键盘矩阵扫描与事件响应
  char pressedKey = scanKeypad(); 
  if (pressedKey != '\0') {
    handleKey(pressedKey);
    delay(200); 
  }
  
  scanDiceButtons();
  scanHallSensors();
  handleWiFiClient(); 

  // 历史打点数据快照自动哨兵
  static int lastRenchan = 0;       
  static int lastDealer = 0;        
  bool roundEnded = false;           

  if (renchanCounter != lastRenchan) { roundEnded = true; lastRenchan = renchanCounter; }
  if (currentDealer != lastDealer)   { roundEnded = true; lastDealer = currentDealer; }

  if (roundEnded) {
    if (totalHandsRecorded < MAX_HANDS) {
      for (int i = 0; i < 4; i++) {
        scoreHistory[totalHandsRecorded][i] = scores[i]; 
      }
      dealerHistory[totalHandsRecorded] = currentDealer;  
      totalHandsRecorded++;
    }
    lastRenchan = renchanCounter;
    lastDealer = currentDealer;
  }
  delay(10);
}

// =========================================================================
// 12. 硬件控制与物理按键扫描逻辑
// =========================================================================
void updateOyaLeds() {
  for (int i = 0; i < 4; i++) {
    // 庄家点亮指示灯 (写 0)，闲家熄灭 (写 1)
    pcf8575.write(ledPins[i], (i == currentDealer && isPlayerPresent[i]) ? 0 : 1);
  }
}

void scanDiceButtons() {
  if (millis() < 2000) {
    for (int i = 0; i < 4; i++) {
      pcf8575.write(btnPins[i], 1);
      lastBtnState[i] = false;
    }
    return;
  }

  int nextDealerCandidate = (currentDealer + 1) % 4; 
  while (!isPlayerPresent[nextDealerCandidate]) {
    nextDealerCandidate = (nextDealerCandidate + 1) % 4;
  }

  for(int i = 0; i < 4; i++) pcf8575.write(btnPins[i], 1);
  delayMicroseconds(10); 

  for (int i = 0; i < 4; i++) {
    if (!isPlayerPresent[i]) continue; 

    int rawReading = pcf8575.read(btnPins[i]);
    bool currentReading = (rawReading == 0);

    if (currentReading != lastBtnState[i]) {
      delay(25); 
      
      if (currentReading) {
        bool shouldTriggerMusic = false; 

        if (i == currentDealer) {
          if (isGameActive == true) {
            renchanCounter++; 
            resetAllRiichi();
            shouldTriggerMusic = true; 
            addLog("[🎲 骰子开局] 庄家 (P" + String(i+1) + ") 重复开局！本场数递增为: " + String(renchanCounter));
          } 
          else {
            isFirstHand = false;
            shouldTriggerMusic = true; 
            
            // 如果是3人麻将模式，在这个落槌激活瞬间，定向剥离关闭第四位闲家（北家）
            if (is3PlayerMode) {
              isPlayerPresent[3] = false; // 关闭北家生存权，锁死它
              scores[3] = 0;              // 分数归零不计入排顺
              Serial.println("【🀄 三麻落地】-> 第一局正式开局！已定向斩断关闭第四个玩家（北闲）的全部按键与显示！");
            }
          }
        }
        else if (i == nextDealerCandidate) {
          if (isFirstHand) { lastBtnState[i] = currentReading; continue; }

          renchanCounter++;
          currentDealer = nextDealerCandidate; 
          resetAllRiichi();
          shouldTriggerMusic = true; 
          updateOyaLeds(); 
          addLog("[🎲 骰子开局] 下家 (P" + String(i+1) + ") 开启新局！连庄及庄位切至 P" + String(currentDealer+1)); 
        }
        else {
          lastBtnState[i] = currentReading;
          continue;
        }

        lastDiceResult = random(2, 13); 
        targetPlayerIdx = getTargetPlayerByDice(lastDiceResult);
        addLog("[🎲 掷骰开门] 摇出点数: " + String(lastDiceResult) + " 点，开门位: P" + String(targetPlayerIdx+1));
        
        while (!isPlayerPresent[targetPlayerIdx]) {
          targetPlayerIdx = (targetPlayerIdx + 1) % 4;
        }

        diceDisplayTimer = millis();
        isGameActive = true; 

        if (shouldTriggerMusic) {
          hardResetMP3ToNormal(); 
        }

        updateAllSystem();
        delay(150); 
      } 
      lastBtnState[i] = currentReading; 
    }
  }
}

int getTargetPlayerByDice(int dice) {
  int relativeIdx = 0;
  if (dice == 5 || dice == 9)   relativeIdx = 0; 
  if (dice == 2 || dice == 6 || dice == 10)  relativeIdx = 1; 
  if (dice == 3 || dice == 7 || dice == 11)  relativeIdx = 2; 
  if (dice == 4 || dice == 8 || dice == 12)  relativeIdx = 3; 
  return (currentDealer + relativeIdx) % 4;
}

// =========================================================================
// 13. 霍尔立直传感器逻辑 (下降沿边沿触发)
// =========================================================================
void scanHallSensors() {
  if (!isGameActive) return; 

  for (int i = 0; i < 4; i++) {
    if (!isPlayerPresent[i]) continue; 

    bool currentReading = (digitalRead(hallPins[i]) == LOW); 
    if (hasRiichi[i]) { lastHallState[i] = currentReading; continue; }

    // 磁吸瞬间下降沿触发 (零延迟触发立直)
    if (currentReading && !lastHallState[i]) { 
      hasRiichi[i] = true; 
      
      scores[i] -= 1000; 
      riichiPool++; 
      addLog("[🀄 霍尔立直] P" + String(i+1) + " 拍下立直棒！扣除 1000 点，入池 1 棒"); 
      
      sendMP3Command(0x0E, 0x00, 0x00); 
      delay(50);
      
      uint8_t targetVoice = playerVoiceTracks[i];
      playFileInFolder(2, targetVoice); 
      
      activeRiichiTrack = i;      
      audioRestoreTimer = millis(); 
      currentAudioState = AUDIO_STATE_RIICHI_VOICE;

      if (diceDisplayTimer == 0) updateAllSystem(); 
    }
    lastHallState[i] = currentReading; 
  }
}

void resetAllRiichi() {
  for(int i = 0; i < 4; i++) { 
    hasRiichi[i] = false;      
  }
  isRiichiBGMActive = false; 
  activeRiichiTrack = -1; 
  audioRestoreTimer = 0; 
}

void clearTransferBuffer() {
  for(int i=0; i<4; i++) isLoserSelected[i] = false;
  inputBuffer = "";
}

// =========================================================================
// 14. 4x4 矩阵键盘解析与日麻转账结算状态机
// =========================================================================
void handleKey(char key) {
  inputTimeoutTimer = millis(); 
  int pIdx = getPlayerIndex(key);
  
  if (pIdx != -1) {
    if (!isPlayerPresent[pIdx]) return; 

    if (currentMode == MODE_IDLE || (currentMode == MODE_TRANSFER && inputBuffer.length() == 0)) {
      currentMode = MODE_TRANSFER;
      isLoserSelected[pIdx] = !isLoserSelected[pIdx]; 
      for(int i=0; i<4; i++) updateSingleDisplay(i);
    } 
    else if (currentMode == MODE_TRANSFER && inputBuffer.length() > 0) {
      int loserCount = 0;
      for(int i=0; i<4; i++) { if(isLoserSelected[i]) loserCount++; }
      
      if (loserCount == 0 || isLoserSelected[pIdx]) {
        clearTransferBuffer(); currentMode = MODE_IDLE; updateAllSystem(); return;
      }

      long baseVal = inputBuffer.toInt(); 
      long totalWinSum = 0;                    
      bool isDealerWinner = (pIdx == currentDealer);

      for (int i = 0; i < 4; i++) {
        if (isLoserSelected[i] && isPlayerPresent[i]) {
          long actualDeduct = baseVal; 
          
          // 💥【日麻标准本场棒分配规则】
          if (loserCount > 1) {
            // 1. 自摸情况：若该输家为当前庄家，需支付双倍基础分
            if (i == currentDealer) {
              actualDeduct = baseVal * 2; 
            } 
            // 自摸时其余各家平分支付本场点：每位输家各支付 100 * renchanCounter
            if (renchanCounter > 0) {
              actualDeduct += (100 * renchanCounter);
            }
          } 
          else {
            // 2. 荣和/放铳情况：由放铳单家全额包揽支付本场点 (三麻 200/本场，四麻 300/本场)
            if (renchanCounter > 0) {
              actualDeduct += ((is3PlayerMode ? 200 : 300) * renchanCounter);
            }
          }
          
          scores[i] -= actualDeduct;    
          totalWinSum += actualDeduct; 
        }
      }

      scores[pIdx] += totalWinSum; 
 
      if (riichiPool > 0) {
        long bonusPoints = riichiPool * 1000; 
        scores[pIdx] += bonusPoints;          
        riichiPool = 0;
      }

      if (isDealerWinner) {
        renchanCounter += 1; 
      } else {
        renchanCounter = 0;  
        currentDealer = (currentDealer + 1) % 4; 
        while (!isPlayerPresent[currentDealer]) {
          currentDealer = (currentDealer + 1) % 4;
        }
      }

      isGameActive = false; 
      sendMP3Command(0x16, 0x00, 0x00); // 💥【结算机制优化】和牌结算成功，当前局结束，系统回到待机状态并关闭背景音，等待掷骰开局
      currentAudioState = AUDIO_STATE_OFF;

      updateOyaLeds(); 
      resetAllRiichi(); 
      clearTransferBuffer(); 
      currentMode = MODE_IDLE;
      updateAllSystem(); 
      addLog("[💸 转账结算] P" + String(pIdx+1) + " 和牌结算成功！本局共进账: " + String(totalWinSum) + " 点");
      blinkWinner(pIdx);  
    }
    else if (currentMode == MODE_MANUAL_ADD || currentMode == MODE_MANUAL_SUB) {
      if (inputBuffer.length() > 0) {
        long val = inputBuffer.toInt(); 
        bool isAdd = (currentMode == MODE_MANUAL_ADD);
        if (isAdd) scores[pIdx] += val;
        else       scores[pIdx] -= val;
        addLog("[🔧 手动微调] P" + String(pIdx+1) + (isAdd ? " 强加 " : " 强扣 ") + String(val) + " 点");
        resetAllRiichi(); clearTransferBuffer(); currentMode = MODE_IDLE; updateAllSystem();
        if (isAdd) blinkWinner(pIdx);
      }
    }
  }
  else if (key == '+') {
    static unsigned long lastPlusPressTime = 0;
    if ((currentMode == MODE_MANUAL_ADD || currentMode == MODE_MANUAL_SUB) && (millis() - lastPlusPressTime < 2000)) {
      currentMode = (currentMode == MODE_MANUAL_ADD) ? MODE_MANUAL_SUB : MODE_MANUAL_ADD;
    } else {
      currentMode = MODE_MANUAL_ADD;
      inputBuffer = "";
    }
    addLog("[⌨️ 模式切换] 键盘切入: " + getModeName(currentMode));
    lastPlusPressTime = millis();
  }
  else if (isdigit(key)) {
    if (currentMode != MODE_IDLE) {
      inputBuffer += key;
      for(int i=0; i<4; i++) { if(isLoserSelected[i]) updateSingleDisplay(i); }
      if (inputBuffer.toInt() > 100000) { 
        clearTransferBuffer(); currentMode = MODE_IDLE; updateAllSystem(); 
      }
    }
  }
}

int getPlayerIndex(char key) {
  if (key == 'E') return 0; if (key == 'S') return 1; if (key == 'W') return 2; if (key == 'N') return 3;
  return -1;
}

String getPlayerName(int p) {
  String names[] = {"【玩家1】", "【玩家2】", "【玩家3】", "【玩家4】"}; 
  String windNames[] = {" (当前东家)", " (当前南家)", " (当前西家)", " (当前北家)"};
  return names[p] + windNames[(p - currentDealer + 4) % 4];
}

String getModeName(Mode mode) {
  if (mode == MODE_IDLE) return "【大盘待机模式】";
  if (mode == MODE_TRANSFER) return "【点数精算转账模式】";
  if (mode == MODE_MANUAL_ADD) return "【强行增分微调模式】";
  return "【强行减分微调模式】";
}

// =========================================================================
// 15. 数码管显示渲染与顺位算法
// =========================================================================
void calculateRanks() {
  for (int i = 0; i < 4; i++) {
    if (!isPlayerPresent[i]) { ranks[i] = 4; continue; } 
    int r = 1;
    for (int j = 0; j < 4; j++) {
      if (isPlayerPresent[j] && scores[j] > scores[i]) r++; 
      else if (isPlayerPresent[j] && scores[j] == scores[i] && j < i) r++; 
    }
    ranks[i] = r;
  }
}

void updateAllSystem() {
  calculateRanks(); 
  for (int i = 0; i < 4; i++) updateSingleDisplay(i);
}

void updateSingleDisplay(int playerIdx) {
  Serial.printf("[DEBUG Display] playerIdx: %d, present: %d, score: %ld, rank: %d\n", playerIdx, isPlayerPresent[playerIdx], scores[playerIdx], ranks[playerIdx]);
  if (!isPlayerPresent[playerIdx]) {
    uint8_t blankData[6] = { SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK };
    displays[playerIdx].setSegments(blankData, 6, 0);
    return; 
  }

  uint8_t data[6] = { SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK }; 
  uint8_t totalScoreDot = (scores[0] + scores[1] + scores[2] + scores[3] == totalsum) ? 0x80 : 0x00;
  
  uint8_t dot3 = (renchanCounter >= 1 && renchanCounter < 6) ? 0x80 : 0x00; 
  uint8_t dot4 = (renchanCounter >= 2 && renchanCounter < 6) ? 0x80 : 0x00; 
  uint8_t dot5 = (renchanCounter >= 3 && renchanCounter < 6) ? 0x80 : 0x00; 
  uint8_t dot0 = (renchanCounter >= 4 && renchanCounter < 6) ? 0x80 : 0x00; 
  uint8_t dot1 = (renchanCounter >= 5) ? 0x80 : 0x00; 
  if (renchanCounter > 5) {
    dot3 = 0x00; dot4 = 0x00; dot5 = 0x00; dot0 = 0x00; 
    dot0 = (renchanCounter >= 6) ? 0x80 : 0x00; 
    dot5 = (renchanCounter >= 7) ? 0x80 : 0x00; 
    dot4 = (renchanCounter >= 8) ? 0x80 : 0x00; 
    dot3 = (renchanCounter >= 9) ? 0x80 : 0x00; 
  }

  if (diceDisplayTimer != 0) {
    if (playerIdx == targetPlayerIdx) {
      data[2] = SEG_MINUS; data[1] = SEG_MINUS; data[0] = SEG_MINUS; data[5] = SEG_MINUS; 
      if (lastDiceResult >= 10) { data[4] = SEG_NUMBER[lastDiceResult / 10]; data[3] = SEG_NUMBER[lastDiceResult % 10]; } 
      else { data[4] = SEG_MINUS; data[3] = SEG_NUMBER[lastDiceResult]; }
    }
    else if (playerIdx == currentDealer) {
      data[2] = SEG_BLANK; data[1] = SEG_BLANK; data[0] = SEG_BLANK; data[5] = SEG_BLANK;
      if (lastDiceResult >= 10) { data[4] = SEG_NUMBER[lastDiceResult / 10]; data[3] = SEG_NUMBER[lastDiceResult % 10]; } 
      else { data[4] = SEG_BLANK; data[3] = SEG_NUMBER[lastDiceResult]; }
    }
  } 
  else if (currentMode == MODE_TRANSFER && isLoserSelected[playerIdx]) {
    if (inputBuffer.length() == 0) {
      if (blinkState) {
        data[2] = SEG_NUMBER[ranks[playerIdx]]; 
        long absScore = abs(scores[playerIdx]); long displayVal = absScore / 100;
        data[1] = (scores[playerIdx] < 0 ? SEG_MINUS : SEG_BLANK) | dot1;
        data[0] = SEG_NUMBER[(displayVal / 1000) % 10] | dot0; 
        data[5] = SEG_NUMBER[(displayVal / 100) % 10]  | dot5;
        data[4] = SEG_NUMBER[(displayVal / 10) % 10]   | dot4; 
        data[3] = SEG_NUMBER[displayVal % 10]          | dot3;
      } else {
        for(int s=0; s<6; s++) data[s] = SEG_BLANK; 
      }
    }
    else {
      long currentVal = inputBuffer.toInt(); 
      int loserCount = 0;
      for(int i=0; i<4; i++) { if(isLoserSelected[i]) loserCount++; }
      if (loserCount > 1 && playerIdx == currentDealer) currentVal = currentVal * 2; 

      String valStr = String(currentVal); int len = valStr.length();
      data[2] = SEG_MINUS; 
      data[3] = ((len >= 1) ? SEG_NUMBER[valStr[len - 1] - '0'] : SEG_BLANK) | dot3; 
      data[4] = ((len >= 2) ? SEG_NUMBER[valStr[len - 2] - '0'] : SEG_BLANK) | dot4; 
      data[5] = ((len >= 3) ? SEG_NUMBER[valStr[len - 3] - '0'] : SEG_BLANK) | dot5; 
      data[0] = ((len >= 4) ? SEG_NUMBER[valStr[len - 4] - '0'] : SEG_BLANK) | dot0; 
      data[1] = ((len >= 5) ? SEG_NUMBER[valStr[len - 5] - '0'] : SEG_BLANK) | dot1;
    }
  }
  else {
    data[2] = SEG_NUMBER[ranks[playerIdx]] | totalScoreDot;  

    long absScore = abs(scores[playerIdx]); 
    long displayVal = absScore / 100; 

    data[1] = (scores[playerIdx] < 0 ? SEG_MINUS : SEG_BLANK) | dot1; 
    data[0] = SEG_NUMBER[(displayVal / 1000) % 10] | dot0; 
    data[5] = SEG_NUMBER[(displayVal / 100) % 10]  | dot5;  
    data[4] = SEG_NUMBER[(displayVal / 10) % 10]   | dot4; 
    data[3] = SEG_NUMBER[displayVal % 10]          | dot3;          
  }
  displays[playerIdx].setSegments(data, 6, 0);
}

void blinkWinner(int winnerIdx) {
  for (int i = 0; i < 3; i++) {
    uint8_t blankData[6] = {SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK};
    displays[winnerIdx].setSegments(blankData, 6, 0); delay(200);
    updateSingleDisplay(winnerIdx); delay(200);
  }
}

char scanKeypad() {
  char foundKey = '\0'; 
  for (uint8_t i = 4; i < 8; i++) pcf8575.write(i, 1);
  for (uint8_t r = 4; r < 8; r++) {
    pcf8575.write(r, 0); delayMicroseconds(30); 
    for (uint8_t c = 0; c < 4; c++) { 
      if (pcf8575.read(c) == 0) { foundKey = keyMap[r - 4][c]; break; } 
    }
    pcf8575.write(r, 1); 
    if (foundKey != '\0') break; 
  }
  return foundKey;
}