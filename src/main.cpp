/*
**    ▄▄▄▄  ▄▄▄  ▄▄▄▄                   
** ▀▀ ▀███  ███  ███▀            ▄▄     
** ██  ███  ███  ███ ▄███▄ ████▄ ██ ▄█▀ 
** ██  ███▄▄███▄▄███ ██ ██ ██ ▀▀ ████   
** ██▄  ▀████▀████▀  ▀███▀ ██    ██ ▀█▄ 
*/
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include "stb_image.h"
#include <nlohmann/json.hpp>
#include "office.hpp"
#include "person.hpp"
#include "journal.hpp"

namespace {
struct Vertex {
  float x;
  float y;
  float u;
  float v;
};

struct Rect {
  float x;
  float y;
  float w;
  float h;
};

// 窗口与布局参数
const int g_windowInitW = 1280;
const int g_windowInitH = 720;
const int g_uiPanelLogicalW = 380; // 右侧紧凑型日志终端宽度 (30% 屏幕)

int g_tileSize = 48;
int g_gridW = 27;
int g_gridH = 20;
int g_internalW = g_tileSize * g_gridW;
int g_internalH = g_tileSize * g_gridH;

struct FramebufferSize {
  int w = g_windowInitW;
  int h = g_windowInitH;
};
FramebufferSize g_fb;

// 全局日志对象指针（供鼠标滚轮事件回调访问）
Journal* g_journalPtr = nullptr;

void framebufferSizeCallback(GLFWwindow* window, int w, int h) {
  g_fb.w = w;
  g_fb.h = h;
}

// 鼠标滚轮滚动事件回调
void scrollCallback(GLFWwindow* window, double xoffset, double yoffset) {
  if (g_journalPtr) {
    g_journalPtr->scroll(static_cast<float>(yoffset));
  }
}

// 辅助转换：wstring 转 UTF-8 string
std::string toUtf8(const std::wstring& wstr) {
  std::string out;
  for (wchar_t wc : wstr) {
    if (wc < 0x80) {
      out.push_back(static_cast<char>(wc));
    } else if (wc < 0x800) {
      out.push_back(static_cast<char>(0xC0 | ((wc >> 6) & 0x1F)));
      out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
    } else {
      out.push_back(static_cast<char>(0xE0 | ((wc >> 12) & 0x0F)));
      out.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
      out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
    }
  }
  return out;
}

// -------------------------------------------------------------
// 读取地图碰撞并初始化网格
// -------------------------------------------------------------
std::vector<std::vector<bool>> loadWalkableGrid(const std::string& jsonPath) {
  std::vector<std::vector<bool>> walkable(g_gridH, std::vector<bool>(g_gridW, true));
  std::ifstream in(jsonPath);
  if (!in) {
    return walkable;
  }
  nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
  if (doc.is_discarded()) {
    return walkable;
  }

  std::vector<Rect> colliders;
  for (const auto& layer : doc["layers"]) {
    if (layer.value("type", "") == "objectgroup" && layer.value("name", "") == "collisions") {
      for (const auto& obj : layer["objects"]) {
        float x = obj.value("x", 0.0f);
        float y = obj.value("y", 0.0f);
        float w = obj.value("width", 0.0f);
        float h = obj.value("height", 0.0f);
        colliders.push_back({x, y, w, h});
      }
    }
  }

  for (int ty = 0; ty < g_gridH; ++ty) {
    for (int tx = 0; tx < g_gridW; ++tx) {
      float tileMinX = tx * g_tileSize + 4.0f;
      float tileMinY = ty * g_tileSize + 4.0f;
      float tileMaxX = (tx + 1) * g_tileSize - 4.0f;
      float tileMaxY = (ty + 1) * g_tileSize - 4.0f;

      for (const auto& box : colliders) {
        if (tileMinX < box.x + box.w && tileMaxX > box.x &&
            tileMinY < box.y + box.h && tileMaxY > box.y) {
          walkable[ty][tx] = false;
          break;
        }
      }
    }
  }
  return walkable;
}

// 可用工位网格坐标
const std::vector<std::pair<int, int>> g_seats = {
  {5, 7}, {8, 7}, {11, 7}, {5, 9}, {8, 9}, {11, 9}, {8, 13}
};

// -------------------------------------------------------------
// 着色器编译与图元
// -------------------------------------------------------------
GLuint compileShader(GLenum type, const char* src) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &src, nullptr);
  glCompileShader(shader);
  return shader;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  return program;
}

GLuint createWhiteTexture() {
  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  uint32_t white = 0xFFFFFFFF;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glBindTexture(GL_TEXTURE_2D, 0);
  return tex;
}

std::array<Vertex, 6> makeQuad(float x, float y, float w, float h,
                               float u0, float v0, float u1, float v1) {
  return {
    Vertex{x, y, u0, v0},
    Vertex{x + w, y, u1, v0},
    Vertex{x + w, y + h, u1, v1},
    Vertex{x, y, u0, v0},
    Vertex{x + w, y + h, u1, v1},
    Vertex{x, y + h, u0, v1}
  };
}

void drawColorRect(GLuint vbo, GLint uColorTintLoc, float x, float y, float w, float h,
                   float r, float g, float b, float a = 1.0f) {
  std::array<Vertex, 6> quad = makeQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad.data());
  glUniform4f(uColorTintLoc, r, g, b, a);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}
}  // namespace

int main() {
  if (!glfwInit()) return 1;

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif

  GLFWwindow* window = glfwCreateWindow(g_windowInitW, g_windowInitH, "Office Live Simulation", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }

  glfwMakeContextCurrent(window);
  glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
  glfwSetScrollCallback(window, scrollCallback);
  glfwGetFramebufferSize(window, &g_fb.w, &g_fb.h);

  if (!gladLoadGL((GLADloadfunc)glfwGetProcAddress)) return 1;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  const char* vsSrc = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

uniform vec2 uScreen;
uniform vec2 uCamera;
uniform float uZoom;

out vec2 vUV;

void main() {
  vec2 worldPos = (aPos - uCamera) * uZoom;
  vec2 ndc = (worldPos / uScreen) * 2.0;
  ndc.y = -ndc.y;
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUV = aUV;
}
)";

  const char* fsSrc = R"(
#version 330 core
in vec2 vUV;

uniform sampler2D uTex;
uniform vec4 uColorTint;

out vec4 FragColor;

void main() {
  vec4 c = texture(uTex, vUV) * uColorTint;
  if (c.a < 0.1) discard;
  FragColor = c;
}
)";

  GLuint vs = compileShader(GL_VERTEX_SHADER, vsSrc);
  GLuint fs = compileShader(GL_FRAGMENT_SHADER, fsSrc);
  GLuint program = linkProgram(vs, fs);

  // 初始化地图
  Office office;
  if (!office.load("res/maps/office2.json")) {
    std::cerr << "Map load failed.\n";
    return 1;
  }
  auto walkableGrid = loadWalkableGrid("res/maps/office2.json");

  // 初始化打工人角色
  Person person(5, 7, g_tileSize);
  person.init(walkableGrid, g_seats);

  // 初始化独立日志终端
  Journal journal;
  g_journalPtr = &journal;

  GLuint whiteTex = createWhiteTexture();

  // 动态图元绘制的专用 VAO/VBO
  GLuint dynamicVao = 0;
  GLuint dynamicVbo = 0;
  glGenVertexArrays(1, &dynamicVao);
  glGenBuffers(1, &dynamicVbo);
  glBindVertexArray(dynamicVao);
  glBindBuffer(GL_ARRAY_BUFFER, dynamicVbo);
  glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(Vertex), nullptr, GL_DYNAMIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(offsetof(Vertex, u)));
  glBindVertexArray(0);

  GLint uScreenLoc = glGetUniformLocation(program, "uScreen");
  GLint uCameraLoc = glGetUniformLocation(program, "uCamera");
  GLint uZoomLoc = glGetUniformLocation(program, "uZoom");
  GLint uTexLoc = glGetUniformLocation(program, "uTex");
  GLint uColorTintLoc = glGetUniformLocation(program, "uColorTint");

  // 1.0 全景自然比例
  const float zoomFactor = 1.0f;
  double lastTime = glfwGetTime();

  // 摄像机固定在办公室物理中心 (648, 480)
  float fixedCameraX = static_cast<float>(g_internalW) * 0.5f; 
  float fixedCameraY = static_cast<float>(g_internalH) * 0.5f; 

  double lastKeyTime = 0.0;

  // 日志联动状态跟踪变量
  Person::State lastState = person.state();
  bool step2Logged = false;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    double currentTime = glfwGetTime();
    float dt = static_cast<float>(currentTime - lastTime);
    lastTime = currentTime;

    // 键盘微调机位 (W/A/S/D 或 方向键)
    if (currentTime - lastKeyTime > 0.08) {
      bool changed = false;
      if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_LEFT) == GLFW_PRESS) {
        fixedCameraX -= 16.0f;
        changed = true;
      }
      if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_RIGHT) == GLFW_PRESS) {
        fixedCameraX += 16.0f;
        changed = true;
      }
      if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_UP) == GLFW_PRESS) {
        fixedCameraY -= 16.0f;
        changed = true;
      }
      if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS || glfwGetKey(window, GLFW_KEY_DOWN) == GLFW_PRESS) {
        fixedCameraY += 16.0f;
        changed = true;
      }
      if (changed) {
        std::cout << ">> 当前固定机位坐标: CameraX = " << fixedCameraX << ", CameraY = " << fixedCameraY << "\n";
        lastKeyTime = currentTime;
      }
    }

    // 更新角色状态
    person.update(dt);

    // =========================================================
    // 角色生命周期事件 -> 联动写入 Journal 日志
    // =========================================================
    const auto& task = person.currentTask();

    // 状态切换事件检测
    if (person.state() != lastState) {
      if (person.state() == Person::STATE_WORKING) {
        std::string seatMsg = "到达工位 (" + std::to_string(person.tileX()) + ", " +
                              std::to_string(person.tileY()) + ")，就座并连接内网";
        journal.addLog(seatMsg);
        journal.addLog("开始执行任务: " + toUtf8(task.title));
        step2Logged = false;
      } else if (person.state() == Person::STATE_WALKING) {
        journal.addLog(toUtf8(task.result));
        journal.addLog("已离席，正在规划最优路线前往下一个工位...");
      }
      lastState = person.state();
    }

    // 任务中途深度日志推进
    if (person.state() == Person::STATE_WORKING) {
      if (person.workTimer() > 2.8f && !step2Logged) {
        journal.addLog(toUtf8(task.step2));
        step2Logged = true;
      }
      journal.setCurrentTask(toUtf8(task.title), toUtf8(task.step1), "[ 专注开发中 ]", person.workProgress());
    } else {
      journal.setCurrentTask("正在工位间巡检移动", "避障寻路系统已激活，正在前往目标座位...", "[ 移动巡检中 ]", 0.0f);
    }

    // =========================================================
    // 分屏计算与视网膜高分屏支持
    // =========================================================
    int winW, winH;
    glfwGetWindowSize(window, &winW, &winH);
    float dpiScale = (winH > 0) ? (static_cast<float>(g_fb.h) / static_cast<float>(winH)) : 1.0f;

    int uiPixelW = static_cast<int>(g_uiPanelLogicalW * dpiScale);
    int uiPixelH = g_fb.h;
    int gamePixelW = std::max(g_fb.w - uiPixelW, 1);
    int gamePixelH = g_fb.h;

    // 自适应重置 Journal 画布大小
    journal.resize(uiPixelW, uiPixelH, dpiScale);

    glEnable(GL_SCISSOR_TEST);

    // =========================================================
    // 阶段 1：渲染左侧【超大地图画面】(70% 屏幕宽度)
    // =========================================================
    glViewport(0, 0, gamePixelW, gamePixelH);
    glScissor(0, 0, gamePixelW, gamePixelH);
    glClearColor(0.08f, 0.08f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(program);
    glUniform2f(uScreenLoc, static_cast<float>(gamePixelW), static_cast<float>(gamePixelH));
    glUniform2f(uCameraLoc, fixedCameraX, fixedCameraY);
    glUniform1f(uZoomLoc, zoomFactor);
    glUniform1i(uTexLoc, 0);

    // 渲染底层家具（严格重置颜色，防变黑）
    glUniform4f(uColorTintLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    glActiveTexture(GL_TEXTURE0);
    office.drawBase();

    // 绘制打工人小人
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    glBindVertexArray(dynamicVao);
    person.draw(dynamicVbo, uColorTintLoc, static_cast<float>(currentTime));

    // 渲染顶层遮挡家具（严格重置颜色）
    glUniform4f(uColorTintLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    office.drawOverhead();

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // =========================================================
    // 阶段 2：渲染右侧【Journal 日志终端】(30% 屏幕宽度)
    // =========================================================
    glViewport(gamePixelW, 0, uiPixelW, uiPixelH);
    glScissor(gamePixelW, 0, uiPixelW, uiPixelH);
    glClearColor(0.06f, 0.07f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    // 更新日志画板
    journal.update();

    glUniform2f(uScreenLoc, static_cast<float>(uiPixelW), static_cast<float>(uiPixelH));
    glUniform2f(uCameraLoc, static_cast<float>(uiPixelW) * 0.5f, static_cast<float>(uiPixelH) * 0.5f);
    glUniform1f(uZoomLoc, 1.0f);
    glUniform4f(uColorTintLoc, 1.0f, 1.0f, 1.0f, 1.0f);
    glUniform1i(uTexLoc, 0);

    // 绘制 Journal 贴图
    glBindTexture(GL_TEXTURE_2D, journal.textureId());
    glBindVertexArray(dynamicVao);

    std::array<Vertex, 6> uiQuad = makeQuad(
      0.0f, 0.0f, static_cast<float>(uiPixelW), static_cast<float>(uiPixelH),
      0.0f, 0.0f, 1.0f, 1.0f
    );
    glBindBuffer(GL_ARRAY_BUFFER, dynamicVbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(uiQuad), uiQuad.data());
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 绘制左右分屏 2px 分界线
    float lineThickness = 2.0f * dpiScale;
    glBindTexture(GL_TEXTURE_2D, whiteTex);
    drawColorRect(dynamicVbo, uColorTintLoc, 0.0f, 0.0f, lineThickness, static_cast<float>(uiPixelH), 0.25f, 0.28f, 0.35f);

    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);

    glDisable(GL_SCISSOR_TEST);
    glfwSwapBuffers(window);
  }

  // 释放资源
  g_journalPtr = nullptr;
  glDeleteBuffers(1, &dynamicVbo);
  glDeleteVertexArrays(1, &dynamicVao);
  glDeleteProgram(program);
  glDeleteTextures(1, &whiteTex);

  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}