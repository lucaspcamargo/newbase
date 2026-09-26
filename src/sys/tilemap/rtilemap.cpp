#include <newbase/log.hpp>
#include <newbase/res/manager.hpp>
#include <newbase/res/tilemap.hpp>
#include <newbase/utility/strings.hpp>

using namespace nb;

// Resolve a relative path from the map file's directory. Tileset image paths
// in .tmj are relative to the .tsj / .tmj file location.
static std::string resolve_sibling(entt::id_type map_id,
                                   const std::string &rel_path) {
  const auto &handles = rman().handles();
  auto it = handles.find(map_id);
  if (it == handles.end() || it->second.path.empty())
    return nb::util::path_normalize(rel_path);

  const std::string &base = it->second.path;
  auto slash = base.rfind('/');
  if (slash == std::string::npos)
    return nb::util::path_normalize(rel_path);
  return nb::util::path_normalize(base.substr(0, slash + 1) + rel_path);
}

static bool load_tileset_data(ryml::ConstNodeRef ts_node, tilemap_tileset &ts,
                              entt::id_type map_id) {
  if (ts_node.has_child("firstgid"))
    ts_node["firstgid"] >> ts.firstgid;

  // External tileset: load .tsj file — keep data and tree alive for the rest of
  // this call. image paths inside .tsj are relative to the .tsj, so track its
  // id separately.
  std::vector<char> tsj_data;
  ryml::Tree tsj_tree;
  entt::id_type image_base_id = map_id;
  if (ts_node.has_child("source") && !ts_node.has_child("image")) {
    std::string src;
    ts_node["source"] >> src;
    const std::string full = resolve_sibling(map_id, src);
    const auto tsj_id = entt::hashed_string{full.c_str()}.value();

    if (!rman().read_all_sync(tsj_id, tsj_data, true)) {
      log::error("[rloader_tilemap] cannot read tileset '%s'", full.c_str());
      return false;
    }
    tsj_tree = ryml::parse_json_in_place(c4::to_substr(tsj_data.data()));
    ts_node = tsj_tree.rootref();
    image_base_id = tsj_id;
  }

  if (ts_node.has_child("tilewidth"))
    ts_node["tilewidth"] >> ts.tile_width;
  if (ts_node.has_child("tileheight"))
    ts_node["tileheight"] >> ts.tile_height;
  if (ts_node.has_child("columns"))
    ts_node["columns"] >> ts.columns;
  if (ts_node.has_child("tilecount"))
    ts_node["tilecount"] >> ts.tilecount;
  if (ts_node.has_child("spacing"))
    ts_node["spacing"] >> ts.spacing;
  if (ts_node.has_child("margin"))
    ts_node["margin"] >> ts.margin;
  if (ts_node.has_child("imagewidth"))
    ts_node["imagewidth"] >> ts.image_width;
  if (ts_node.has_child("imageheight"))
    ts_node["imageheight"] >> ts.image_height;

  if (ts_node.has_child("image")) {
    std::string img_path;
    ts_node["image"] >> img_path;
    const std::string full = resolve_sibling(image_base_id, img_path);
    const auto tex_id = entt::hashed_string{full.c_str()}.value();
    log::info("[rloader_tilemap] tileset image: '%s' -> '%s' (hash %x)",
              img_path.c_str(), full.c_str(), tex_id);
    ts.tex = rman().load_sync<rtexture>(tex_id);
    if (!ts.tex) {
      log::error("[rloader_tilemap] cannot load tileset texture '%s'",
                 full.c_str());
      return false;
    }
  }

  // Per-tile data: custom properties and collision shapes.
  if (ts_node.has_child("tiles")) {
    for (auto tile_node : ts_node["tiles"]) {
      int tile_id = 0;
      if (!tile_node.has_child("id"))
        continue;
      tile_node["id"] >> tile_id;

      if (tile_node.has_child("properties")) {
        auto &props = ts.tile_properties[tile_id];
        for (auto pn : tile_node["properties"]) {
          std::string pname, ptype;
          if (pn.has_child("name"))
            c4::from_chars(pn["name"].val(), &pname);
          if (pn.has_child("type"))
            c4::from_chars(pn["type"].val(), &ptype);
          if (pname.empty() || !pn.has_child("value"))
            continue;

          if (ptype == "bool") {
            bool v = false;
            pn["value"] >> v;
            props[pname] = entt::meta_any{v};
          } else if (ptype == "int" || ptype == "object") {
            int v = 0;
            pn["value"] >> v;
            props[pname] = entt::meta_any{v};
          } else if (ptype == "float") {
            float v = 0.f;
            pn["value"] >> v;
            props[pname] = entt::meta_any{v};
          } else // string, color, file, class
          {
            std::string v;
            c4::from_chars(pn["value"].val(), &v);
            props[pname] = entt::meta_any{std::move(v)};
          }
        }
      }

      if (!tile_node.has_child("objectgroup"))
        continue;

      // Mark tile as having an objectgroup (empty vector = passthrough).
      auto &shapes = ts.tile_shapes[tile_id];

      auto og = tile_node["objectgroup"];
      if (!og.has_child("objects"))
        continue;

      for (auto obj : og["objects"]) {
        float ox = 0.f, oy = 0.f, ow = 0.f, oh = 0.f, rot_deg = 0.f;
        if (obj.has_child("x"))
          obj["x"] >> ox;
        if (obj.has_child("y"))
          obj["y"] >> oy;
        if (obj.has_child("width"))
          obj["width"] >> ow;
        if (obj.has_child("height"))
          obj["height"] >> oh;
        if (obj.has_child("rotation"))
          obj["rotation"] >> rot_deg;

        if (obj.has_child("ellipse")) {
          log::warn("[rloader_tilemap] tile %d: ellipse collision shape "
                    "unsupported, skipped",
                    tile_id);
          continue;
        }

        std::vector<glm::vec2> poly;
        if (obj.has_child("polygon")) {
          for (auto pt : obj["polygon"]) {
            float px = 0.f, py = 0.f;
            if (pt.has_child("x"))
              pt["x"] >> px;
            if (pt.has_child("y"))
              pt["y"] >> py;
            poly.push_back({px, py});
          }
        } else {
          // Rectangle → 4-vertex polygon.
          poly = {{0.f, 0.f}, {ow, 0.f}, {ow, oh}, {0.f, oh}};
        }

        // Apply object-local rotation (Tiled: clockwise degrees, y-down screen
        // space).
        if (rot_deg != 0.f) {
          const float rad = rot_deg * (3.14159265f / 180.f);
          const float c = std::cos(rad), s = std::sin(rad);
          for (auto &p : poly) {
            float nx = p.x * c - p.y * s;
            float ny = p.x * s + p.y * c;
            p = {nx, ny};
          }
        }

        // Translate by object origin within the tile.
        for (auto &p : poly) {
          p.x += ox;
          p.y += oy;
        }

        if (!poly.empty())
          shapes.push_back(std::move(poly));
      }
    }
    log::info("[rloader_tilemap] tileset: %zu tiles with collision shapes, %zu "
              "with custom properties",
              ts.tile_shapes.size(), ts.tile_properties.size());
  }

  return true;
}

bool rtilemap::do_load() {
  log::info("[rloader_tilemap] loading: %x", id());

  std::vector<char> data;
  if (!rman().read_all_sync(id(), data, true)) {
    log::error("[rloader_tilemap] cannot read: %x", id());
    return false;
  }

  auto tree = ryml::parse_json_in_place(c4::to_substr(data.data()));
  auto root = tree.rootref();

  if (root.has_child("width"))
    root["width"] >> width;
  if (root.has_child("height"))
    root["height"] >> height;
  if (root.has_child("tilewidth"))
    root["tilewidth"] >> tile_width;
  if (root.has_child("tileheight"))
    root["tileheight"] >> tile_height;

  if (root.has_child("tilesets")) {
    for (auto ts_node : root["tilesets"]) {
      tilemap_tileset ts;
      if (!load_tileset_data(ts_node, ts, id())) {

        log::info("[rloader_tilemap] tielset parse failed: 0x%08x", id());
      }
      tilesets.push_back(std::move(ts));
    }
  }

  if (root.has_child("layers")) {
    for (auto ln : root["layers"]) {
      tilemap_layer layer;

      std::string type_str = "tilelayer";
      if (ln.has_child("type"))
        ln["type"] >> type_str;

      if (type_str == "objectgroup") {
        layer.layer_type = tilemap_layer::type::OBJECT;
        if (ln.has_child("name"))
          ln["name"] >> layer.name;
        if (ln.has_child("visible"))
          ln["visible"] >> layer.visible;
        if (ln.has_child("objects")) {
          for (auto on : ln["objects"]) {
            tilemap_object obj;
            if (on.has_child("id"))
              on["id"] >> obj.id;
            if (on.has_child("name")) {
              c4::from_chars(on["name"].val(), &obj.name);
            }
            if (on.has_child("x"))
              on["x"] >> obj.x;
            if (on.has_child("y"))
              on["y"] >> obj.y;
            if (on.has_child("width"))
              on["width"] >> obj.width;
            if (on.has_child("height"))
              on["height"] >> obj.height;
            // "class" is Tiled 1.9+; fall back to legacy "type"
            if (on.has_child("class")) {
              c4::from_chars(on["class"].val(), &obj.type);
            } else if (on.has_child("type")) {
              c4::from_chars(on["type"].val(), &obj.type);
            }
            // shape detection: tile > point > rectangle
            if (on.has_child("gid")) {
              on["gid"] >> obj.gid;
              obj.shape = tilemap_object::SHAPE_TILE;
            } else if (on.has_child("point")) {
              obj.shape = tilemap_object::SHAPE_POINT;
            } else {
              obj.shape = tilemap_object::SHAPE_RECTANGLE;
            }
            if (on.has_child("properties")) {
              for (auto pn : on["properties"]) {
                std::string pname, pval;
                if (pn.has_child("name"))
                  c4::from_chars(pn["name"].val(), &pname);
                if (pn.has_child("value"))
                  c4::from_chars(pn["value"].val(), &pval);
                if (!pname.empty())
                  obj.properties[pname] = pval;
              }
            }
            layer.objects.push_back(std::move(obj));
          }
        }
        layers.push_back(std::move(layer));
        continue;
      }

      // tilelayer
      layer.layer_type = tilemap_layer::type::TILE;
      if (ln.has_child("name"))
        ln["name"] >> layer.name;
      if (ln.has_child("width"))
        ln["width"] >> layer.width;
      if (ln.has_child("height"))
        ln["height"] >> layer.height;
      if (ln.has_child("visible"))
        ln["visible"] >> layer.visible;
      if (ln.has_child("opacity"))
        ln["opacity"] >> layer.opacity;

      if (ln.has_child("data")) {
        auto data_node = ln["data"];
        layer.tiles.reserve(data_node.num_children());
        for (auto tile_node : data_node) {
          int gid = 0;
          tile_node >> gid;
          layer.tiles.push_back(gid);
        }
      }

      layers.push_back(std::move(layer));
    }
  }

  log::info("[rloader_tilemap] loaded %dx%d map, %zu tilesets, %zu layers",
            width, height, tilesets.size(), layers.size());
  return true;
}
