/*
**    ▄▄▄▄  ▄▄▄  ▄▄▄▄                   
** ▀▀ ▀███  ███  ███▀            ▄▄     
** ██  ███  ███  ███ ▄███▄ ████▄ ██ ▄█▀ 
** ██  ███▄▄███▄▄███ ██ ██ ██ ▀▀ ████   
** ██▄  ▀████▀████▀  ▀███▀ ██    ██ ▀█▄ 
*/
#include "journal.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace {
// 自动获取当前时分秒字符串 [HH:MM:SS]
std::string getCurrentTimeString() {
  auto now = std::chrono::system_clock::now();
  auto in_time_t = std::chrono::system_clock::to_time_t(now);
  std::stringstream ss;
  ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
  return ss.str();
}
}  // namespace

Journal::Journal() {
  // 初始预置启动日志
  addLog("工作终端初始化完成，已连接工位传感器网络");
  addLog("地图碰撞网格与寻路引擎自检通过");
}

Journal::~Journal() {
  if (textureId_) {
    glDeleteTextures(1, &textureId_);
    textureId_ = 0;
  }
#ifdef _WIN32
  if (hFontTitle_) DeleteObject((HFONT)hFontTitle_);
  if (hFontText_) DeleteObject((HFONT)hFontText_);
  if (hbitmap_) DeleteObject((HBITMAP)hbitmap_);
  if (hdc_) DeleteDC((HDC)hdc_);
#endif
}

void Journal::resize(int pixelW, int pixelH, float dpiScale) {
  if (width_ == pixelW && height_ == pixelH && textureId_ != 0) {
    return;
  }
  width_ = pixelW;
  height_ = pixelH;
  dpiScale_ = dpiScale;

  buffer_.assign(width_ * height_ * 4, 0);

  if (textureId_ == 0) {
    glGenTextures(1, &textureId_);
  }
  glBindTexture(GL_TEXTURE_2D, textureId_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  // 开辟初始显存空间
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width_, height_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glBindTexture(GL_TEXTURE_2D, 0);

#ifdef _WIN32
  if (hdc_) DeleteDC((HDC)hdc_);
  if (hbitmap_) DeleteObject((HBITMAP)hbitmap_);
  HDC screenDC = GetDC(NULL);
  hdc_ = CreateCompatibleDC(screenDC);
  ReleaseDC(NULL, screenDC);

  BITMAPINFO bmi = {};
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = width_;
  bmi.bmiHeader.biHeight = -height_;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  hbitmap_ = CreateDIBSection((HDC)hdc_, &bmi, DIB_RGB_COLORS, &bits_, NULL, 0);
  SelectObject((HDC)hdc_, (HBITMAP)hbitmap_);

  if (!hFontTitle_) {
    hFontTitle_ = CreateFontW(static_cast<int>(18 * dpiScale_), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
    hFontText_ = CreateFontW(static_cast<int>(13 * dpiScale_), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei");
  }
#endif
}

void Journal::setCurrentTask(const std::string& title,
                             const std::string& detail,
                             const std::string& status,
                             float progress) {
  currentTitle_ = title;
  currentDetail_ = detail;
  currentStatus_ = status;
  currentProgress_ = std::clamp(progress, 0.0f, 1.0f);
}

void Journal::addLog(const std::string& content) {
  addLog(getCurrentTimeString(), content);
}

void Journal::addLog(const std::string& timeStr, const std::string& content) {
  logs_.push_back({timeStr, content});
  // 产生新日志时自动贴底滚动
  if (autoScrollToBottom_) {
    scrollOffset_ = maxScroll_;
  }
}

void Journal::scroll(float deltaY) {
  // deltaY 正值向上滚，负值向下滚
  scrollOffset_ -= deltaY * 30.0f * dpiScale_;
  scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll_);
  // 若用户往上翻阅，则暂停自动贴底；若滑到最底，则恢复自动贴底
  autoScrollToBottom_ = (scrollOffset_ >= maxScroll_ - 1.0f);
}

void Journal::update() {
  if (width_ <= 0 || height_ <= 0) return;

#ifdef __APPLE__
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(
    buffer_.data(), width_, height_, 8, width_ * 4,
    colorSpace, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big
  );

  // 1. 翻转坐标系为左上角原点，Y 轴向下
  CGContextTranslateCTM(ctx, 0.0f, static_cast<float>(height_));
  CGContextScaleCTM(ctx, 1.0f, -1.0f);

  // 全屏底色
  CGContextSetRGBFillColor(ctx, 0.06f, 0.07f, 0.09f, 1.0f);
  CGContextFillRect(ctx, CGRectMake(0, 0, width_, height_));

  auto drawString = [&](const std::string& text, float x, float y, float size,
                        float r, float g, float b, bool bold) {
    CFStringRef fontName = bold ? CFSTR("PingFangSC-Semibold") : CFSTR("PingFangSC-Regular");
    CTFontRef font = CTFontCreateWithName(fontName, size * dpiScale_, NULL);
    if (!font) font = CTFontCreateWithName(CFSTR("Helvetica-Bold"), size * dpiScale_, NULL);

    CGColorRef color = CGColorCreateGenericRGB(r, g, b, 1.0f);
    CFStringRef keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    CFTypeRef values[] = { font, color };
    CFDictionaryRef attr = CFDictionaryCreate(NULL, (const void**)keys, (const void**)values,
                                              2, &kCFCopyStringDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);

    CFStringRef cfText = CFStringCreateWithCString(NULL, text.c_str(), kCFStringEncodingUTF8);
    if (cfText) {
      CFAttributedStringRef attrStr = CFAttributedStringCreate(NULL, cfText, attr);
      CTLineRef line = CTLineCreateWithAttributedString(attrStr);

      // 反转文字矩阵以保证文字正向立正
      CGContextSetTextMatrix(ctx, CGAffineTransformMakeScale(1.0f, -1.0f));
      CGContextSetTextPosition(ctx, x * dpiScale_, y * dpiScale_);
      CTLineDraw(line, ctx);

      CFRelease(line);
      CFRelease(attrStr);
      CFRelease(cfText);
    }
    CFRelease(attr);
    CGColorRelease(color);
    if (font) CFRelease(font);
  };

  // =========================================================
  // 上部：正在干的事情 (Current Task Card)
  // =========================================================
  float pad = 16.0f * dpiScale_;
  float topCardH = 150.0f * dpiScale_;
  CGContextSetRGBFillColor(ctx, 0.11f, 0.13f, 0.17f, 1.0f);
  CGContextFillRect(ctx, CGRectMake(pad, 16.0f * dpiScale_, width_ - pad * 2, topCardH));

  drawString("【 正在进行的工作事项 】", 26, 44, 16, 0.35f, 0.75f, 1.0f, true);
  drawString(currentTitle_, 26, 76, 14, 1.0f, 0.85f, 0.3f, true);
  drawString(currentDetail_, 26, 104, 12, 0.75f, 0.78f, 0.84f, false);

  // 状态标签
  bool isWork = (currentStatus_.find("开发") != std::string::npos ||
                 currentStatus_.find("工作") != std::string::npos);
  if (isWork) {
    drawString(currentStatus_, 26, 130, 12, 0.2f, 0.85f, 0.45f, true);
  } else {
    drawString(currentStatus_, 26, 130, 12, 1.0f, 0.75f, 0.15f, true);
  }

  // 进度条槽底与填充
  float barX = 26.0f * dpiScale_;
  float barY = 146.0f * dpiScale_;
  float barW = width_ - barX * 2;
  CGContextSetRGBFillColor(ctx, 0.18f, 0.20f, 0.26f, 1.0f);
  CGContextFillRect(ctx, CGRectMake(barX, barY, barW, 6.0f * dpiScale_));

  if (currentProgress_ > 0.01f) {
    CGContextSetRGBFillColor(ctx, 0.2f, 0.85f, 0.45f, 1.0f);
    CGContextFillRect(ctx, CGRectMake(barX, barY, barW * currentProgress_, 6.0f * dpiScale_));
  }

  // =========================================================
  // 下部：干的事情的记录 (Scrollable Activity Logs)
  // =========================================================
  float logAreaY = 180.0f * dpiScale_;
  float logAreaH = height_ - logAreaY - 16.0f * dpiScale_;
  CGRect logRect = CGRectMake(pad, logAreaY, width_ - pad * 2, logAreaH);

  // 终端框深色底色
  CGContextSetRGBFillColor(ctx, 0.04f, 0.05f, 0.07f, 1.0f);
  CGContextFillRect(ctx, logRect);

  drawString(">> 工作历史日志流 (Activity Journal):", 26, 206, 13, 0.55f, 0.60f, 0.70f, true);

  // 计算可滚动区间并做裁剪 (Clip)
  float contentStartY = 236.0f * dpiScale_;
  float contentH = logAreaH - (contentStartY - logAreaY) - 10.0f * dpiScale_;
  CGRect clipRect = CGRectMake(pad, contentStartY - 16.0f * dpiScale_, width_ - pad * 2, contentH + 16.0f * dpiScale_);

  float lineSpacing = 28.0f * dpiScale_;
  float totalLogsH = logs_.size() * lineSpacing;
  maxScroll_ = std::max(0.0f, totalLogsH - contentH);

  if (autoScrollToBottom_) {
    scrollOffset_ = maxScroll_;
  }

  CGContextSaveGState(ctx);
  CGContextClipToRect(ctx, clipRect);

  float curY = contentStartY - scrollOffset_;
  for (const auto& item : logs_) {
    // 视锥剔除：超出视窗上下的文字不进行 CoreText 绘制
    if (curY >= contentStartY - 30.0f * dpiScale_ && curY <= contentStartY + contentH + 30.0f * dpiScale_) {
      // 时间戳 [HH:MM:SS]（蓝青色）
      std::string t = "[" + item.timeStr + "]";
      drawString(t, 26, curY / dpiScale_, 12, 0.38f, 0.65f, 0.95f, true);
      // 日志正文（亮白色）
      drawString(item.content, 98, curY / dpiScale_, 12, 0.92f, 0.93f, 0.96f, false);
    }
    curY += lineSpacing;
  }
  CGContextRestoreGState(ctx);

  // 绘制右侧滚动条指示器
  if (maxScroll_ > 0.0f) {
    float scrollRatio = scrollOffset_ / maxScroll_;
    float thumbH = std::max(20.0f * dpiScale_, contentH * (contentH / totalLogsH));
    float thumbY = (contentStartY - 10.0f * dpiScale_) + (contentH - thumbH) * scrollRatio;
    float thumbX = width_ - pad - 6.0f * dpiScale_;

    CGContextSetRGBFillColor(ctx, 0.30f, 0.35f, 0.45f, 0.75f);
    CGContextFillRect(ctx, CGRectMake(thumbX, thumbY, 3.0f * dpiScale_, thumbH));
  }

  CGContextRelease(ctx);
  CGColorSpaceRelease(colorSpace);

  // 增量更新纹理（绝不重复申请显存）
  glBindTexture(GL_TEXTURE_2D, textureId_);
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE, buffer_.data());
  glBindTexture(GL_TEXTURE_2D, 0);

#elif defined(_WIN32)
  if (!hdc_ || !bits_) return;
  // Windows GDI 兼容渲染逻辑...
#endif
}