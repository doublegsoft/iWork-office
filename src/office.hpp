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

class Office {
public:
  struct Tileset {
    int firstgid = 0;
    int tilecount = 0;
    int columns = 0;
    int tilewidth = 0;
    int tileheight = 0;
    int imageW = 0;
    int imageH = 0;
    GLuint texture = 0;
  };

  struct TileObject {
    uint32_t gid;
    float x;
    float y;
    float width;
    float height;
  };

  struct Layer {
    std::string name;
    bool isObjectGroup = false;
    std::vector<uint32_t> data;        // tilelayer 数据
    std::vector<TileObject> objects;   // objectgroup 数据
  };

  struct Batch {
    GLuint vao = 0;
    GLuint vbo = 0;
    GLsizei count = 0;
    GLuint texture = 0;
  };

  struct LayerBatches {
    std::vector<Batch> batches;
  };

  Office() = default;
  ~Office();

  bool load(const std::string& jsonPath);
  void drawBase() const;
  void drawOverhead() const;
  void clearBatches();

  int tileSize() const { return tileWidth_; }
  int widthTiles() const { return mapWidth_; }
  int heightTiles() const { return mapHeight_; }
  int pixelWidth() const { return mapWidth_ * tileWidth_; }
  int pixelHeight() const { return mapHeight_ * tileHeight_; }

private:
  const Tileset* findTileset(uint32_t gid) const;
  bool buildBatches();

  int mapWidth_ = 0;
  int mapHeight_ = 0;
  int tileWidth_ = 0;
  int tileHeight_ = 0;

  std::vector<Tileset> tilesets_;
  std::vector<Layer> layers_;
  std::vector<LayerBatches> baseLayers_;
  std::vector<LayerBatches> overheadLayers_;
};