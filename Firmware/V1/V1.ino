#include "Arduino.h"
#include "PCF8575.h"              // PCF8575 库
#include <Wire.h>                 // Wire 库
#include <TM1637Display.h> 

// === 1. 游戏状态机模式定义 (必须放在最前) ===
enum Mode { MODE_IDLE, MODE_TRANSFER, MODE_MANUAL_ADD, MODE_MANUAL_SUB };
Mode currentMode = MODE_IDLE;

// === 2. 引脚与地址定义 ===
#define I2C_SDA 13
#define I2C_SCL 14
#define PCF8575_ADDRESS 0x20  

#define FPSERIAL Serial2 

#define TM_CLK 22 
const uint8_t tmDIO[4] = {21, 19, 18, 5}; 

// === 4路霍尔传感器引脚定义 ===
const uint8_t hallPins[4] = {26, 27, 33, 32};

// 时间滤波变量
unsigned long hallHighTimer[4] = {0, 0, 0, 0}; 
bool lastHallState[4] = {false, false, false, false}; 

// 💥【完美回归】立直局内锁定状态：true 代表本局已立直过，锁死不再扣分
bool hasRiichi[4] = {false, false, false, false}; 

// === 💥 日麻核心算法变量 ===
int currentDealer = 0;               // 核心指针：当前哪位物理玩家是“东家”(0->P1, 1->P2, 2->P3, 3->P4)
int renchanCounter = 0;              // 核心变量：连庄数（本场数）
bool isFirstHand = true;             // 核心变量：是否为开机后的第一局

unsigned long diceDisplayTimer = 0; 
int lastDiceResult = 0;             
int targetPlayerIdx = -1;           

// === 转账动态闪烁与非阻塞守卫变量 ===
unsigned long lastBlinkTime = 0;    
bool blinkState = true;             

// === 键盘输入全流程5秒超时守卫变量 ===
unsigned long inputTimeoutTimer = 0; 

// === 全局音量与物理绝对线序音频变量 ===
int systemVolume = 20;               
int defaultBGM = 12;                 

int activeRiichiTrack = -1;          
bool isRiichiBGMActive = false;      
unsigned long audioRestoreTimer = 0; 

// === 防误触核心全局状态锁 ===
bool isGameActive = false;           

// === 3. 实例初始化 ===
PCF8575 pcf8575(PCF8575_ADDRESS);
TM1637Display displays[4] = {
  TM1637Display(TM_CLK, tmDIO[0]), 
  TM1637Display(TM_CLK, tmDIO[1]), 
  TM1637Display(TM_CLK, tmDIO[2]), 
  TM1637Display(TM_CLK, tmDIO[3])  
};

// === 4. 数码管特殊字符与段码定义 ===
const uint8_t SEG_BLANK = 0x00;
const uint8_t SEG_MINUS = 0x40; 
const uint8_t SEG_NUMBER[] = {
  0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F 
};

// === 5. 游戏计分系统核心变量 ===
long scores[4] = {25000, 25000, 25000, 25000};
int totalsum = 100000;
int ranks[4] = {1, 1, 1, 1};

// === 🀄 立直棒供托核心变量 ===
int riichiPool = 0;              // 场面上当前累积的立直棒总数（每根代表1000点）
bool isRiichi[4] = {false, false, false, false}; // 记录本局有谁拍出了立直棒

// === 📈 历史走势内存存储变量 ===
#define MAX_HANDS 32              // 最大支持记录32局走势
long scoreHistory[MAX_HANDS][4];  // 存放每局结束时4位玩家的分数快照
int dealerHistory[MAX_HANDS];     // 存放每局对应的东家物理索引(0-3)
int totalHandsRecorded = 0;       // 当前已经记录的局数计数器

bool isLoserSelected[4] = {false, false, false, false}; 
String inputBuffer = "";

// === 6. 4x4 键盘矩阵映射表 ===
char keyMap[4][4] = {
  {'E', '3', '2', '1'}, 
  {'S', '6', '5', '4'}, 
  {'W', '9', '8', '7'}, 
  {'N', '-', '0', '+'}   
};

// === 7. 最终确定的按键与灯光物理引脚映射 ===
const uint8_t btnPins[4] = {15, 13, 11, 9}; 
const uint8_t ledPins[4] = {14, 12, 10, 8}; 

// 按键状态记忆守卫变量（默认为false低电平）
bool lastBtnState[4] = {false, false, false, false}; 

// 函数预声明
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

// =========================================================================
// 🔊 MH2024K 芯片专属底层原始 HEX 串驱动引擎（不依赖第三方库函数）
// =========================================================================
const uint8_t playerVoiceTracks[4] = {1, 3, 4, 7}; // 02文件夹下的立直人声 (001, 003, 004, 007)
const uint8_t playerBGMTracks[4]   = {1, 2, 3, 4}; // 03文件夹下的处刑曲 (001, 002, 003, 004)

void sendMP3Command(uint8_t cmd, uint8_t para1, uint8_t para2) {
  uint8_t msg[10];
  msg[0] = 0x7E; // 起始位
  msg[1] = 0xFF; // 版本号
  msg[2] = 0x06; // 核心内容数据长度
  msg[3] = cmd;  // 命令字
  msg[4] = 0x00; // Feedback: 不需要应答
  msg[5] = para1;// 参数高字节 (文件夹号)
  msg[6] = para2;// 参数低字节 (曲目号)
  
  // 严格计算校验和：长度位所有数据(VER到para2共6字节)相加，取反加1
  uint16_t sum = msg[1] + msg[2] + msg[3] + msg[4] + msg[5] + msg[6];
  uint16_t checksum = ~sum + 1;
  
  msg[7] = (uint8_t)(checksum >> 8);   
  msg[8] = (uint8_t)(checksum & 0xFF); 
  msg[9] = 0xEF; // 结束位

  FPSERIAL.write(msg, 10);
  delay(30); // 串口底层延迟缓冲
}

void playFileInFolder(uint8_t folder, uint8_t fileIdx) {
  sendMP3Command(0x0F, folder, fileIdx);
  Serial.println("[🔊 原始HEX发射] 点播路径: /0" + String(folder) + "/00" + String(fileIdx) + ".mp3");
}

void loopCurrentTrack() {
  sendMP3Command(0x08, 0x00, 0x01); // 0x08 0x00 0x01 代表当前单曲硬循环播放
  Serial.println("[🔊 原始HEX发射] 已下达当前曲目【芯片级单曲循环】控制锁！");
}

// =========================================================================
// 💥 【核心顺序修正】将外部网页服务包含放在所有全局变量声明之下，使其完美继承活性
// =========================================================================
#include "web_server.h" 
WebServer server(80);   // 实例化全局 80 端口网页服务器

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n========================================================");
  Serial.println("     键盘&BGM&计分&骰子&霍尔立直系统固件（原生修正平账版）");
  Serial.println("========================================================");

  for(int i = 0; i < 4; i++) {
    displays[i].setBrightness(4); 
  }

  // 硬件串口2配置 (RX=16, TX=17, 9600波特率, SERIAL_8N1)
  FPSERIAL.begin(9600, SERIAL_8N1, 16, 17); 
  delay(100);
  
  // 初始化音量设置与默认挂起
  sendMP3Command(0x06, 0x00, systemVolume); 
  delay(50);
  sendMP3Command(0x0E, 0x00, 0x00); 

  Wire.begin(I2C_SDA, I2C_SCL); 
  if (pcf8575.begin(0xFFFF)) { 
    Serial.println("【⌨️ 芯片配置】-> PCF8575 键盘扩展芯片初始化成功！");
  } else {
    Serial.println("【❌ 核心错误】-> 未找到 PCF8575 芯片，系统挂起！");
    while(1); 
  }

  for(int i = 0; i < 4; i++) {
    pinMode(hallPins[i], INPUT);
    lastHallState[i] = (digitalRead(hallPins[i]) == HIGH);
    hallHighTimer[i] = 0; 
    hasRiichi[i] = false;
  }

  randomSeed(analogRead(34)); 
  
  currentDealer = 0;
  renchanCounter = 0;
  isFirstHand = true;
  
  updateOyaLeds(); 
  updateAllSystem();

  // 写入第0局初始分快照
  for(int i=0; i<4; i++) scoreHistory[0][i] = scores[i];
  dealerHistory[0] = currentDealer;
  totalHandsRecorded = 1; 

  initWiFiAndWeb(); // 让 ESP32 挂载网页服务器
}

void loop() {
  // 1. 矩阵键盘输出控制
  for (uint8_t pin = 0; pin < 4; pin++)  pcf8575.write(pin, 1); 
  for (uint8_t pin = 4; pin < 8; pin++)  pcf8575.write(pin, 0); 
  
  // 2. 独立按键配置
  pcf8575.write(15, 0);
  pcf8575.write(13, 0);
  pcf8575.write(11, 0);
  pcf8575.write(9,  0);

  // 3. 动态刷新东家指示灯
  updateOyaLeds();
  delayMicroseconds(50); 

  // =========================================================================
  // 🔊 原生 HEX 级音频状态机：负责截断真人语音并高燃接入处刑曲单曲循环
  // =========================================================================
  if (audioRestoreTimer != 0) {
    if (millis() - audioRestoreTimer >= 2500) { 
      audioRestoreTimer = 0; 
      isRiichiBGMActive = true; 
      
      uint8_t targetBGM = playerBGMTracks[activeRiichiTrack];
      Serial.println("\n----------------------------------------------------");
      Serial.print("[🔊 音频状态机] 真人语音宣告退场。正在无缝突入 ");
      Serial.print(getPlayerName(activeRiichiTrack));
      Serial.println(" 位于 [03] 文件夹的专属处刑曲，曲目号: " + String(targetBGM));
      
      playFileInFolder(3, targetBGM); 
      delay(50);
      loopCurrentTrack(); 
      Serial.println("----------------------------------------------------");
    }
  }

  // 5秒无操作超时守卫
  if (currentMode != MODE_IDLE) {
    if (millis() - inputTimeoutTimer >= 5000) {
      clearTransferBuffer(); 
      currentMode = MODE_IDLE;
      updateAllSystem();
    }
  }

  // 转账闪烁
  if (millis() - lastBlinkTime >= 250) {
    lastBlinkTime = millis();
    blinkState = !blinkState;
    if (currentMode == MODE_TRANSFER && inputBuffer.length() == 0 && diceDisplayTimer == 0) {
      for(int i=0; i<4; i++) {
        if(isLoserSelected[i]) updateSingleDisplay(i); 
      }
    }
  }

  // 数码管骰子显示寿命结束刷新大盘
  if (diceDisplayTimer != 0) {
    if (millis() - diceDisplayTimer >= 2000) {
      diceDisplayTimer = 0;
      updateAllSystem();
    }
  }

  char pressedKey = scanKeypad(); 
  if (pressedKey != '\0') {
    Serial.println("\n------------------------------------");
    Serial.println("[⌨️ 键盘扫描] 物理按键触发: '" + String(pressedKey) + "'");
    handleKey(pressedKey);
    delay(200); 
  }
  
  scanDiceButtons();
  scanHallSensors();
  
  handleWiFiClient(); // 维持网页/穿透长连接

  // =========================================================================
  // 智能麻将战局换届哨兵（立直扣分绝对不会误触发）
  // =========================================================================
  static int  lastRenchan = 0;       
  static int  lastDealer = 0;        
  bool roundEnded = false;           

  if (renchanCounter != lastRenchan) { roundEnded = true; lastRenchan = renchanCounter; }
  if (currentDealer != lastDealer)   { roundEnded = true; lastDealer = currentDealer; }

  if (roundEnded) {
    Serial.println("\n[🔍 哨兵截获] 监测到庄位更替或连庄结算，确认换局！正在追加历史快照...");
    if (totalHandsRecorded < MAX_HANDS) {
      for (int i = 0; i < 4; i++) {
        scoreHistory[totalHandsRecorded][i] = scores[i]; 
      }
      dealerHistory[totalHandsRecorded] = currentDealer;  
      totalHandsRecorded++;
      Serial.println("[📈 历史引擎] 成功写入第 " + String(totalHandsRecorded - 1) + " 局终盘走势数据。");
    } else {
      Serial.println("[⚠️ 历史警报] 内存存储已满，不再记录后续局数走势。");
    }
  }
  delay(20);
}

void updateOyaLeds() {
  for (int i = 0; i < 4; i++) {
    pcf8575.write(ledPins[i], (i == currentDealer) ? 0 : 1);
  }
}

void scanDiceButtons() {
  int nextDealerCandidate = (currentDealer + 1) % 4; 

  for (int i = 0; i < 4; i++) {
    int rawReading = pcf8575.read(btnPins[i]);
    bool currentReading = (rawReading == 1);

    if (currentReading != lastBtnState[i]) {
      delay(25); 
      
      if (currentReading) {
        if (i == currentDealer) {
          if (isGameActive == true) {
            renchanCounter++; 
            resetAllRiichi();

            playFileInFolder(1, 2); 
            delay(50);
            loopCurrentTrack();
    
            Serial.println("\n====================================================");
            Serial.print("[🎲 骰子重置流局] 庄家 " + getPlayerName(i) + " 重复开局！触发连庄棒递增。");
            Serial.println("\n[⚙️ 状态机累加] 全场连庄数变为: " + String(renchanCounter) + " 本场");
            Serial.println("====================================================");
          } 
          else {
            isFirstHand = false;
          }
        }
        else if (i == nextDealerCandidate) {
          if (isFirstHand) { lastBtnState[i] = currentReading; continue; }

          int oldOya = currentDealer;
          renchanCounter++;
          currentDealer = nextDealerCandidate; 
          resetAllRiichi();

          playFileInFolder(1, 2); 
          delay(50);
          loopCurrentTrack();
          
          updateOyaLeds(); 
        }
        else {
          lastBtnState[i] = currentReading;
          continue;
        }

        lastDiceResult = random(2, 13); 
        targetPlayerIdx = getTargetPlayerByDice(lastDiceResult);
        diceDisplayTimer = millis();
        isGameActive = true; 

        if (!isRiichiBGMActive && audioRestoreTimer == 0) {
          playFileInFolder(1, 2); 
          delay(50);
          loopCurrentTrack(); 
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

void scanHallSensors() {
  if (!isGameActive) return; 

  if (millis() < 2000) {
    for (int i = 0; i < 4; i++) {
      hallHighTimer[i] = 0; 
      lastHallState[i] = (digitalRead(hallPins[i]) == HIGH); 
    }
    return; 
  }

  for (int i = 0; i < 4; i++) {
    bool currentReading = (digitalRead(hallPins[i]) == HIGH);
    if (hasRiichi[i]) { lastHallState[i] = currentReading; continue; }

    if (currentReading) {
      if (!lastHallState[i]) { 
        hallHighTimer[i] = millis(); 
      } 
      else {
        if (hallHighTimer[i] > 0 && (millis() - hallHighTimer[i] >= 1000)) {
          hasRiichi[i] = true; 
          
          scores[i] -= 1000; 
          riichiPool++; 
          
          sendMP3Command(0x0E, 0x00, 0x00); // 挂起当前BGM
          delay(50);
          
          uint8_t targetVoice = playerVoiceTracks[i];
          playFileInFolder(2, targetVoice); 
          
          activeRiichiTrack = i;      
          audioRestoreTimer = millis(); 

          if (diceDisplayTimer == 0) updateAllSystem(); 
        }
      }
    } else { hallHighTimer[i] = 0; }
    lastHallState[i] = currentReading; 
  }
}

void resetAllRiichi() {
  Serial.println("[🧹 清场机制] 确认本局结束，开始剥离全场玩家物理立直锁定状态...");
  for(int i = 0; i < 4; i++) { 
    hasRiichi[i] = false;       
    hallHighTimer[i] = 0;       
  }
  
  isRiichiBGMActive = false; 
  activeRiichiTrack = -1; 
  audioRestoreTimer = 0; 

  Serial.println("[🔊 音乐引擎] 向 MH2024K 下发 0x0E 暂停清场，回归清净。");
  sendMP3Command(0x0E, 0x00, 0x00);
}

void clearTransferBuffer() {
  for(int i=0; i<4; i++) isLoserSelected[i] = false;
  inputBuffer = "";
}

void handleKey(char key) {
  inputTimeoutTimer = millis(); 
  int pIdx = getPlayerIndex(key);
  
  if (pIdx != -1) {
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

      long baseVal = inputBuffer.toInt() * 100; 
      long totalWinSum = 0;                    
      bool isDealerWinner = (pIdx == currentDealer);

      for (int i = 0; i < 4; i++) {
        if (isLoserSelected[i]) {
          long actualDeduct = baseVal; 
          if (loserCount > 1 && i == currentDealer) {
            actualDeduct = baseVal * 2; 
          } 
          if (i == currentDealer && renchanCounter > 0) {
            actualDeduct += (300 * renchanCounter);
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

      int oldDealer = currentDealer;
      if (isDealerWinner) {
        renchanCounter += 1; 
      } else {
        renchanCounter = 0;  
        currentDealer = (currentDealer + 1) % 4; 
      }

      updateOyaLeds(); 
      resetAllRiichi(); 
      clearTransferBuffer(); 
      currentMode = MODE_IDLE;
      updateAllSystem(); 
      blinkWinner(pIdx);  
    }
    else if (currentMode == MODE_MANUAL_ADD || currentMode == MODE_MANUAL_SUB) {
      if (inputBuffer.length() > 0) {
        long val = inputBuffer.toInt() * 100; 
        if (currentMode == MODE_MANUAL_ADD) scores[pIdx] += val;
        else                               scores[pIdx] -= val;
        resetAllRiichi(); clearTransferBuffer(); currentMode = MODE_IDLE; updateAllSystem();
        if (currentMode == MODE_MANUAL_ADD) blinkWinner(pIdx);
      }
    }
  }
  else if (key == '+' || key == '-') {
    currentMode = (key == '+') ? MODE_MANUAL_ADD : MODE_MANUAL_SUB; 
    inputBuffer = "";
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

void calculateRanks() {
  for (int i = 0; i < 4; i++) {
    int r = 1;
    for (int j = 0; j < 4; j++) {
      if (scores[j] > scores[i]) r++; 
      else if (scores[j] == scores[i] && j < i) r++; 
    }
    ranks[i] = r;
  }
}

void updateAllSystem() {
  calculateRanks(); 
  for (int i = 0; i < 4; i++) updateSingleDisplay(i);
}

void updateSingleDisplay(int playerIdx) {
  uint8_t data[6] = { SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK, SEG_BLANK }; 

  uint8_t totalScoreDot = (scores[0] + scores[1] + scores[2] + scores[3] == 100000) ? 0x80 : 0x00;
  
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
  char foundKey = '\0'; bool anyKeyPressed = false;
  for (uint8_t c = 0; c < 4; c++) { if (pcf8575.read(c) == 0) { anyKeyPressed = true; break; } }
  if (!anyKeyPressed) return '\0'; 
  for (uint8_t r = 4; r < 8; r++) {
    for (uint8_t i = 4; i < 8; i++) pcf8575.write(i, 1); 
    pcf8575.write(r, 0); delayMicroseconds(30); 
    for (uint8_t c = 0; c < 4; c++) { if (pcf8575.read(c) == 0) { foundKey = keyMap[r - 4][c]; break; } }
    if (foundKey != '\0') break; 
  }
  return foundKey;
}