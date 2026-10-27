/*
**    ▄▄▄▄  ▄▄▄  ▄▄▄▄                   
** ▀▀ ▀███  ███  ███▀            ▄▄     
** ██  ███  ███  ███ ▄███▄ ████▄ ██ ▄█▀ 
** ██  ███▄▄███▄▄███ ██ ██ ██ ▀▀ ████   
** ██▄  ▀████▀████▀  ▀███▀ ██    ██ ▀█▄ 
*/
#include "person.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <queue>

namespace {
struct Vertex {
  float x;
  float y;
  float u;
  float v;
};

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
}  // namespace

Person::Person(int startTileX, int startTileY, int tileSize)
  : tileSize_(tileSize),
    currentTileX_(startTileX),
    currentTileY_(startTileY),
    worldX_(static_cast<float>(startTileX * tileSize)),
    worldY_(static_cast<float>(startTileY * tileSize)) {
  // 内置预设工作任务
  tasks_ = {
    {
      L"构建核心分布式服务网关",
      L">> 正在分析各节点 RPC 延迟分布...",
      L">> 优化连接池复用率，实施超时熔断策略...",
      L"【完成】吞吐量提升 35%，P99 延迟归零"
    },
    {
      L"分析 Q3 季度产品留存报表",
      L">> 正在从 ClickHouse 提取行为漏斗模型...",
      L">> 对次日留存与核心链路执行聚类归因...",
      L"【完成】数据汇总完毕，已生成可视化图表"
    },
    {
      L"高并发 A* 寻路引擎压力测试",
      L">> 并发模拟 1200 个单位进行动态避障探测...",
      L">> 持续监控内存占用与 CPU 缓存局部性...",
      L"【完成】压测通过，未发现越界与死锁异常"
    },
    {
      L"排查分布式事务 Raft 选举抖动",
      L">> 正在排查网络分区心跳丢失竞态条件...",
      L">> 修复租约续签 Bug，提交至核心代码仓...",
      L"【完成】补丁已上线，主备一致性恢复正常"
    }
  };
}

void Person::init(const std::vector<std::vector<bool>>& walkableGrid,
                  const std::vector<std::pair<int, int>>& seats) {
  walkableGrid_ = walkableGrid;
  seats_ = seats;
}

float Person::workProgress() const {
  if (state_ == STATE_WORKING) {
    return std::clamp(workTimer_ / workDuration_, 0.0f, 1.0f);
  }
  return 0.0f;
}

const Person::WorkTask& Person::currentTask() const {
  return tasks_[currentTaskIndex_];
}

void Person::update(float dt) {
  if (state_ == STATE_WORKING) {
    workTimer_ += dt;
    if (workTimer_ >= workDuration_) {
      if (!seats_.empty()) {
        int nextSeatIdx = std::rand() % seats_.size();
        auto target = seats_[nextSeatIdx];

        path_ = findPath(currentTileX_, currentTileY_, target.first, target.second);
        if (!path_.empty()) {
          state_ = STATE_WALKING;
          pathIndex_ = 0;
        } else {
          workTimer_ = 0.0f;
        }
      }
    }
  } else if (state_ == STATE_WALKING) {
    if (pathIndex_ < path_.size()) {
      float targetX = static_cast<float>(path_[pathIndex_].first * tileSize_);
      float targetY = static_cast<float>(path_[pathIndex_].second * tileSize_);
      float dx = targetX - worldX_;
      float dy = targetY - worldY_;
      float dist = std::sqrt(dx * dx + dy * dy);

      if (dist <= moveSpeed_ * dt || dist < 1.0f) {
        worldX_ = targetX;
        worldY_ = targetY;
        currentTileX_ = path_[pathIndex_].first;
        currentTileY_ = path_[pathIndex_].second;
        pathIndex_++;
      } else {
        worldX_ += (dx / dist) * moveSpeed_ * dt;
        worldY_ += (dy / dist) * moveSpeed_ * dt;
        facingX_ = dx;
      }
    } else {
      state_ = STATE_WORKING;
      workTimer_ = 0.0f;
      currentTaskIndex_ = (currentTaskIndex_ + 1) % tasks_.size();
    }
  }
}

std::vector<std::pair<int, int>> Person::findPath(int startX, int startY, int targetX, int targetY) {
  if (startX == targetX && startY == targetY) {
    return {};
  }
  if (walkableGrid_.empty() || walkableGrid_[0].empty()) {
    return {};
  }

  int gridH = static_cast<int>(walkableGrid_.size());
  int gridW = static_cast<int>(walkableGrid_[0].size());

  std::vector<std::vector<bool>> visited(gridH, std::vector<bool>(gridW, false));
  std::vector<std::vector<std::pair<int, int>>> parent(gridH, std::vector<std::pair<int, int>>(gridW, {-1, -1}));
  std::queue<std::pair<int, int>> q;

  q.push({startX, startY});
  visited[startY][startX] = true;

  const int dx[4] = {0, 0, -1, 1};
  const int dy[4] = {-1, 1, 0, 0};
  bool found = false;

  while (!q.empty()) {
    auto [cx, cy] = q.front();
    q.pop();

    if (cx == targetX && cy == targetY) {
      found = true;
      break;
    }

    for (int i = 0; i < 4; ++i) {
      int nx = cx + dx[i];
      int ny = cy + dy[i];
      if (nx >= 0 && nx < gridW && ny >= 0 && ny < gridH) {
        if (!visited[ny][nx] && (walkableGrid_[ny][nx] || (nx == targetX && ny == targetY))) {
          visited[ny][nx] = true;
          parent[ny][nx] = {cx, cy};
          q.push({nx, ny});
        }
      }
    }
  }

  std::vector<std::pair<int, int>> path;
  if (!found) {
    return path;
  }
  std::pair<int, int> cur = {targetX, targetY};
  while (cur.first != startX || cur.second != startY) {
    path.push_back(cur);
    cur = parent[cur.second][cur.first];
  }
  std::reverse(path.begin(), path.end());
  return path;
}

void Person::drawColorRect(GLuint vbo, GLint uColorTintLoc, float x, float y, float w, float h,
                          float r, float g, float b, float a) {
  std::array<Vertex, 6> quad = makeQuad(x, y, w, h, 0.0f, 0.0f, 1.0f, 1.0f);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(quad), quad.data());
  glUniform4f(uColorTintLoc, r, g, b, a);
  glDrawArrays(GL_TRIANGLES, 0, 6);
}

void Person::draw(GLuint vbo, GLint uColorTintLoc, float time) {
  float bx = worldX_ + 24.0f; // 脚底中心 X
  float by = worldY_ + 44.0f; // 脚底中心 Y

  // 1. 脚底接触投影
  drawColorRect(vbo, uColorTintLoc, bx - 16.0f, by - 4.0f, 32.0f, 8.0f, 0.0f, 0.0f, 0.0f, 0.40f);

  if (state_ == STATE_WALKING) {
    float walkCycle = std::sin(time * 12.0f);
    float legL = walkCycle * 6.0f;
    float legR = -walkCycle * 6.0f;

    // 2. 双腿与皮鞋
    drawColorRect(vbo, uColorTintLoc, bx - 10.0f, by - 20.0f + legL, 7.0f, 14.0f, 0.16f, 0.22f, 0.32f);
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, by - 6.0f + legL, 9.0f, 6.0f, 0.08f, 0.08f, 0.10f);

    drawColorRect(vbo, uColorTintLoc, bx + 3.0f, by - 20.0f + legR, 7.0f, 14.0f, 0.16f, 0.22f, 0.32f);
    drawColorRect(vbo, uColorTintLoc, bx + 3.0f, by - 6.0f + legR, 9.0f, 6.0f, 0.08f, 0.08f, 0.10f);

    // 3. 上身：白衬衫与红领带
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, by - 38.0f, 24.0f, 18.0f, 0.95f, 0.96f, 0.98f);
    drawColorRect(vbo, uColorTintLoc, bx - 2.0f, by - 36.0f, 4.0f, 14.0f, 0.88f, 0.18f, 0.18f);

    // 4. 双臂前后摆动
    drawColorRect(vbo, uColorTintLoc, bx - 16.0f, by - 36.0f + legR, 4.0f, 16.0f, 0.90f, 0.92f, 0.95f);
    drawColorRect(vbo, uColorTintLoc, bx + 12.0f, by - 36.0f + legL, 4.0f, 16.0f, 0.90f, 0.92f, 0.95f);

    // 5. 头部与五官
    drawColorRect(vbo, uColorTintLoc, bx - 10.0f, by - 52.0f, 20.0f, 16.0f, 0.98f, 0.82f, 0.70f);
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, by - 58.0f, 24.0f, 9.0f, 0.24f, 0.16f, 0.12f);
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, by - 52.0f, 3.0f, 8.0f, 0.24f, 0.16f, 0.12f);
    drawColorRect(vbo, uColorTintLoc, bx + 9.0f, by - 52.0f, 3.0f, 8.0f, 0.24f, 0.16f, 0.12f);

    // 视线
    float eyeOffset = (facingX_ > 0.1f) ? 2.0f : ((facingX_ < -0.1f) ? -2.0f : 0.0f);
    drawColorRect(vbo, uColorTintLoc, bx - 6.0f + eyeOffset, by - 46.0f, 3.0f, 4.0f, 0.1f, 0.1f, 0.1f);
    drawColorRect(vbo, uColorTintLoc, bx + 3.0f + eyeOffset, by - 46.0f, 3.0f, 4.0f, 0.1f, 0.1f, 0.1f);

  } else {
    // 坐姿办公
    float sitY = by - 6.0f;

    // 坐下西裤与鞋
    drawColorRect(vbo, uColorTintLoc, bx - 10.0f, sitY - 14.0f, 20.0f, 10.0f, 0.16f, 0.22f, 0.32f);
    drawColorRect(vbo, uColorTintLoc, bx - 11.0f, sitY - 4.0f, 8.0f, 5.0f, 0.08f, 0.08f, 0.10f);
    drawColorRect(vbo, uColorTintLoc, bx + 3.0f, sitY - 4.0f, 8.0f, 5.0f, 0.08f, 0.08f, 0.10f);

    // 躯干与领带
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, sitY - 30.0f, 24.0f, 16.0f, 0.95f, 0.96f, 0.98f);
    drawColorRect(vbo, uColorTintLoc, bx - 2.0f, sitY - 28.0f, 4.0f, 12.0f, 0.88f, 0.18f, 0.18f);

    // 头部与视线
    drawColorRect(vbo, uColorTintLoc, bx - 10.0f, sitY - 44.0f, 20.0f, 16.0f, 0.98f, 0.82f, 0.70f);
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, sitY - 50.0f, 24.0f, 9.0f, 0.24f, 0.16f, 0.12f);
    drawColorRect(vbo, uColorTintLoc, bx - 12.0f, sitY - 44.0f, 3.0f, 7.0f, 0.24f, 0.16f, 0.12f);
    drawColorRect(vbo, uColorTintLoc, bx + 9.0f, sitY - 44.0f, 3.0f, 7.0f, 0.24f, 0.16f, 0.12f);

    drawColorRect(vbo, uColorTintLoc, bx - 6.0f, sitY - 38.0f, 3.0f, 3.0f, 0.1f, 0.1f, 0.1f);
    drawColorRect(vbo, uColorTintLoc, bx + 3.0f, sitY - 38.0f, 3.0f, 3.0f, 0.1f, 0.1f, 0.1f);

    // 笔记本电脑
    drawColorRect(vbo, uColorTintLoc, bx - 14.0f, sitY - 22.0f, 28.0f, 5.0f, 0.75f, 0.78f, 0.82f);
    drawColorRect(vbo, uColorTintLoc, bx - 13.0f, sitY - 36.0f, 26.0f, 14.0f, 0.18f, 0.20f, 0.24f);
    drawColorRect(vbo, uColorTintLoc, bx - 11.0f, sitY - 34.0f, 22.0f, 10.0f, 0.25f, 0.70f, 1.00f);

    // 敲击打字
    float typeL = std::sin(time * 24.0f) * 2.0f;
    float typeR = std::cos(time * 24.0f) * 2.0f;
    drawColorRect(vbo, uColorTintLoc, bx - 10.0f, sitY - 23.0f + typeL, 5.0f, 4.0f, 0.98f, 0.82f, 0.70f);
    drawColorRect(vbo, uColorTintLoc, bx + 5.0f, sitY - 23.0f + typeR, 5.0f, 4.0f, 0.98f, 0.82f, 0.70f);
  }
}