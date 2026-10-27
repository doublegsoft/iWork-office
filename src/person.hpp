/*
**    ▄▄▄▄  ▄▄▄  ▄▄▄▄                   
** ▀▀ ▀███  ███  ███▀            ▄▄     
** ██  ███  ███  ███ ▄███▄ ████▄ ██ ▄█▀ 
** ██  ███▄▄███▄▄███ ██ ██ ██ ▀▀ ████   
** ██▄  ▀████▀████▀  ▀███▀ ██    ██ ▀█▄ 
*/
#pragma once

#include <cmath>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <glad/gl.h>

class Person {
public:
  enum State {
    STATE_WALKING,
    STATE_WORKING
  };

  struct WorkTask {
    std::wstring title;
    std::wstring step1;
    std::wstring step2;
    std::wstring result;
  };

  Person(int startTileX, int startTileY, int tileSize);
  ~Person() = default;

  // 初始化地图可行走网格与工位列表
  void init(const std::vector<std::vector<bool>>& walkableGrid,
            const std::vector<std::pair<int, int>>& seats);

  // 状态机更新与寻路移动
  void update(float dt);

  // 绘制大号实体小人
  void draw(GLuint vbo, GLint uColorTintLoc, float time);

  // Getter 接口（供 UI 监控大屏和主相机调用）
  float worldX() const { return worldX_; }
  float worldY() const { return worldY_; }
  int tileX() const { return currentTileX_; }
  int tileY() const { return currentTileY_; }
  State state() const { return state_; }
  float workTimer() const { return workTimer_; }
  float workDuration() const { return workDuration_; }
  float workProgress() const;
  const WorkTask& currentTask() const;

private:
  std::vector<std::pair<int, int>> findPath(int startX, int startY, int targetX, int targetY);
  void drawColorRect(GLuint vbo, GLint uColorTintLoc, float x, float y, float w, float h,
                     float r, float g, float b, float a = 1.0f);

  int tileSize_ = 48;
  float worldX_ = 0.0f;
  float worldY_ = 0.0f;
  int currentTileX_ = 0;
  int currentTileY_ = 0;
  float facingX_ = 0.0f;

  State state_ = STATE_WORKING;
  float workTimer_ = 0.0f;
  const float workDuration_ = 7.0f;
  float moveSpeed_ = 100.0f;

  std::vector<WorkTask> tasks_;
  size_t currentTaskIndex_ = 0;

  std::vector<std::pair<int, int>> seats_;
  std::vector<std::vector<bool>> walkableGrid_;
  std::vector<std::pair<int, int>> path_;
  size_t pathIndex_ = 0;
};