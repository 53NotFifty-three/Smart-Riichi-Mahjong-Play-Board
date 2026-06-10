// =========================================================================
// 🌐 独立标签页：web_server.h (立直池无线微调、常亮红字与换届二次确认防误触版)
// =========================================================================
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

#include <WiFi.h>
#include <WebServer.h>

const char* wifi_ssid     = "Fish&53"; 
const char* wifi_password = "Dd@20040923";   

extern WebServer server; 

// 外部依赖变量声明
extern int renchanCounter;
extern int currentDealer;
extern long scores[4];
extern int ranks[4];
extern Mode currentMode; 

// 引入核心霍尔立直物理锁定状态数组和公积金池变量声明
extern bool hasRiichi[4]; 
extern int riichiPool; // 💥 引入全局立直棒池

// 历史记录与 MH2024K 原生音量全局声明
extern long scoreHistory[32][4];
extern int dealerHistory[32];
extern int totalHandsRecorded;
extern int systemVolume; 

// MH2024K 原生 HEX 驱动引擎外部函数引用
extern void sendMP3Command(uint8_t cmd, uint8_t para1, uint8_t para2);
extern void playFileInFolder(uint8_t folder, uint8_t fileIdx);
extern void loopCurrentTrack();

extern String getPlayerName(int p);
extern void updateOyaLeds();
extern void resetAllRiichi();
extern void updateAllSystem();
extern String getModeName(Mode mode); 

// 💥 网页综合模板
String getPageHTML() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>";
  
  html += "<style>body{font-family:sans-serif; text-align:center; background:#f4f4f4; margin:0; padding:15px;}";
  html += "h1{color:#333; margin-top:10px;} .status-bar{color:#666; margin-bottom:15px; font-size:16px;}";
  html += ".btn{display:inline-block; width:85%; padding:14px; margin:8px auto; font-size:16px; font-weight:bold; color:white; background:#007BFF; border:none; border-radius:8px; box-shadow:0 3px 6px rgba(0,0,0,0.1); cursor:pointer; -webkit-tap-highlight-color:transparent;}";
  
  // 玩家得分卡片基本样式
  html += ".score-box{background:white; padding:15px; margin:10px auto; width:88%; border-radius:10px; box-shadow:0 4px 8px rgba(0,0,0,0.05); text-align:left; box-sizing:border-box; cursor:pointer; transition:transform 0.1s; -webkit-tap-highlight-color:transparent;}";
  html += ".score-box:active{transform:scale(0.97); background:#fdfdfd;}";
  
  // 右侧分数显示区样式
  html += ".score-num-container{float:right; margin-top:-3px; text-align:right;}";
  html += ".score-num{font-size:24px; font-weight:bold; color:#28a745;}";
  
  // 红字立直常亮样式
  html += ".riichi-tag{font-size:18px; font-weight:900; color:#dc3545; margin-right:8px; display:inline-block;}";
  
  // 各控制面板基础样式
  html += ".media-panel, .control-panel, .setup-panel{background:white; padding:15px; margin:15px auto; width:88%; border-radius:10px; box-shadow:0 4px 8px rgba(0,0,0,0.05); box-sizing:border-box;}";
  html += ".media-flex, .control-flex, .setup-flex{display:flex; align-items:center; justify-content:space-between; gap:12px; margin-top:8px;}";
  html += ".media-btn{width:55px; height:45px; font-size:18px; font-weight:bold; color:white; background:#6c757d; border:none; border-radius:8px; cursor:pointer; flex-shrink:0; display:flex; align-items:center; justify-content:center;}";
  html += ".control-btn{flex:1; padding:12px 5px; font-size:14px; font-weight:bold; color:white; border:none; border-radius:8px; cursor:pointer; box-shadow:0 2px 4px rgba(0,0,0,0.08); text-align:center;}";
  
  // 庄位与立直池微调按键样式
  html += ".setup-sub-btn{flex:1; padding:10px 2px; font-size:13px; font-weight:bold; color:#495057; background:#e9ecef; border:1px solid #ced4da; border-radius:6px; cursor:pointer;}";
  html += ".setup-active-oya{background:#fd7e14 !important; color:white !important; border-color:#fd7e14 !important;}"; 
  
  html += ".slider-container{flex-grow:grow; width:100%; text-align:left;}";
  html += ".volume-slider{width:100%; margin-top:5px; cursor:pointer; height:8px; background:#ddd; border-radius:5px;}";
  
  // 虚拟弹出对话框（Modal）样式
  html += ".modal{display:none; position:fixed; z-index:999; left:0; top:0; width:100%; height:100%; background:rgba(0,0,0,0.5); align-items:center; justify-content:center;}";
  html += ".modal-content{background:white; padding:20px; border-radius:12px; width:82%; max-width:320px; box-shadow:0 5px 15px rgba(0,0,0,0.2); text-align:center;}";
  html += ".modal-input{width:90%; padding:12px; font-size:18px; margin:15px 0; border:1px solid #ccc; border-radius:6px; text-align:center; box-sizing:border-box;}";
  html += ".modal-btn-group{display:flex; justify-content:space-between; gap:10px;}";
  html += ".m-btn{flex:1; padding:12px; font-size:14px; font-weight:bold; color:white; border:none; border-radius:6px; cursor:pointer;}";
  html += "</style>";
  
  // JavaScript 动态注入核心
  html += "<script>";
  html += "let myChart = null;";
  html += "let isPlaying = false;"; 
  html += "let activeTargetPlayer = -1;"; 
  
  // 视图切换
  html += "function switchView(view) {";
  html += "  if(view === 'chart') {";
  html += "    document.getElementById('main_view').style.display = 'none';";
  html += "    document.getElementById('chart_view').style.display = 'block';";
  __attribute__((unused)) html += "    loadChartHistory();"; 
  html += "  } else {";
  html += "    document.getElementById('main_view').style.display = 'block';";
  html += "    document.getElementById('chart_view').style.display = 'none';";
  html += "  }";
  html += "}";
  
  // 弹出和关闭无线智能调分框
  html += "function openScoreModal(idx) {";
  html += "  activeTargetPlayer = idx;";
  html += "  document.getElementById('modal_title').innerText = '接管调分: P' + (idx+1);";
  html += "  document.getElementById('score_input').value = '';"; 
  html += "  document.getElementById('score_modal').style.display = 'flex';";
  html += "}";
  html += "function closeModal() { document.getElementById('score_modal').style.display = 'none'; }";
  
  // 异步改分提交
  html += "function submitScoreAdjustment(type) {";
  html += "  let val = document.getElementById('score_input').value;";
  html += "  if(!val || isNaN(val) || parseInt(val) <= 0) { alert('请输入有效的点数！'); return; }";
  html += "  fetch('/api/adjust_score?player=' + activeTargetPlayer + '&amount=' + val + '&type=' + type)";
  html += "  .then(r => r.text()).then(res => { closeModal(); });";
  html += "}";
  
  // 💥 核心修改：无线遥控修改庄位（加入极其严格的二次安全拦截弹窗确认）
  html += "function setOya(playerIdx) {";
  html += "  let posNames = ['玩家1 (东)', '玩家2 (南)', '玩家3 (西)', '玩家4 (北)'];";
  html += "  if(confirm('🚨 [安全防线] 确认要强行将当前庄家修改为 ' + posNames[playerIdx] + ' 吗？\\n这将会重设所有立直锁定并强制平账刷新数码管！')) {";
  html += "    fetch('/api/set_oya?idx=' + playerIdx);";
  html += "  }";
  html += "}";
  
  // 局数微调
  html += "function adjustHandCount(action) {";
  html += "  fetch('/api/adjust_hands?action=' + action);";
  html += "}";
  
  // 💥 新增：立直公积金池无线微调触发函数
  html += "function adjustRiichiPool(action) {";
  html += "  fetch('/api/adjust_pool?action=' + action);";
  html += "}";
  
  // AJAX 0.5秒实时大刷盘核心
  html += "setInterval(function() {";
  html += "  if(document.getElementById('main_view').style.display !== 'none') {";
  html += "    fetch('/api/get_scores').then(r => r.json()).then(data => {";
  html += "      document.getElementById('renchan_val').innerText = data.renchan;";
  html += "      document.getElementById('mode_val').innerText = data.mode_str;"; 
  html += "      document.getElementById('hands_val').innerText = data.total_hands;"; 
  html += "      document.getElementById('pool_val').innerText = data.pool_sticks;"; // 💥 实时将底层的立直棒总数拉取到网页上
  html += "      isPlaying = data.is_playing;"; 
  html += "      document.getElementById('play_btn').innerText = isPlaying ? '⏸' : '▶';";
  html += "      document.getElementById('play_btn').style.background = isPlaying ? '#dc3545' : '#28a745';";
  html += "      document.getElementById('btn_transfer').style.background = (data.mode_id === 1) ? '#28a745' : '#6c757d';";
  html += "      document.getElementById('btn_manual').style.background = (data.mode_id === 2) ? '#17a2b8' : '#6c757d';";
  
  html += "      for(let i=0; i<4; i++) {";
  html += "        let oyaBtn = document.getElementById('oya_btn_' + i);";
  html += "        if(data.oya_idx === i) { oyaBtn.classList.add('setup-active-oya'); } else { oyaBtn.classList.remove('setup-active-oya'); }";
  html += "        document.getElementById('score_' + i).innerText = data.scores[i];";
  __attribute__((unused)) html += "        document.getElementById('rank_' + i).innerText = '第 ' + data.ranks[i] + ' 名';";
  
  html += "        if(data.riichi_states[i] === true) {";
  html += "          document.getElementById('riichi_box_' + i).innerHTML = '<span class=\"riichi-tag\">立直</span>';";
  html += "        } else {";
  html += "          document.getElementById('riichi_box_' + i).innerHTML = '';"; 
  html += "        }";
  html += "      }";
  html += "    });";
  html += "  }";
  html += "}, 500);";
  
  // 音量与播放控制
  html += "function onSliderChange(rawVal) {";
  html += "  let percent = Math.round((rawVal / 30) * 100);";
  html += "  document.getElementById('vol_percent').innerText = percent + '%';";
  html += "  if(parseInt(rawVal) === 0) {";
  html += "    isPlaying = false; document.getElementById('play_btn').innerText = '▶'; document.getElementById('play_btn').style.background = '#28a745';";
  html += "  } else {";
  html += "    if(isPlaying) { document.getElementById('play_btn').innerText = '⏸'; document.getElementById('play_btn').style.background = '#dc3545'; }";
  html += "  }";
  html += "  fetch('/api/set_volume?val=' + rawVal);";
  html += "}";
  html += "function togglePlay() {";
  html += "  let slider = document.getElementById('vol_slider'); let currentRaw = parseInt(slider.value);";
  html += "  if(!isPlaying && currentRaw === 0) { slider.value = 6; document.getElementById('vol_percent').innerText = '20%'; }";
  html += "  fetch('/bgm_toggle');";
  html += "}";
  html += "function triggerMode(targetMode) { fetch('/api/set_mode?type=' + targetMode); }";
  
  // Chart
  html += "function loadChartHistory() {";
  html += "  fetch('/api/get_history').then(r => r.json()).then(res => {";
  html += "    const ctx = document.getElementById('canvas_history').getContext('2d');";
  html += "    if(myChart) myChart.destroy();"; 
  html += "    myChart = new Chart(ctx, {";
  html += "      type: 'line',"; 
  html += "      data: {";
  html += "        labels: res.labels,"; 
  html += "        datasets: [";
  __attribute__((unused)) html += "          { label: 'P1', data: res.p1, borderColor: '#ff4d4d', tension: 0.15, fill: false },";
  html += "          { label: 'P2', data: res.p2, borderColor: '#3399ff', tension: 0.15, fill: false },";
  html += "          { label: 'P3', data: res.p3, borderColor: '#00cc66', tension: 0.15, fill: false },";
  html += "          { label: 'P4', data: res.p4, borderColor: '#ffcc00', tension: 0.15, fill: false }";
  html += "        ]";
  html += "      },";
  html += "      options: {responsive:true, plugins:{tooltip:{callbacks:{footer:function(items){return '当前局庄家: P'+res.dealers[items[0].dataIndex];}}}}, scales:{y:{ticks:{stepSize:1000}}}}";
  html += "    });";
  html += "  });";
  html += "}";
  html += "</script>";
  html += "</head><body>";
  
  // -------------------------------------------------------------
  // 🚩 视图一：主对局大盘
  // -------------------------------------------------------------
  html += "<div id='main_view'>";
  html += "<h1>立直麻将智能风盘</h1>";
  html += "<div class='status-bar'>";
  html += "  <span id='renchan_val' style='color:#dc3545; font-weight:bold;'>0</span> 本场 | ";
  html += "  <span id='mode_val' style='color:#007BFF; font-weight:bold;'>大盘待机模式</span>";
  html += "</div>";
  
  // 渲染得分卡片
  for(int i = 0; i < 4; i++) {
    html += "<div class='score-box' onclick='openScoreModal(" + String(i) + ")'>";
    html += "<div class='score-num-container'>";
    html += "  <span id='riichi_box_" + String(i) + "'></span>"; 
    html += "  <span class='score-num' id='score_" + String(i) + "'>25000</span>";
    html += "</div>";
    html += "<span style='font-size:18px; font-weight:bold;'>P" + String(i+1) + "</span>" + getPlayerName(i) + "<br>";
    html += "<span class='rank-num' id='rank_" + String(i) + "'>第 1 名</span>";
    html += "</div>";
  }
  
  // 多媒体中心
  int initialPercent = (int)round((systemVolume / 30.0) * 100.0);
  html += "<div class='media-panel'>";
  html += "  <div style='text-align:left; font-weight:bold; color:#495057;'>🎛️ 多媒体中心</div>";
  html += "  <div class='media-flex'>";
  html += "    <button id='play_btn' class='media-btn' onclick='togglePlay()'>▶</button>";
  html += "    <div class='slider-container'>";
  html += "      <div style='font-size:13px; color:#666;'>核心功放音量: <span id='vol_percent' style='font-weight:bold; color:#007BFF;'>" + String(initialPercent) + "%</span></div>";
  html += "      <input id='vol_slider' type='range' class='volume-slider' min='0' max='30' value='" + String(systemVolume) + "' oninput='onSliderChange(this.value)'>";
  html += "    </div>";
  html += "  </div>";
  html += "</div>";
  
  // 裁判功能面板
  html += "<div class='control-panel'>";
  html += "  <div style='text-align:left; font-weight:bold; color:#495057;'>⚖️ 裁判功能远程接管区</div>";
  html += "  <div class='control-flex'>";
  html += "    <button id='btn_transfer' class='control-btn' onclick=\"triggerMode('transfer')\">💸 进入转账结算</button>";
  html += "    <button id='btn_manual' class='control-btn' onclick=\"triggerMode('manual')\">🔧 手动微调加分</button>";
  html += "  </div>";
  html += "</div>";
  
  // 💥 🀄 3. 局势换届与参数修正控制台 (全面重组局数与立直Pool并排微调区)
  html += "<div class='setup-panel'>";
  html += "  <div style='text-align:left; font-weight:bold; color:#495057;'>🎚️ 局势换届与参数修正</div>";
  // 庄位秒切行 (已带onclick弹窗盾牌)
  html += "  <div style='text-align:left; font-size:12px; color:#666; margin-top:5px;'>强制指定当前亲家(庄位):</div>";
  html += "  <div class='setup-flex'>";
  html += "    <button id='oya_btn_0' class='setup-sub-btn' onclick='setOya(0)'>P1 东</button>";
  html += "    <button id='oya_btn_1' class='setup-sub-btn' onclick='setOya(1)'>P2 南</button>";
  html += "    <button id='oya_btn_2' class='setup-sub-btn' onclick='setOya(2)'>P3 西</button>";
  html += "    <button id='oya_btn_3' class='setup-sub-btn' onclick='setOya(3)'>P4 北</button>";
  html += "  </div>";
  
  // 局数微调行
  html += "  <div class='setup-flex' style='margin-top:12px; border-top:1px dashed #eee; padding-top:8px;'>";
  html += "    <div style='font-size:13px; color:#495057; font-weight:bold;'>折线图手牌局数: <span id='hands_val' style='color:#fd7e14;'>1</span> 局</div>";
  html += "    <div style='display:flex; gap:6px;'>";
  html += "      <button class='setup-sub-btn' style='padding:5px 15px; font-size:16px;' onclick=\"adjustHandCount('sub')\">-</button>";
  html += "      <button class='setup-sub-btn' style='padding:5px 15px; font-size:16px; background:#fd7e14; color:white;' onclick=\"adjustHandCount('add')\">+</button>";
  html += "    </div>";
  html += "  </div>";
  
  // 💥💥 核心新增：立直公积金池无线微调行（死死锁在局数正下方）
  html += "  <div class='setup-flex' style='margin-top:8px; border-top:1px dashed #eee; padding-top:8px;'>";
  html += "    <div style='font-size:13px; color:#495057; font-weight:bold;'>场面滞留立直棒: <span id='pool_val' style='color:#dc3545;'>0</span> 根</div>";
  html += "    <div style='display:flex; gap:6px;'>";
  html += "      <button class='setup-sub-btn' style='padding:5px 15px; font-size:16px;' onclick=\"adjustRiichiPool('sub')\">-</button>";
  html += "      <button class='setup-sub-btn' style='padding:5px 15px; font-size:16px; background:#dc3545; color:white;' onclick=\"adjustRiichiPool('add')\">+</button>";
  html += "    </div>";
  html += "  </div>";
  html += "</div>";
  
  html += "<hr style='border:0; border-top:1px solid #ddd; margin:15px 0;'>";
  html += "<button class='btn' style='background:#28a745;' onclick=\"switchView('chart')\">分数走势折线图</button>";
  html += "<button class='btn' style='background:#dc3545;' onclick=\"if(confirm('确定强行荒牌过庄流局吗？')){fetch('/force_liuju');}\">流局</button>";
  html += "</div>";
  
  // 弹出模态框
  html += "<div id='score_modal' class='modal'>";
  html += "  <div class='modal-content'>";
  html += "    <h3 id='modal_title' style='margin:0; color:#333;'>接管调分</h3>";
  html += "    <div style='font-size:12px; color:#777; margin-top:5px;'>请输入需要增减的绝对点数值 (如: 8000)</div>";
  html += "    <input id='score_input' type='number' pattern='[0-9]*' inputmode='numeric' class='modal-input' placeholder='点数'>";
  html += "    <div class='modal-btn-group'>";
  html += "      <button class='m-btn' style='background:#28a745;' onclick=\"submitScoreAdjustment('add')\">➕ 强行加分</button>";
  html += "      <button class='m-btn' style='background:#dc3545;' onclick=\"submitScoreAdjustment('sub')\">➖ 强行减分</button>";
  html += "    </div>";
  __attribute__((unused)) html += "    <button class='btn' style='background:#6c757d; padding:8px; font-size:14px; margin-top:15px; width:40%;' onclick='closeModal()'>取消</button>";
  html += "  </div>";
  html += "</div>";
  
  // 图表
  html += "<div id='chart_view' class='chart-container'>";
  html += "<h2>分数动态曲线</h2>";
  html += "<canvas id='canvas_history' style='width:100%; max-height:350px;'></canvas>"; 
  html += "<p style='font-size:12px; color:#999; text-align:left; padding:0 10px;'>提示：点击上方图例可隐藏玩家线条，手指按住节点可查看详细局末结算分。</p>";
  html += "<button class='btn' style='background:#6c757d; margin-top:15px;' onclick=\"switchView('main')\">返回计分板</button>";
  html += "</div>";
  
  html += "</body></html>";
  return html;
}

void initWiFiAndWeb() {
  Serial.println("\n[🌐 WiFi 引擎] 正在尝试连接路由器: " + String(wifi_ssid));
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  
  Serial.println("\n========================================================");
  Serial.println("[✅ WiFi 成功] 智能计分器无线最高操控后台部署成功！");
  Serial.println("👉 网页端最高入口: http://" + WiFi.localIP().toString() + "/");
  Serial.println("========================================================");

  server.on("/", []() { server.send(200, "text/html", getPageHTML()); });

  // 0.5秒刷盘API
  server.on("/api/get_scores", []() {
    String json = "{";
    json += "\"renchan\":" + String(renchanCounter) + ",";
    json += "\"is_playing\":" + String(isRiichiBGMActive ? "true" : "false") + ",";
    json += "\"mode_id\":" + String((int)currentMode) + ","; 
    json += "\"mode_str\":\"" + getModeName(currentMode) + "\","; 
    json += "\"oya_idx\":" + String(currentDealer) + ","; 
    json += "\"total_hands\":" + String(totalHandsRecorded) + ","; 
    json += "\"pool_sticks\":" + String(riichiPool) + ","; // 💥 核心新增：向手机端实时喂入当前立直池棒数
    json += "\"scores\":[" + String(scores[0]) + "," + String(scores[1]) + "," + String(scores[2]) + "," + String(scores[3]) + "],";
    json += "\"ranks\":[" + String(ranks[0]) + "," + String(ranks[1]) + "," + String(ranks[2]) + "," + String(ranks[3]) + "],";
    json += "\"riichi_states\":[" ;
    for(int s=0; s<4; s++) { json += (hasRiichi[s] ? "true" : "false"); if(s < 3) json += ","; }
    json += "]";
    json += "}";
    server.send(200, "application/json", json); 
  });

  // 💥 核心安全升级点：无线裁判变更当前亲家庄位 API（配合前端进行强制换庄干预）
  server.on("/api/set_oya", []() {
    if (server.hasArg("idx")) {
      int targetOya = server.arg("idx").toInt();
      if (targetOya >= 0 && targetOya < 4) {
        currentDealer = targetOya; 
        updateOyaLeds();           
        resetAllRiichi();          
        updateAllSystem();         
        Serial.println("[🌐 无线大裁判] 最高干预！当前坐庄玩家已被隔空强切为: P" + String(currentDealer + 1));
      }
    }
    server.send(200, "text/plain", "OK");
  });

  // 💥💥 核心新增：无线裁判隔空强调【立直公积金池】API 路由控制线
  server.on("/api/adjust_pool", []() {
    if (server.hasArg("action")) {
      String action = server.arg("action");
      if (action == "add") {
        riichiPool++; // 立直棒无线累加一根
      } else if (action == "sub") {
        if (riichiPool > 0) riichiPool--; // 保护边界，绝对不跌破 0 变成负数呆账
      }
      
      // 💥 触发全场大盘同步。因为立直棒增减会导致账面发生变动（10万大平账安全灯会心领神会地熄灭或复燃）！
      updateAllSystem(); 
      Serial.println("[🌐 无线大裁判] 场面留存立直棒池完成远程修正！当前公积金存量: " + String(riichiPool) + " 根");
    }
    server.send(200, "text/plain", "OK");
  });

  // 无线局数修正
  server.on("/api/adjust_hands", []() {
    if (server.hasArg("action")) {
      String action = server.arg("action");
      if (action == "add") { if (totalHandsRecorded < 32) totalHandsRecorded++; } 
      else if (action == "sub") { if (totalHandsRecorded > 1) totalHandsRecorded--; }
      Serial.println("[🌐 无线大裁判] 手牌走势局数完成远程修正: " + String(totalHandsRecorded));
    }
    server.send(200, "text/plain", "OK");
  });

  // 无线隔空精确调分
  server.on("/api/adjust_score", []() {
    if (server.hasArg("player") && server.hasArg("amount") && server.hasArg("type")) {
      int p = server.arg("player").toInt();
      long amt = server.arg("amount").toInt();
      String type = server.arg("type");
      if (p >= 0 && p < 4 && amt > 0) {
        if (type == "add")      scores[p] += amt;
        else if (type == "sub") scores[p] -= amt;
        updateAllSystem(); 
      }
    }
    server.send(200, "text/plain", "OK");
  });

  // 模式硬干预
  server.on("/api/set_mode", []() {
    if (server.hasArg("type")) {
      String type = server.arg("type");
      if (type == "transfer")       currentMode = (currentMode == MODE_TRANSFER) ? MODE_IDLE : MODE_TRANSFER;
      else if (type == "manual")    currentMode = (currentMode == MODE_MANUAL_ADD) ? MODE_IDLE : MODE_MANUAL_ADD;
      for(int i=0; i<4; i++) { extern bool isLoserSelected[4]; isLoserSelected[i] = false; }
      extern String inputBuffer; inputBuffer = "";
      updateAllSystem(); 
    }
    server.send(200, "text/plain", "OK");
  });

  // 历史打点数据
  server.on("/api/get_history", []() {
    String json = "{";
    json += "\"labels\":[";
    for(int i = 0; i < totalHandsRecorded; i++) {
      if(i == 0) json += "\"局首状态\"";
      else json += "\"第" + String(i) + "局结算\"";
      if(i < totalHandsRecorded - 1) json += ",";
    }
    json += "],";
    for(int p = 0; p < 4; p++) {
      json += "\"p" + String(p+1) + "\":[";
      for(int i = 0; i < totalHandsRecorded; i++) { json += String(scoreHistory[i][p]); if(i < totalHandsRecorded - 1) json += ","; }
      json += "],";
    }
    json += "\"dealers\":[";
    for(int i = 0; i < totalHandsRecorded; i++) { json += String(dealerHistory[i] + 1); if(i < totalHandsRecorded - 1) json += ","; }
    json += "]";
    json += "}";
    server.send(200, "application/json", json); 
  });

  // 调音
  server.on("/api/set_volume", []() {
    if (server.hasArg("val")) {
      int targetVol = server.arg("val").toInt();
      if (targetVol >= 0 && targetVol <= 30) {
        systemVolume = targetVol;
        if (systemVolume == 0) { isRiichiBGMActive = false; sendMP3Command(0x0E, 0x00, 0x00); } 
        else { sendMP3Command(0x06, 0x00, (uint8_t)systemVolume); }
      }
    }
    server.send(200, "text/plain", "OK");
  });

  // 播放翻转
  server.on("/bgm_toggle", []() {
    if (isRiichiBGMActive) {
      isRiichiBGMActive = false; sendMP3Command(0x0E, 0x00, 0x00); 
    } else {
      if (systemVolume == 0) { systemVolume = 6; sendMP3Command(0x06, 0x00, (uint8_t)systemVolume); }
      playFileInFolder(1, 2); delay(50); loopCurrentTrack(); isRiichiBGMActive = true;
    }
    server.send(200, "text/plain", "OK");
  });

  // 流局过庄
  server.on("/force_liuju", []() {
    renchanCounter = 0; currentDealer = (currentDealer + 1) % 4; 
    updateOyaLeds(); resetAllRiichi(); updateAllSystem();
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

void handleWiFiClient() { server.handleClient(); }
#endif