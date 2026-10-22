// Tab5 おうちパネル デモ版
// 家電のお手入れと消耗品の予定を、期日の近い順に並べて「やった」「開封」「届いた」で記録する。
// 本番版（自宅PCの台帳サーバーと Wi-Fi でつながる）の画面と操作を、本体だけで体験できるようにしたもの。
// データは架空で、記録は保存しない（電源を切るか「最初から」で元に戻る）。
#include <Arduino.h>
#include <M5Unified.h>
#include <algorithm>
#include <vector>

static const char* PROJECT_URL = "https://sin1.studio/Tab5HomePanelDemo/";

static constexpr uint32_t DIM_MS = 3 * 60 * 1000;  // 触らなければ暗くする
static constexpr uint8_t BRIGHT = 180, DIM = 20;

// 色
static constexpr uint32_t C_BG = 0xF4F1EA, C_CARD = 0xFFFFFF, C_TEXT = 0x26231F, C_SUB = 0x7A746B,
                          C_LINE = 0xDDD7CC, C_OVER = 0xD9534F, C_TODAY = 0xE8892B, C_SOON = 0xE6B422,
                          C_OK = 0x8FB08A, C_BTN = 0x2F6F62, C_BTN2 = 0x4E7FA8, C_BTN_TEXT = 0xFFFFFF,
                          C_PALE = 0xE4DED3, C_MUTED = 0xB8B1A6, C_DEMO = 0xC2563A;

// ---- 日付（デモ開始日 = ビルドした日。日数は 1970-01-01 からの通し番号） ----

static int daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = (unsigned)(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + (int)doe - 719468;
}

static void civilFromDays(int z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = (unsigned)(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = (int)yoe + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp + (mp < 10 ? 3 : -9);
  y += m <= 2;
}

static int buildDay() {  // __DATE__ = "Oct 22 2026"
  static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
  char mon[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  unsigned m = (unsigned)((strstr(months, mon) - months) / 3 + 1);
  return daysFromCivil(atoi(__DATE__ + 7), m, (unsigned)atoi(__DATE__ + 4));
}

static String mdText(int day) {
  int y;
  unsigned m, d;
  civilFromDays(day, y, m, d);
  static const char* wd[] = {"木", "金", "土", "日", "月", "火", "水"};  // 1970-01-01 は木曜
  return String(m) + "/" + String(d) + "（" + wd[((day % 7) + 7) % 7] + "）";
}

// ---- 架空の台帳 ----

struct Consumable {
  const char* name;
  int stock, minStock, lifetime, lead;
  std::vector<int> opened;  // 開封した日（相対日。0 = デモ開始日）
};

struct Care {
  const char* appliance;
  const char* task;
  int every, lastDone;
  int uses;  // 使う消耗品の番号（-1 = なし）
};

static std::vector<Consumable> consumables;
static std::vector<Care> cares;

static void resetLedger() {
  consumables = {
      {"食洗機用洗剤", 1, 1, 60, 7, {-50}},
      {"洗濯槽クリーナー", 1, 0, 30, 7, {-24}},
      {"浄水器カートリッジ", 2, 1, 90, 10, {-200, -110, -20}},
      {"トイレットペーパー", 1, 1, 14, 3, {-38, -24, -10}},
  };
  cares = {
      {"洗濯機", "糸くずフィルター掃除", 7, -5, -1},
      {"洗濯機", "槽洗浄", 30, -31, 1},
      {"エアコン", "フィルター掃除", 14, -14, -1},
      {"オーブンレンジ", "庫内の拭き掃除", 7, -1, -1},
      {"食洗機", "残さいフィルター掃除", 7, -3, -1},
      {"掃除機", "ダストボックス洗い", 30, -10, -1},
  };
}

// 本番の predict.py と同じ考え方:
//   ペース = 直近3回の開封間隔の中央値（履歴2件未満は目安の日数）
//   補充が必要な日 = 最後に開封した日 + ペース × (在庫 − 最低在庫 + 1)、通知日 = その lead 日前
static int paceDays(const Consumable& c) {
  if (c.opened.size() < 2) return c.lifetime;
  std::vector<int> gaps;
  size_t from = c.opened.size() > 4 ? c.opened.size() - 4 : 0;
  for (size_t i = from + 1; i < c.opened.size(); i++) gaps.push_back(c.opened[i] - c.opened[i - 1]);
  std::sort(gaps.begin(), gaps.end());
  size_t n = gaps.size();
  return n % 2 ? gaps[n / 2] : (gaps[n / 2 - 1] + gaps[n / 2]) / 2;
}

static int needDay(const Consumable& c) {
  return c.opened.back() + paceDays(c) * std::max(c.stock - c.minStock + 1, 0);
}

// ---- 画面の行 ----

struct Row {
  bool care;
  int index;  // cares / consumables の番号
  int due;    // 通知日（相対日）
  String title, sub;
};

static std::vector<Row> rows;
static int today = 0;  // 「日付を進める」で増える
static int baseDay = 0;

static void rebuildRows() {
  rows.clear();
  for (int i = 0; i < (int)cares.size(); i++) {
    const Care& c = cares[i];
    String sub = c.task;
    if (c.uses >= 0) sub += String("（") + consumables[c.uses].name + "を使う）";
    rows.push_back({true, i, c.lastDone + c.every, c.appliance, sub});
  }
  for (int i = 0; i < (int)consumables.size(); i++) {
    const Consumable& c = consumables[i];
    int need = needDay(c);
    rows.push_back({false, i, need - c.lead, c.name,
                    "在庫 " + String(c.stock) + "・補充目安 " + mdText(baseDay + need) + "・ペース " +
                        String(paceDays(c)) + "日"});
  }
  std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.due < b.due; });
}

// ---- UI 状態 ----

enum class Act { None, Care, Open, Arrive, PrevPage, NextPage, Confirm, Cancel, QtyMinus, QtyPlus,
                 Day1, Day7, Reset, About, CloseAbout };

struct Button {
  int x, y, w, h;
  Act act;
  int index;
};

static M5Canvas canvas(&M5.Display);
static std::vector<Button> buttons;
static int page = 0;
static Act pending = Act::None;
static int pendingRow = -1, pendingQty = 1;
static bool showAbout = false;
static String toast;
static uint32_t toastUntil = 0;
static bool toastAlert = false;
static uint32_t lastTouch = 0;
static bool dimmed = false;

static void showToast(const String& text, bool alert, uint32_t ms = 5000) {
  toast = text;
  toastAlert = alert;
  toastUntil = millis() + ms;
}

static String state(int days) {
  if (days < 0) return "over";
  if (days == 0) return "today";
  if (days <= 3) return "soon";
  return "ok";
}

static uint32_t stateColor(const String& s) {
  if (s == "over") return C_OVER;
  if (s == "today") return C_TODAY;
  if (s == "soon") return C_SOON;
  return C_OK;
}

static String dueText(int days) {
  if (days < 0) return String(-days) + "日過ぎ";
  if (days == 0) return "今日";
  if (days == 1) return "明日";
  return "あと" + String(days) + "日";
}

// ---- 描画 ----

static void addButton(int x, int y, int w, int h, const char* label, Act act, int index,
                      uint32_t bg = C_BTN, uint32_t fg = C_BTN_TEXT, const lgfx::IFont* font = &fonts::lgfxJapanGothic_28) {
  canvas.fillRoundRect(x, y, w, h, 14, bg);
  canvas.setFont(font);
  canvas.setTextColor(fg);
  canvas.setTextDatum(middle_center);
  canvas.drawString(label, x + w / 2, y + h / 2);
  buttons.push_back({x, y, w, h, act, index});
}

// 横幅に収まらない文字列は末尾を「…」にする
static String fit(const String& s, int maxW) {
  if (canvas.textWidth(s) <= maxW) return s;
  String t = s;
  while (t.length() > 0 && canvas.textWidth(t + "…") > maxW) {
    int cut = t.length() - 1;
    while (cut > 0 && (t[cut] & 0xC0) == 0x80) cut--;  // UTF-8 の途中で切らない
    t = t.substring(0, cut);
  }
  return t + "…";
}

static void drawHeader(int W) {
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(C_TEXT);
  canvas.setFont(&fonts::lgfxJapanGothic_40);
  canvas.drawString("おうちのお手入れ", 28, 44);
  int tx = 28 + canvas.textWidth("おうちのお手入れ") + 16;
  canvas.fillRoundRect(tx, 24, 96, 40, 10, C_DEMO);
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  canvas.setTextColor(0xFFFFFF);
  canvas.setTextDatum(middle_center);
  canvas.drawString("デモ", tx + 48, 44);

  canvas.setTextDatum(middle_left);
  canvas.setFont(&fonts::lgfxJapanGothic_28);
  canvas.setTextColor(C_SUB);
  String d = "今日 " + mdText(baseDay + today);
  if (today > 0) d += "  +" + String(today) + "日";
  canvas.drawString(d, tx + 120, 44);

  addButton(W - 28 - 200, 14, 200, 60, "？ 説明", Act::About, -1, C_PALE, C_TEXT);
  canvas.drawFastHLine(0, 88, W, C_LINE);
}

static void drawRow(const Row& r, int rowIndex, int x, int y, int w, int h) {
  int days = r.due - today;
  String st = state(days);
  canvas.fillRoundRect(x, y, w, h, 16, C_CARD);
  canvas.fillRoundRect(x, y, 14, h, 7, stateColor(st));

  int btnW = 160, btnH = h - 32, gap = 14;
  int btnArea = r.care ? btnW : btnW * 2 + gap;
  int dueX = x + w - btnArea - 40;
  int textRight = dueX - 200;

  canvas.setTextDatum(top_left);
  canvas.setTextColor(C_TEXT);
  canvas.setFont(&fonts::lgfxJapanGothic_32);
  canvas.drawString(fit(r.title, textRight - (x + 40)), x + 40, y + 16);
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  canvas.setTextColor(C_SUB);
  canvas.drawString(fit(r.sub, textRight - (x + 40)), x + 40, y + h - 40);

  canvas.setTextDatum(middle_right);
  canvas.setFont(&fonts::lgfxJapanGothic_32);
  canvas.setTextColor(st == "ok" ? C_TEXT : stateColor(st));
  canvas.drawString(dueText(days), dueX, y + h / 2 - 15);
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  canvas.setTextColor(C_SUB);
  canvas.drawString(String(r.care ? "期日 " : "通知 ") + mdText(baseDay + r.due), dueX, y + h / 2 + 20);

  int bx = x + w - btnArea - 20, by = y + 16;
  if (r.care) {
    addButton(bx, by, btnW, btnH, "やった", Act::Care, rowIndex);
  } else {
    addButton(bx, by, btnW, btnH, "開封", Act::Open, rowIndex);
    addButton(bx + btnW + gap, by, btnW, btnH, "届いた", Act::Arrive, rowIndex, C_BTN2);
  }
}

static void drawFooter(int W, int H, int pages) {
  int y = H - 92;
  canvas.drawFastHLine(0, y, W, C_LINE);
  // 日付を進めるボタン（デモ専用。本番版はここが「更新」）
  addButton(28, y + 14, 120, 64, "+1日", Act::Day1, -1, C_PALE, C_TEXT);
  addButton(160, y + 14, 120, 64, "+7日", Act::Day7, -1, C_PALE, C_TEXT);
  addButton(292, y + 14, 156, 64, "最初から", Act::Reset, -1, C_PALE, C_TEXT, &fonts::lgfxJapanGothic_24);
  canvas.setTextDatum(middle_left);
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  canvas.setTextColor(C_SUB);
  int over = 0, soon = 0;
  for (const Row& r : rows) {
    int d = r.due - today;
    over += d <= 0;
    soon += d > 0 && d <= 3;
  }
  canvas.drawString("期限切れ・今日 " + String(over) + "件　3日以内 " + String(soon) + "件", 472, y + 46);
  if (pages > 1) {
    addButton(W - 360, y + 14, 120, 64, "＜", Act::PrevPage, -1, page > 0 ? C_BTN : C_MUTED);
    canvas.setTextDatum(middle_center);
    canvas.setFont(&fonts::lgfxJapanGothic_28);
    canvas.setTextColor(C_TEXT);
    canvas.drawString(String(page + 1) + "/" + String(pages), W - 190, y + 46);
    addButton(W - 148, y + 14, 120, 64, "＞", Act::NextPage, -1, page < pages - 1 ? C_BTN : C_MUTED);
  }
}

static void drawConfirm(int W, int H) {
  canvas.fillRect(0, 0, W, H, 0x000000);
  int w = std::min(W - 80, 900), h = 400, x = (W - w) / 2, y = (H - h) / 2;
  canvas.fillRoundRect(x, y, w, h, 24, C_CARD);
  const Row& r = rows[pendingRow];
  const char* verb = pending == Act::Care ? "やった" : pending == Act::Open ? "開封した" : "届いた";
  canvas.setTextDatum(top_center);
  canvas.setTextColor(C_TEXT);
  canvas.setFont(&fonts::lgfxJapanGothic_36);
  canvas.drawString(fit(r.title, w - 60), W / 2, y + 40);
  canvas.setFont(&fonts::lgfxJapanGothic_28);
  canvas.setTextColor(C_SUB);
  canvas.drawString(fit(r.care ? r.sub : String("在庫 ") + consumables[r.index].stock, w - 60), W / 2, y + 96);
  canvas.setTextColor(C_TEXT);
  canvas.setFont(&fonts::lgfxJapanGothic_32);
  if (pending == Act::Care) {
    canvas.drawString(String("今日「") + verb + "」と記録しますか？", W / 2, y + 160);
  } else {
    canvas.drawString(String(verb) + "数", W / 2, y + 150);
    addButton(W / 2 - 200, y + 190, 100, 70, "−", Act::QtyMinus, -1, C_PALE, C_TEXT);
    canvas.setTextDatum(middle_center);
    canvas.setFont(&fonts::lgfxJapanGothic_40);
    canvas.setTextColor(C_TEXT);
    canvas.drawString(String(pendingQty), W / 2, y + 225);
    addButton(W / 2 + 100, y + 190, 100, 70, "＋", Act::QtyPlus, -1, C_PALE, C_TEXT);
  }
  addButton(x + 40, y + h - 110, w / 2 - 60, 80, "やめる", Act::Cancel, -1, C_PALE, C_TEXT);
  addButton(x + w / 2 + 20, y + h - 110, w / 2 - 60, 80, "記録する", Act::Confirm, -1);
}

static void drawAbout(int W, int H) {
  canvas.fillRect(0, 0, W, H, 0x000000);
  int w = W - 120, h = H - 100, x = 60, y = 50;
  canvas.fillRoundRect(x, y, w, h, 24, C_CARD);
  int qr = 300, qx = x + w - qr - 50, qy = y + 70;
  canvas.qrcode(PROJECT_URL, qx, qy, qr);
  canvas.setTextDatum(top_center);
  canvas.setFont(&fonts::lgfxJapanGothic_20);
  canvas.setTextColor(C_SUB);
  canvas.drawString("書き込みページ・説明", qx + qr / 2, qy + qr + 14);

  canvas.setTextDatum(top_left);
  canvas.setTextColor(C_TEXT);
  canvas.setFont(&fonts::lgfxJapanGothic_36);
  canvas.drawString("このパネルについて", x + 50, y + 50);
  static const char* lines[] = {
      "家電のお手入れと消耗品の予定を、期日の近い順に並べます。",
      "終わったら「やった」、開けたら「開封」、買ったら「届いた」。",
      "消耗品は開封の間隔から、次に買う日を見積もります。",
      "",
      "本番版は Wi-Fi で自宅PCの台帳とつながり、記録すると",
      "AI アシスタント（xangi）が通知の予約を入れ直します。",
      "このデモは本体だけで動き、データは架空です。",
      "",
      "「+1日」「+7日」で日付を進めると、色と並びが変わります。",
  };
  canvas.setFont(&fonts::lgfxJapanGothic_24);
  int ty = y + 120;
  for (const char* line : lines) {
    canvas.drawString(line, x + 50, ty);
    ty += 40;
  }
  canvas.setTextColor(C_SUB);
  canvas.drawString("色：", x + 50, ty + 10);
  struct { uint32_t c; const char* t; } legend[] = {{C_OVER, "期限切れ"}, {C_TODAY, "今日"}, {C_SOON, "3日以内"}, {C_OK, "まだ先"}};
  int lx = x + 110;
  for (auto& l : legend) {
    canvas.fillRoundRect(lx, ty + 12, 28, 28, 6, l.c);
    canvas.setTextColor(C_TEXT);
    canvas.drawString(l.t, lx + 38, ty + 10);
    lx += 38 + canvas.textWidth(l.t) + 30;
  }
  addButton(x + w - 290, y + h - 110, 240, 76, "閉じる", Act::CloseAbout, -1);
}

static void drawToast(int W, int H) {
  if (!toast.length() || millis() > toastUntil) return;
  canvas.setFont(&fonts::lgfxJapanGothic_28);
  int lines = 1;
  for (char c : toast) lines += c == '\n';
  int h = 36 + lines * 40, w = W - 200, x = 100, y = H - 92 - h - 20;
  canvas.fillRoundRect(x, y, w, h, 18, toastAlert ? C_TODAY : C_TEXT);
  canvas.setTextColor(0xFFFFFF);
  canvas.setTextDatum(top_left);
  int ty = y + 18, start = 0;
  while (start <= (int)toast.length()) {
    int nl = toast.indexOf('\n', start);
    String line = toast.substring(start, nl < 0 ? toast.length() : nl);
    canvas.drawString(fit(line, w - 48), x + 24, ty);
    ty += 40;
    if (nl < 0) break;
    start = nl + 1;
  }
}

static void render() {
  const int W = canvas.width(), H = canvas.height();
  buttons.clear();
  canvas.fillScreen(C_BG);
  drawHeader(W);

  const int top = 104, bottom = H - 104, rowH = 112, rowGap = 14;
  int perPage = std::max(1, (bottom - top + rowGap) / (rowH + rowGap));
  int pages = std::max(1, (int)((rows.size() + perPage - 1) / perPage));
  page = std::min(page, pages - 1);
  for (int i = page * perPage, r = 0; i < (int)rows.size() && r < perPage; i++, r++) {
    drawRow(rows[i], i, 24, top + r * (rowH + rowGap), W - 48, rowH);
  }
  drawFooter(W, H, pages);
  if (showAbout) {
    buttons.clear();  // 重なった画面の後ろのボタンは押させない
    drawAbout(W, H);
  } else if (pending != Act::None && pendingRow >= 0 && pendingRow < (int)rows.size()) {
    buttons.clear();
    drawConfirm(W, H);
  }
  drawToast(W, H);
  canvas.pushSprite(0, 0);
}

// ---- 操作 ----

// 日付を進めて、新しく通知日を迎えた品を「お知らせ」として出す（本番では xangi が Discord に投稿する）
static void advance(int days) {
  std::vector<String> due;
  for (const Row& r : rows) {
    if (r.due > today && r.due <= today + days) due.push_back(r.care ? r.title + "：" + cares[r.index].task : r.title);
  }
  today += days;
  rebuildRows();
  page = 0;
  if (due.empty()) {
    showToast(String(days) + "日進めました。新しいお知らせはありません", false, 3000);
  } else {
    String msg = "お知らせ（" + String(due.size()) + "件）\n";
    for (size_t i = 0; i < due.size() && i < 3; i++) msg += due[i] + (i + 1 < std::min<size_t>(due.size(), 3) ? "\n" : "");
    if (due.size() > 3) msg += " ほか" + String(due.size() - 3) + "件";
    showToast(msg, true, 7000);
  }
}

static void record(Act act, int rowIndex, int qty) {
  const Row r = rows[rowIndex];
  String msg;
  if (act == Act::Care) {
    Care& c = cares[r.index];
    c.lastDone = today;
    msg = String(c.appliance) + "：" + c.task + " を記録\n次の期日 " + mdText(baseDay + today + c.every);
    if (c.uses >= 0) {
      Consumable& u = consumables[c.uses];
      u.stock = std::max(u.stock - 1, 0);
      u.opened.push_back(today);
      msg += String("　") + u.name + " を1つ使用（在庫 " + u.stock + "）";
    }
  } else {
    Consumable& c = consumables[r.index];
    int before = c.stock;
    if (act == Act::Open) {
      c.stock = std::max(c.stock - qty, 0);
      c.opened.push_back(today);
    } else {
      c.stock += qty;
    }
    msg = String(c.name) + (act == Act::Open ? " を開封" : " が到着") + "：在庫 " + before + "→" + c.stock +
          "\n次のお知らせ " + mdText(baseDay + needDay(c) - c.lead);
  }
  rebuildRows();
  showToast(msg, false, 6000);
}

static void handleTap(int tx, int ty) {
  for (const Button& b : buttons) {
    if (tx < b.x || tx >= b.x + b.w || ty < b.y || ty >= b.y + b.h) continue;
    switch (b.act) {
      case Act::Care:
      case Act::Open:
      case Act::Arrive:
        pending = b.act;
        pendingRow = b.index;
        pendingQty = 1;
        break;
      case Act::QtyMinus: pendingQty = std::max(1, pendingQty - 1); break;
      case Act::QtyPlus: pendingQty = std::min(99, pendingQty + 1); break;
      case Act::Cancel: pending = Act::None; break;
      case Act::Confirm: {
        Act act = pending;
        pending = Act::None;
        record(act, pendingRow, pendingQty);
        break;
      }
      case Act::Day1: advance(1); break;
      case Act::Day7: advance(7); break;
      case Act::Reset:
        today = 0;
        page = 0;
        resetLedger();
        rebuildRows();
        showToast("最初の状態に戻しました", false, 3000);
        break;
      case Act::About: showAbout = true; break;
      case Act::CloseAbout: showAbout = false; break;
      case Act::PrevPage: page = std::max(0, page - 1); break;
      case Act::NextPage: page++; break;
      default: break;
    }
    render();
    return;
  }
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  M5.Display.setRotation(M5.Display.width() < M5.Display.height() ? 1 : 0);  // 横置き
  M5.Display.setBrightness(BRIGHT);
  canvas.setPsram(true);
  canvas.setColorDepth(16);
  canvas.createSprite(M5.Display.width(), M5.Display.height());

  baseDay = buildDay();
  resetLedger();
  rebuildRows();
  showAbout = true;  // 最初に説明を出す
  lastTouch = millis();
  render();
}

void loop() {
  M5.update();
  uint32_t now = millis();

  auto t = M5.Touch.getDetail();
  if (t.wasPressed()) {
    lastTouch = now;
    if (dimmed) {  // 暗いときの最初のタッチは明るくするだけ
      dimmed = false;
      M5.Display.setBrightness(BRIGHT);
    } else {
      handleTap(t.x, t.y);
    }
  }
  if (!dimmed && now - lastTouch > DIM_MS) {
    dimmed = true;
    M5.Display.setBrightness(DIM);
  }
  static bool toastShown = false;
  bool visible = toast.length() && now <= toastUntil;
  if (toastShown && !visible) render();  // 時間切れのトーストを消す
  toastShown = visible;
  delay(20);
}
