/*
**    ▄▄▄▄  ▄▄▄  ▄▄▄▄                   
** ▀▀ ▀███  ███  ███▀            ▄▄     
** ██  ███  ███  ███ ▄███▄ ████▄ ██ ▄█▀ 
** ██  ███▄▄███▄▄███ ██ ██ ██ ▀▀ ████   
** ██▄  ▀████▀████▀  ▀███▀ ██    ██ ▀█▄ 
*/
#include "office.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "stb_image.h"
#include <nlohmann/json.hpp>

namespace {
struct Vertex {
  float x;
  float y;
  float u;
  float v;
};

// Tiled GID 翻转标志位
constexpr uint32_t kFlipH = 0x80000000u;
constexpr uint32_t kFlipV = 0x40000000u;
constexpr uint32_t kFlipD = 0x20000000u;
constexpr uint32_t kGidMask = ~(kFlipH | kFlipV | kFlipD);

// 贴图加载函数
GLuint loadTexture(const std::string& path, int* outW, int* outH) {
  int w = 0;
  int h = 0;
  int comp = 0;

  // 必须为 false：保持原点在左上角，与 Tiled 瓦片原点保持一致
  stbi_set_flip_vertically_on_load(false);
  unsigned char* data = stbi_load(path.c_str(), &w, &h, &comp, STBI_rgb_alpha);
  if (!data) {
    return 0;
  }

  GLuint tex = 0;
  glGenTextures(1, &tex);
  glBindTexture(GL_TEXTURE_2D, tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);

  stbi_image_free(data);
  if (outW) {
    *outW = w;
  }
  if (outH) {
    *outH = h;
  }
  return tex;
}

// 尝试多个候选路径加载贴图，解决 TMX 路径混乱的问题
GLuint tryLoadTextureMultiPath(const std::filesystem::path& mapDir, const std::string& rawPath, int* outW, int* outH) {
  std::filesystem::path p(rawPath);
  std::vector<std::filesystem::path> candidates = {
    mapDir / p,
    p,
    mapDir / p.filename(),
    std::filesystem::path("res/tilesets") / p.filename(),
    std::filesystem::path("../tilesets") / p.filename()
  };

  for (const auto& path : candidates) {
    std::error_code ec;
    auto normalPath = path.lexically_normal();
    if (std::filesystem::exists(normalPath, ec)) {
      GLuint tex = loadTexture(normalPath.string(), outW, outH);
      if (tex != 0) {
        return tex;
      }
    }
  }

  std::cerr << "Failed to locate tileset image: " << rawPath << "\n";
  return 0;
}

// 顶点与 UV 写入（包含完整的水平、垂直、对角翻转逻辑）
void pushQuad(std::vector<Vertex>& verts, float x, float y, float w, float h,
              float u0, float v0, float u1, float v1,
              bool flipH, bool flipV, bool flipD) {
  // 4 个角的基准 UV：0:左上, 1:右上, 2:右下, 3:左下
  float cu[4] = {u0, u1, u1, u0};
  float cv[4] = {v0, v0, v1, v1};

  // 1. 对角翻转（转置）
  if (flipD) {
    std::swap(cu[1], cu[3]);
    std::swap(cv[1], cv[3]);
  }
  // 2. 水平翻转
  if (flipH) {
    std::swap(cu[0], cu[1]);
    std::swap(cv[0], cv[1]);
    std::swap(cu[3], cu[2]);
    std::swap(cv[3], cv[2]);
  }
  // 3. 垂直翻转
  if (flipV) {
    std::swap(cu[0], cu[3]);
    std::swap(cv[0], cv[3]);
    std::swap(cu[1], cu[2]);
    std::swap(cv[1], cv[2]);
  }

  // 三角形 1: 0 -> 1 -> 2
  verts.push_back({x, y, cu[0], cv[0]});
  verts.push_back({x + w, y, cu[1], cv[1]});
  verts.push_back({x + w, y + h, cu[2], cv[2]});

  // 三角形 2: 0 -> 2 -> 3
  verts.push_back({x, y, cu[0], cv[0]});
  verts.push_back({x + w, y + h, cu[2], cv[2]});
  verts.push_back({x, y + h, cu[3], cv[3]});
}
}  // namespace

Office::~Office() {
  clearBatches();
  for (auto& ts : tilesets_) {
    if (ts.texture) {
      glDeleteTextures(1, &ts.texture);
      ts.texture = 0;
    }
  }
}

bool Office::load(const std::string& jsonPath) {
  clearBatches();
  layers_.clear();
  tilesets_.clear();

  std::filesystem::path mapPath(jsonPath);
  std::string jsonText;
  {
    std::ifstream in(jsonPath);
    if (!in) {
      std::cerr << "Failed to open map file: " << jsonPath << "\n";
      return false;
    }
    jsonText.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  }

  nlohmann::json doc = nlohmann::json::parse(jsonText, nullptr, false);
  if (doc.is_discarded()) {
    std::cerr << "Failed to parse map JSON: " << jsonPath << "\n";
    return false;
  }

  mapWidth_ = doc.value("width", 0);
  mapHeight_ = doc.value("height", 0);
  tileWidth_ = doc.value("tilewidth", 0);
  tileHeight_ = doc.value("tileheight", 0);

  if (mapWidth_ <= 0 || mapHeight_ <= 0 || tileWidth_ <= 0 || tileHeight_ <= 0) {
    std::cerr << "Invalid map dimensions in: " << jsonPath << "\n";
    return false;
  }

  // 1. 加载所有图集
  const auto& tilesets = doc["tilesets"];
  for (const auto& ts : tilesets) {
    Tileset out;
    out.firstgid = ts.value("firstgid", 0);
    out.tilecount = ts.value("tilecount", 0);
    out.columns = ts.value("columns", 0);
    out.tilewidth = ts.value("tilewidth", tileWidth_);
    out.tileheight = ts.value("tileheight", tileHeight_);
    std::string image = ts.value("image", "");
    if (image.empty()) {
      continue;
    }

    out.texture = tryLoadTextureMultiPath(mapPath.parent_path(), image, &out.imageW, &out.imageH);
    if (!out.texture) {
      continue;
    }
    tilesets_.push_back(out);
  }

  // 2. 加载 Tilelayers 和含有 Tile Objects 的 Objectgroups
  const auto& layers = doc["layers"];
  for (const auto& layer : layers) {
    if (!layer.value("visible", true)) {
      continue;
    }

    std::string type = layer.value("type", "");
    if (type == "tilelayer") {
      Layer out;
      out.name = layer.value("name", "");
      out.isObjectGroup = false;
      const auto& data = layer["data"];
      out.data.reserve(data.size());
      for (const auto& val : data) {
        out.data.push_back(val.get<uint32_t>());
      }
      layers_.push_back(std::move(out));
    } else if (type == "objectgroup") {
      // 提取带有贴图 GID 的对象层（如 props, props-over）
      if (layer.contains("objects")) {
        Layer out;
        out.name = layer.value("name", "");
        out.isObjectGroup = true;
        for (const auto& obj : layer["objects"]) {
          if (obj.contains("gid")) {
            TileObject to;
            to.gid = obj.value("gid", 0u);
            to.x = obj.value("x", 0.0f);
            to.y = obj.value("y", 0.0f);
            to.width = obj.value("width", static_cast<float>(tileWidth_));
            to.height = obj.value("height", static_cast<float>(tileHeight_));
            out.objects.push_back(to);
          }
        }
        if (!out.objects.empty()) {
          layers_.push_back(std::move(out));
        }
      }
    }
  }

  if (tilesets_.empty() || layers_.empty()) {
    std::cerr << "Map contains no valid layers or tilesets!\n";
    return false;
  }

  return buildBatches();
}

const Office::Tileset* Office::findTileset(uint32_t gid) const {
  const Tileset* best = nullptr;
  for (const auto& ts : tilesets_) {
    if (gid >= static_cast<uint32_t>(ts.firstgid)) {
      if (!best || ts.firstgid > best->firstgid) {
        best = &ts;
      }
    }
  }
  if (!best) {
    return nullptr;
  }
  uint32_t localId = gid - static_cast<uint32_t>(best->firstgid);
  if (localId >= static_cast<uint32_t>(best->tilecount)) {
    return nullptr;
  }
  return best;
}

bool Office::buildBatches() {
  clearBatches();

  auto buildForLayer = [&](const Layer& layer, std::vector<LayerBatches>& outLayers) {
    std::unordered_map<const Tileset*, std::vector<Vertex>> perTileset;
    perTileset.reserve(tilesets_.size());

    if (!layer.isObjectGroup) {
      // 普通网格瓦片图层
      for (int y = 0; y < mapHeight_; ++y) {
        for (int x = 0; x < mapWidth_; ++x) {
          size_t idx = static_cast<size_t>(y * mapWidth_ + x);
          if (idx >= layer.data.size()) {
            continue;
          }
          uint32_t rawGid = layer.data[idx];
          if (rawGid == 0) {
            continue;
          }

          bool flipH = (rawGid & kFlipH) != 0;
          bool flipV = (rawGid & kFlipV) != 0;
          bool flipD = (rawGid & kFlipD) != 0;
          uint32_t gid = rawGid & kGidMask;

          const Tileset* ts = findTileset(gid);
          if (!ts || ts->columns <= 0) {
            continue;
          }

          uint32_t localId = gid - static_cast<uint32_t>(ts->firstgid);
          int tileX = static_cast<int>(localId % static_cast<uint32_t>(ts->columns));
          int tileY = static_cast<int>(localId / static_cast<uint32_t>(ts->columns));

          float u0 = static_cast<float>(tileX * ts->tilewidth) / ts->imageW;
          float v0 = static_cast<float>(tileY * ts->tileheight) / ts->imageH;
          float u1 = static_cast<float>((tileX + 1) * ts->tilewidth) / ts->imageW;
          float v1 = static_cast<float>((tileY + 1) * ts->tileheight) / ts->imageH;

          float px = static_cast<float>(x * tileWidth_);
          float py = static_cast<float>(y * tileHeight_);

          pushQuad(perTileset[ts], px, py,
                   static_cast<float>(tileWidth_),
                   static_cast<float>(tileHeight_),
                   u0, v0, u1, v1,
                   flipH, flipV, flipD);
        }
      }
    } else {
      // 对象层中的 Tile Objects（如桌椅家具）
      for (const auto& obj : layer.objects) {
        uint32_t rawGid = obj.gid;
        if (rawGid == 0) {
          continue;
        }

        bool flipH = (rawGid & kFlipH) != 0;
        bool flipV = (rawGid & kFlipV) != 0;
        bool flipD = (rawGid & kFlipD) != 0;
        uint32_t gid = rawGid & kGidMask;

        const Tileset* ts = findTileset(gid);
        if (!ts || ts->columns <= 0) {
          continue;
        }

        uint32_t localId = gid - static_cast<uint32_t>(ts->firstgid);
        int tileX = static_cast<int>(localId % static_cast<uint32_t>(ts->columns));
        int tileY = static_cast<int>(localId / static_cast<uint32_t>(ts->columns));

        float u0 = static_cast<float>(tileX * ts->tilewidth) / ts->imageW;
        float v0 = static_cast<float>(tileY * ts->tileheight) / ts->imageH;
        float u1 = static_cast<float>((tileX + 1) * ts->tilewidth) / ts->imageW;
        float v1 = static_cast<float>((tileY + 1) * ts->tileheight) / ts->imageH;

        // Tiled 规则：对象层 Tile 的原点在左下角，必须减去 height 转回左上角
        float px = obj.x;
        float py = obj.y - obj.height;

        pushQuad(perTileset[ts], px, py, obj.width, obj.height,
                 u0, v0, u1, v1,
                 flipH, flipV, flipD);
      }
    }

    // 为该层各图集创建批处理 Mesh
    LayerBatches layerOut;
    layerOut.batches.reserve(perTileset.size());
    for (auto& entry : perTileset) {
      const Tileset* ts = entry.first;
      std::vector<Vertex>& verts = entry.second;
      if (verts.empty()) {
        continue;
      }

      Batch batch;
      batch.texture = ts->texture;
      batch.count = static_cast<GLsizei>(verts.size());

      glGenVertexArrays(1, &batch.vao);
      glGenBuffers(1, &batch.vbo);
      glBindVertexArray(batch.vao);
      glBindBuffer(GL_ARRAY_BUFFER, batch.vbo);
      glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(verts.size() * sizeof(Vertex)),
                   verts.data(), GL_STATIC_DRAW);

      glEnableVertexAttribArray(0);
      glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
      glEnableVertexAttribArray(1);
      glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            (void*)(offsetof(Vertex, u)));

      layerOut.batches.push_back(batch);
    }
    outLayers.push_back(std::move(layerOut));
  };

  // 区分基础层与遮罩顶层
  for (const auto& layer : layers_) {
    if (layer.name == "overhead" || layer.name == "props-over") {
      buildForLayer(layer, overheadLayers_);
    } else {
      buildForLayer(layer, baseLayers_);
    }
  }

  return true;
}

void Office::clearBatches() {
  auto clear = [](std::vector<LayerBatches>& layers) {
    for (auto& layer : layers) {
      for (auto& batch : layer.batches) {
        if (batch.vbo) {
          glDeleteBuffers(1, &batch.vbo);
        }
        if (batch.vao) {
          glDeleteVertexArrays(1, &batch.vao);
        }
      }
      layer.batches.clear();
    }
    layers.clear();
  };
  clear(baseLayers_);
  clear(overheadLayers_);
}

void Office::drawBase() const {
  for (const auto& layer : baseLayers_) {
    for (const auto& batch : layer.batches) {
      glBindTexture(GL_TEXTURE_2D, batch.texture);
      glBindVertexArray(batch.vao);
      glDrawArrays(GL_TRIANGLES, 0, batch.count);
    }
  }
}

void Office::drawOverhead() const {
  for (const auto& layer : overheadLayers_) {
    for (const auto& batch : layer.batches) {
      glBindTexture(GL_TEXTURE_2D, batch.texture);
      glBindVertexArray(batch.vao);
      glDrawArrays(GL_TRIANGLES, 0, batch.count);
    }
  }
}