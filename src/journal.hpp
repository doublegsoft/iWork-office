/*
**    ▄▄▄▄  ▄▄▄  ▄▄▄▄                   
** ▀▀ ▀███  ███  ███▀            ▄▄     
** ██  ███  ███  ███ ▄███▄ ████▄ ██ ▄█▀ 
** ██  ███▄▄███▄▄███ ██ ██ ██ ▀▀ ████   
** ██▄  ▀████▀████▀  ▀███▀ ██    ██ ▀█▄ 
*/
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glad/gl.h>

class Journal {
public:
  struct LogItem {
    std::string timeStr;
    std::string content;
  };

  Journal();
  ~Journal();

  // 根据窗口尺寸与 Retina 缩放比重设物理分辨率
  void resize(int pixelW, int pixelH, float dpiScale);

  // 设置顶部「正在干的事情」状态数据
  void setCurrentTask(const std::string& title,
                      const std::string& detail,
                      const std::string& status,
                      float progress);

  // 追加一条工作日志（自动生成当前 [HH:MM:SS] 时间戳）
  void addLog(const std::string& content);

  // 追加一条指定时间戳的日志
  void addLog(const std::string& timeStr, const std::string& content);

  // 鼠标滚轮滚动日志区域 (delta: 滚轮偏移量)
  void scroll(float deltaY);

  // 将内容光栅化渲染并上传至 GPU 纹理
  void update();

  GLuint textureId() const { return textureId_; }
  int width() const { return width_; }
  int height() const { return height_; }

private:
  int width_ = 0;
  int height_ = 0;
  float dpiScale_ = 1.0f;
  GLuint textureId_ = 0;
  std::vector<uint8_t> buffer_;

  // 顶部当前任务数据
  std::string currentTitle_ = "等待分配任务...";
  std::string currentDetail_ = "系统就绪，待命状态";
  std::string currentStatus_ = "[ 待命中 ]";
  float currentProgress_ = 0.0f;

  // 底部活动日志列表
  std::vector<LogItem> logs_;
  float scrollOffset_ = 0.0f;
  float maxScroll_ = 0.0f;
  bool autoScrollToBottom_ = true;

#ifdef _WIN32
  void* hdc_ = nullptr;
  void* hbitmap_ = nullptr;
  void* hFontTitle_ = nullptr;
  void* hFontText_ = nullptr;
  void* bits_ = nullptr;
#endif
};