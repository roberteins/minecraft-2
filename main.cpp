#include <GL/glut.h>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <png.h>

namespace fs = std::filesystem;

struct Vec3 { float x, y, z; };
struct IVec3 { int x, y, z; };

static constexpr int WORLD_X = 48;
static constexpr int WORLD_Y = 32;
static constexpr int WORLD_Z = 48;
static constexpr float PI = 3.1415926535f;
static constexpr int HOTBAR_SLOTS = 9;
static constexpr int INVENTORY_ROWS = 4; // first row is hotbar
static constexpr int INVENTORY_COLS = 9;
static constexpr int INVENTORY_SLOTS = INVENTORY_ROWS * INVENTORY_COLS;
static constexpr int MAX_STACK = 64;

enum BlockType : uint8_t {
    AIR = 0,
    GRASS,
    DIRT,
    STONE,
    OAK_PLANKS,
    COBBLESTONE,
    SAND,
    GLASS,
    OAK_LOG,
    BRICKS,
    BLOCK_COUNT
};

struct BlockTex { const char* top; const char* side; const char* bottom; };

static std::vector<uint8_t> world(WORLD_X * WORLD_Y * WORLD_Z, AIR);
static int windowW = 1280, windowH = 720;
static Vec3 playerPos{24.0f, 12.0f, 24.0f};
static Vec3 velocity{0, 0, 0};
static float yaw = 0.0f, pitch = 0.0f;
static bool keyDown[256]{};
static bool onGround = false;
static int selectedSlot = 0;
static bool inventoryOpen = false;
static bool warpingMouse = false;

static int lastMouseX = -1, lastMouseY = -1;
static bool firstMouse = true;
static float deltaTime = 1.0f / 60.0f;
static int lastTicks = 0;

struct Texture {
    GLuint id = 0;
    int w = 0;
    int h = 0;
};

struct ItemStack {
    BlockType block = AIR;
    int count = 0;
};

struct BlockTextureSet {
    GLuint top = 0;
    GLuint side = 0;
    GLuint bottom = 0;
};

static ItemStack inventory[INVENTORY_SLOTS];
static BlockTextureSet blockTextures[BLOCK_COUNT];
static Texture hudHotbarTex{};
static Texture hudHotbarSelTex{};

static std::map<std::string, Texture> texCache;

inline int idx(int x, int y, int z) { return x + WORLD_X * (z + WORLD_Z * y); }

uint8_t getBlock(int x, int y, int z) {
    if (x < 0 || y < 0 || z < 0 || x >= WORLD_X || y >= WORLD_Y || z >= WORLD_Z) return AIR;
    return world[idx(x, y, z)];
}

void setBlock(int x, int y, int z, uint8_t b) {
    if (x < 0 || y < 0 || z < 0 || x >= WORLD_X || y >= WORLD_Y || z >= WORLD_Z) return;
    world[idx(x, y, z)] = b;
}

void initInventory() {
    BlockType starter[HOTBAR_SLOTS] = {GRASS, DIRT, STONE, OAK_PLANKS, COBBLESTONE, SAND, GLASS, OAK_LOG, BRICKS};
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        inventory[i].block = starter[i];
        inventory[i].count = MAX_STACK;
    }
}

bool addToInventory(BlockType block, int amount) {
    if (block == AIR || amount <= 0) return true;
    for (int i = 0; i < INVENTORY_SLOTS && amount > 0; ++i) {
        if (inventory[i].block == block && inventory[i].count < MAX_STACK) {
            int add = std::min(MAX_STACK - inventory[i].count, amount);
            inventory[i].count += add;
            amount -= add;
        }
    }
    for (int i = 0; i < INVENTORY_SLOTS && amount > 0; ++i) {
        if (inventory[i].count == 0 || inventory[i].block == AIR) {
            inventory[i].block = block;
            int add = std::min(MAX_STACK, amount);
            inventory[i].count = add;
            amount -= add;
        }
    }
    return amount == 0;
}

bool consumeHotbarSelected() {
    ItemStack& s = inventory[selectedSlot];
    if (s.count <= 0 || s.block == AIR) return false;
    s.count--;
    if (s.count <= 0) {
        s.count = 0;
        s.block = AIR;
    }
    return true;
}

Texture makeFallbackTexture(uint8_t r, uint8_t g, uint8_t b) {
    Texture t;
    uint8_t data[16 * 16 * 4];
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            bool c = ((x / 4) + (y / 4)) % 2 == 0;
            int i = (y * 16 + x) * 4;
            data[i + 0] = c ? r : uint8_t(std::max(0, r - 30));
            data[i + 1] = c ? g : uint8_t(std::max(0, g - 30));
            data[i + 2] = c ? b : uint8_t(std::max(0, b - 30));
            data[i + 3] = 255;
        }
    }
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    t.w = t.h = 16;
    return t;
}

Texture loadTexture(const std::string& logical, const std::string& path, uint8_t fr, uint8_t fg, uint8_t fb) {
    if (texCache.count(logical)) return texCache[logical];
    Texture t;
    FILE* fp = std::fopen(path.c_str(), "rb");
    if (!fp) {
        t = makeFallbackTexture(fr, fg, fb);
        texCache[logical] = t;
        std::cerr << "[warn] texture missing: " << path << " -> fallback\n";
        return t;
    }

    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
    png_infop info = png_create_info_struct(png);
    if (!png || !info || setjmp(png_jmpbuf(png))) {
        if (png) png_destroy_read_struct(&png, &info, nullptr);
        std::fclose(fp);
        t = makeFallbackTexture(fr, fg, fb);
        texCache[logical] = t;
        return t;
    }

    png_init_io(png, fp);
    png_read_info(png, info);
    t.w = png_get_image_width(png, info);
    t.h = png_get_image_height(png, info);
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB || color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    std::vector<png_byte> image(t.w * t.h * 4);
    std::vector<png_bytep> rows(t.h);
    for (int y = 0; y < t.h; ++y) rows[y] = image.data() + y * t.w * 4;
    png_read_image(png, rows.data());

    png_destroy_read_struct(&png, &info, nullptr);
    std::fclose(fp);

    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
    texCache[logical] = t;
    return t;
}

static std::map<BlockType, BlockTex> blockTex = {
    {GRASS, {"grass_block_top", "grass_block_side", "dirt"}},
    {DIRT, {"dirt", "dirt", "dirt"}},
    {STONE, {"stone", "stone", "stone"}},
    {OAK_PLANKS, {"oak_planks", "oak_planks", "oak_planks"}},
    {COBBLESTONE, {"cobblestone", "cobblestone", "cobblestone"}},
    {SAND, {"sand", "sand", "sand"}},
    {GLASS, {"glass", "glass", "glass"}},
    {OAK_LOG, {"oak_log_top", "oak_log", "oak_log_top"}},
    {BRICKS, {"bricks", "bricks", "bricks"}},
};

std::string assetRoot() {
    const char* env = std::getenv("MC_ASSET_ROOT");
    if (env && *env) return std::string(env);
    if (fs::exists("/src/textures")) return "/src";
    if (fs::exists("/src/assets/minecraft/textures")) return "/src/assets/minecraft";
    if (fs::exists("/src/minecraft/textures")) return "/src/minecraft";
    if (fs::exists("./src/textures")) return "./src";
    if (fs::exists("./src/assets/minecraft/textures")) return "./src/assets/minecraft";
    if (fs::exists("./src/minecraft/textures")) return "./src/minecraft";
    return "";
}

std::string resolveTexturePath(const std::string& name) {
    std::vector<std::string> roots;
    const char* env = std::getenv("MC_ASSET_ROOT");
    if (env && *env) roots.emplace_back(env);
    roots.emplace_back("/src");
    roots.emplace_back("./src");
    roots.emplace_back("/src/assets/minecraft");
    roots.emplace_back("./src/assets/minecraft");
    roots.emplace_back("/src/minecraft");
    roots.emplace_back("./src/minecraft");

    const std::vector<std::string> patterns = {
        "textures/block/" + name + ".png",
        "assets/minecraft/textures/block/" + name + ".png",
        "minecraft/textures/block/" + name + ".png",
        "textures/blocks/" + name + ".png",
        "textures/item/" + name + ".png",
        "assets/minecraft/textures/item/" + name + ".png",
        "minecraft/textures/item/" + name + ".png"
    };

    for (const auto& root : roots) {
        for (const auto& rel : patterns) {
            fs::path p = fs::path(root) / rel;
            if (fs::exists(p)) return p.string();
        }
    }
    return "";
}

std::string resolveGuiTexturePath(const std::string& name) {
    std::vector<std::string> roots;
    const char* env = std::getenv("MC_ASSET_ROOT");
    if (env && *env) roots.emplace_back(env);
    roots.emplace_back("/src");
    roots.emplace_back("./src");
    roots.emplace_back("/src/assets/minecraft");
    roots.emplace_back("./src/assets/minecraft");
    roots.emplace_back("/src/minecraft");
    roots.emplace_back("./src/minecraft");

    const std::vector<std::string> patterns = {
        "textures/gui/sprites/hud/" + name + ".png",
        "textures/gui/hud/" + name + ".png",
        "assets/minecraft/textures/gui/hud/" + name + ".png",
        "assets/minecraft/textures/gui/sprites/hud/" + name + ".png",
        "minecraft/textures/gui/hud/" + name + ".png",
        "minecraft/textures/gui/sprites/hud/" + name + ".png"
    };

    for (const auto& root : roots) {
        for (const auto& rel : patterns) {
            fs::path p = fs::path(root) / rel;
            if (fs::exists(p)) return p.string();
        }
    }
    return "";
}

Texture texByName(const std::string& name) {
    std::string path = resolveTexturePath(name);
    uint8_t r = 180, g = 180, b = 180;
    if (name.find("grass") != std::string::npos) { r = 95; g = 180; b = 60; }
    if (name.find("dirt") != std::string::npos) { r = 130; g = 95; b = 65; }
    if (name.find("stone") != std::string::npos) { r = 120; g = 120; b = 120; }
    if (name.find("sand") != std::string::npos) { r = 218; g = 208; b = 136; }
    if (name.find("glass") != std::string::npos) { r = 170; g = 220; b = 230; }
    return loadTexture(name, path, r, g, b);
}

void preloadBlockTextures() {
    for (int b = 1; b < BLOCK_COUNT; ++b) {
        auto tx = blockTex[(BlockType)b];
        blockTextures[b].top = texByName(tx.top).id;
        blockTextures[b].side = texByName(tx.side).id;
        blockTextures[b].bottom = texByName(tx.bottom).id;
    }
}

void preloadGuiTextures() {
    // Prefer the exact vanilla HUD sprite location requested by the user.
    std::string hotbarPath;
    if (fs::exists("/src/textures/gui/sprites/hud/hotbar.png")) hotbarPath = "/src/textures/gui/sprites/hud/hotbar.png";
    if (hotbarPath.empty() && fs::exists("./src/textures/gui/sprites/hud/hotbar.png")) hotbarPath = "./src/textures/gui/sprites/hud/hotbar.png";
    if (hotbarPath.empty()) hotbarPath = resolveGuiTexturePath("hotbar");
    if (!hotbarPath.empty()) hudHotbarTex = loadTexture("ui_hotbar", hotbarPath, 90, 90, 90);

    std::string selPath;
    if (fs::exists("/src/textures/gui/sprites/hud/hotbar_selection.png")) selPath = "/src/textures/gui/sprites/hud/hotbar_selection.png";
    if (selPath.empty() && fs::exists("./src/textures/gui/sprites/hud/hotbar_selection.png")) selPath = "./src/textures/gui/sprites/hud/hotbar_selection.png";
    if (selPath.empty()) selPath = resolveGuiTexturePath("hotbar_selection");
    if (!selPath.empty()) hudHotbarSelTex = loadTexture("ui_hotbar_selection", selPath, 255, 230, 120);

    std::cerr << "[hud] hotbar texture: " << (hotbarPath.empty() ? "(missing, fallback UI)" : hotbarPath) << "\n";
    std::cerr << "[hud] selection texture: " << (selPath.empty() ? "(missing, fallback UI)" : selPath) << "\n";
}

void drawFace(float x, float y, float z, int face) {
    glBegin(GL_QUADS);
    switch (face) {
        case 0: // +X
            glTexCoord2f(0, 1); glVertex3f(x + 1, y, z + 1);
            glTexCoord2f(1, 1); glVertex3f(x + 1, y, z);
            glTexCoord2f(1, 0); glVertex3f(x + 1, y + 1, z);
            glTexCoord2f(0, 0); glVertex3f(x + 1, y + 1, z + 1);
            break;
        case 1: // -X
            glTexCoord2f(0, 1); glVertex3f(x, y, z);
            glTexCoord2f(1, 1); glVertex3f(x, y, z + 1);
            glTexCoord2f(1, 0); glVertex3f(x, y + 1, z + 1);
            glTexCoord2f(0, 0); glVertex3f(x, y + 1, z);
            break;
        case 2: // +Y top
            glTexCoord2f(0, 0); glVertex3f(x, y + 1, z + 1);
            glTexCoord2f(1, 0); glVertex3f(x + 1, y + 1, z + 1);
            glTexCoord2f(1, 1); glVertex3f(x + 1, y + 1, z);
            glTexCoord2f(0, 1); glVertex3f(x, y + 1, z);
            break;
        case 3: // -Y bottom
            glTexCoord2f(0, 0); glVertex3f(x, y, z);
            glTexCoord2f(1, 0); glVertex3f(x + 1, y, z);
            glTexCoord2f(1, 1); glVertex3f(x + 1, y, z + 1);
            glTexCoord2f(0, 1); glVertex3f(x, y, z + 1);
            break;
        case 4: // +Z
            glTexCoord2f(0, 1); glVertex3f(x, y, z + 1);
            glTexCoord2f(1, 1); glVertex3f(x + 1, y, z + 1);
            glTexCoord2f(1, 0); glVertex3f(x + 1, y + 1, z + 1);
            glTexCoord2f(0, 0); glVertex3f(x, y + 1, z + 1);
            break;
        case 5: // -Z
            glTexCoord2f(0, 1); glVertex3f(x + 1, y, z);
            glTexCoord2f(1, 1); glVertex3f(x, y, z);
            glTexCoord2f(1, 0); glVertex3f(x, y + 1, z);
            glTexCoord2f(0, 0); glVertex3f(x + 1, y + 1, z);
            break;
    }
    glEnd();
}

void generateWorld() {
    for (int x = 0; x < WORLD_X; ++x) {
        for (int z = 0; z < WORLD_Z; ++z) {
            float h = 8.0f + std::sin(x * 0.27f) * 2.0f + std::cos(z * 0.31f) * 2.0f;
            int height = int(h);
            for (int y = 0; y <= height; ++y) {
                if (y == height) setBlock(x, y, z, GRASS);
                else if (y > height - 3) setBlock(x, y, z, DIRT);
                else setBlock(x, y, z, STONE);
            }
            if ((x + z) % 19 == 0) {
                int y = height + 1;
                if (y < WORLD_Y) setBlock(x, y, z, SAND);
            }
        }
    }
}

void applyCamera() {
    float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    Vec3 forward{sy * cp, sp, -cy * cp};
    Vec3 center{playerPos.x + forward.x, playerPos.y + 1.62f + forward.y, playerPos.z + forward.z};
    gluLookAt(playerPos.x, playerPos.y + 1.62f, playerPos.z, center.x, center.y, center.z, 0, 1, 0);
}

bool isSolid(uint8_t b) { return b != AIR && b != GLASS; }

bool intersectsSolid(const Vec3& pos) {
    float hw = 0.3f, h = 1.8f;
    int minX = int(std::floor(pos.x - hw));
    int maxX = int(std::floor(pos.x + hw));
    int minY = int(std::floor(pos.y));
    int maxY = int(std::floor(pos.y + h));
    int minZ = int(std::floor(pos.z - hw));
    int maxZ = int(std::floor(pos.z + hw));
    for (int x = minX; x <= maxX; ++x)
        for (int y = minY; y <= maxY; ++y)
            for (int z = minZ; z <= maxZ; ++z)
                if (isSolid(getBlock(x, y, z))) return true;
    return false;
}

void physicsStep() {
    float speed = keyDown['\t'] ? 9.5f : 5.2f;
    float dx = 0, dz = 0;
    float sy = std::sin(yaw), cy = std::cos(yaw);
    if (keyDown['w']) { dx += sy; dz += -cy; }
    if (keyDown['s']) { dx -= sy; dz -= -cy; }
    if (keyDown['a']) { dx += -cy; dz += -sy; }
    if (keyDown['d']) { dx -= -cy; dz -= -sy; }
    float len = std::sqrt(dx * dx + dz * dz);
    if (len > 0.0001f) { dx /= len; dz /= len; }

    velocity.x = dx * speed;
    velocity.z = dz * speed;

    if (onGround && keyDown[' ']) { velocity.y = 6.5f; onGround = false; }
    velocity.y -= 18.0f * deltaTime;

    Vec3 n = playerPos;
    n.x += velocity.x * deltaTime;
    if (!intersectsSolid(n)) playerPos.x = n.x;

    n = playerPos;
    n.z += velocity.z * deltaTime;
    if (!intersectsSolid(n)) playerPos.z = n.z;

    n = playerPos;
    n.y += velocity.y * deltaTime;
    if (!intersectsSolid(n)) {
        playerPos.y = n.y;
        onGround = false;
    } else {
        if (velocity.y < 0) onGround = true;
        velocity.y = 0;
    }

    if (playerPos.y < 2.0f) playerPos.y = 18.0f;
}

bool raycastBlock(IVec3& hit, IVec3& prev, float maxDist = 6.0f) {
    Vec3 origin{playerPos.x, playerPos.y + 1.62f, playerPos.z};
    float cp = std::cos(pitch), sp = std::sin(pitch), cy = std::cos(yaw), sy = std::sin(yaw);
    Vec3 dir{sy * cp, sp, -cy * cp};

    float step = 0.02f;
    Vec3 p = origin;
    IVec3 last{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
    for (float t = 0; t <= maxDist; t += step) {
        p.x = origin.x + dir.x * t;
        p.y = origin.y + dir.y * t;
        p.z = origin.z + dir.z * t;
        IVec3 b{int(std::floor(p.x)), int(std::floor(p.y)), int(std::floor(p.z))};
        if (b.x != last.x || b.y != last.y || b.z != last.z) {
            if (getBlock(b.x, b.y, b.z) != AIR) {
                hit = b;
                prev = last;
                return true;
            }
            last = b;
        }
    }
    return false;
}

void drawCrosshair() {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, windowW, windowH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();
    glDisable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
    glBegin(GL_LINES);
    glVertex2f(windowW * 0.5f - 8, windowH * 0.5f); glVertex2f(windowW * 0.5f + 8, windowH * 0.5f);
    glVertex2f(windowW * 0.5f, windowH * 0.5f - 8); glVertex2f(windowW * 0.5f, windowH * 0.5f + 8);
    glEnd();
    glEnable(GL_TEXTURE_2D);
    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

void drawHotbar() {
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, windowW, windowH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    int slot = 52;
    int pad = 6;
    int total = HOTBAR_SLOTS * slot + (HOTBAR_SLOTS - 1) * pad;
    int x0 = (windowW - total) / 2;
    int y = windowH - 72;

    if (hudHotbarTex.id != 0) {
        int bgW = total + 16;
        int bgH = slot + 14;
        int bgX = x0 - 8;
        int bgY = y - 7;
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, hudHotbarTex.id);
        glColor3f(1, 1, 1);
        glBegin(GL_QUADS);
        glTexCoord2f(0, 0); glVertex2f((float)bgX, (float)bgY);
        glTexCoord2f(1, 0); glVertex2f((float)(bgX + bgW), (float)bgY);
        glTexCoord2f(1, 1); glVertex2f((float)(bgX + bgW), (float)(bgY + bgH));
        glTexCoord2f(0, 1); glVertex2f((float)bgX, (float)(bgY + bgH));
        glEnd();
    }

    glDisable(GL_TEXTURE_2D);
    for (int i = 0; i < HOTBAR_SLOTS; ++i) {
        int x = x0 + i * (slot + pad);
        if (i == selectedSlot) glColor3f(1.0f, 0.9f, 0.2f);
        else glColor3f(0.12f, 0.12f, 0.12f);
        glBegin(GL_QUADS);
        glVertex2f(x - 2, y - 2); glVertex2f(x + slot + 2, y - 2); glVertex2f(x + slot + 2, y + slot + 2); glVertex2f(x - 2, y + slot + 2);
        glEnd();

        glColor3f(0.25f, 0.25f, 0.25f);
        glBegin(GL_QUADS);
        glVertex2f(x, y); glVertex2f(x + slot, y); glVertex2f(x + slot, y + slot); glVertex2f(x, y + slot);
        glEnd();

        if (hudHotbarSelTex.id != 0 && i == selectedSlot) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, hudHotbarSelTex.id);
            glColor3f(1, 1, 1);
            glBegin(GL_QUADS);
            glTexCoord2f(0, 0); glVertex2f((float)(x - 3), (float)(y - 3));
            glTexCoord2f(1, 0); glVertex2f((float)(x + slot + 3), (float)(y - 3));
            glTexCoord2f(1, 1); glVertex2f((float)(x + slot + 3), (float)(y + slot + 3));
            glTexCoord2f(0, 1); glVertex2f((float)(x - 3), (float)(y + slot + 3));
            glEnd();
            glDisable(GL_TEXTURE_2D);
        }

        if (inventory[i].count > 0 && inventory[i].block > AIR && inventory[i].block < BLOCK_COUNT) {
            auto names = blockTex[inventory[i].block];
            GLuint tid = texByName(names.side).id;
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, tid);
            glColor3f(1,1,1);
            glBegin(GL_QUADS);
            glTexCoord2f(0,0); glVertex2f(x + 8, y + 8);
            glTexCoord2f(1,0); glVertex2f(x + slot - 8, y + 8);
            glTexCoord2f(1,1); glVertex2f(x + slot - 8, y + slot - 8);
            glTexCoord2f(0,1); glVertex2f(x + 8, y + slot - 8);
            glEnd();
        }
        glDisable(GL_TEXTURE_2D);

        if (inventory[i].count > 0) {
            glColor3f(1, 1, 1);
            std::string n = std::to_string(inventory[i].count);
            glRasterPos2f((float)(x + slot - 8 - (int)n.size() * 8), (float)(y + slot - 8));
            for (char c : n) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);
        }
    }

    glColor3f(1, 1, 1);
    glRasterPos2f(18, 24);
    std::string tip = "WASD move | Mouse look | LMB break | RMB place | E inventory | 1-9 select";
    for (char c : tip) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);

    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

void drawInventory() {
    if (!inventoryOpen) return;
    glMatrixMode(GL_PROJECTION); glPushMatrix(); glLoadIdentity();
    glOrtho(0, windowW, windowH, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glPushMatrix(); glLoadIdentity();

    glDisable(GL_TEXTURE_2D);
    glColor4f(0.0f, 0.0f, 0.0f, 0.55f);
    glBegin(GL_QUADS);
    glVertex2f(0, 0); glVertex2f((float)windowW, 0); glVertex2f((float)windowW, (float)windowH); glVertex2f(0, (float)windowH);
    glEnd();

    int slot = 52, pad = 6;
    int totalW = INVENTORY_COLS * slot + (INVENTORY_COLS - 1) * pad;
    int totalH = INVENTORY_ROWS * slot + (INVENTORY_ROWS - 1) * pad;
    int x0 = (windowW - totalW) / 2;
    int y0 = (windowH - totalH) / 2;

    for (int r = 0; r < INVENTORY_ROWS; ++r) {
        for (int c = 0; c < INVENTORY_COLS; ++c) {
            int i = r * INVENTORY_COLS + c;
            int x = x0 + c * (slot + pad);
            int y = y0 + r * (slot + pad);
            bool isSelected = (i == selectedSlot);

            if (isSelected) glColor3f(1.0f, 0.9f, 0.2f);
            else glColor3f(0.14f, 0.14f, 0.14f);
            glBegin(GL_QUADS);
            glVertex2f(x - 2, y - 2); glVertex2f(x + slot + 2, y - 2); glVertex2f(x + slot + 2, y + slot + 2); glVertex2f(x - 2, y + slot + 2);
            glEnd();

            glColor3f(0.25f, 0.25f, 0.25f);
            glBegin(GL_QUADS);
            glVertex2f(x, y); glVertex2f(x + slot, y); glVertex2f(x + slot, y + slot); glVertex2f(x, y + slot);
            glEnd();

            if (inventory[i].count > 0 && inventory[i].block > AIR && inventory[i].block < BLOCK_COUNT) {
                auto names = blockTex[inventory[i].block];
                GLuint tid = texByName(names.side).id;
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, tid);
                glColor3f(1, 1, 1);
                glBegin(GL_QUADS);
                glTexCoord2f(0,0); glVertex2f(x + 8, y + 8);
                glTexCoord2f(1,0); glVertex2f(x + slot - 8, y + 8);
                glTexCoord2f(1,1); glVertex2f(x + slot - 8, y + slot - 8);
                glTexCoord2f(0,1); glVertex2f(x + 8, y + slot - 8);
                glEnd();
                glDisable(GL_TEXTURE_2D);

                glColor3f(1, 1, 1);
                std::string n = std::to_string(inventory[i].count);
                glRasterPos2f((float)(x + slot - 8 - (int)n.size() * 8), (float)(y + slot - 8));
                for (char ch : n) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, ch);
            }
        }
    }

    glColor3f(1, 1, 1);
    std::string tip = "Inventory (E to close) - top row is hotbar";
    glRasterPos2f((float)x0, (float)(y0 - 14));
    for (char c : tip) glutBitmapCharacter(GLUT_BITMAP_8_BY_13, c);

    glPopMatrix(); glMatrixMode(GL_PROJECTION); glPopMatrix(); glMatrixMode(GL_MODELVIEW);
}

void renderWorld() {
    glEnable(GL_TEXTURE_2D);
    const int renderDistance = 24;
    int px = int(std::floor(playerPos.x));
    int pz = int(std::floor(playerPos.z));
    int minX = std::max(0, px - renderDistance);
    int maxX = std::min(WORLD_X - 1, px + renderDistance);
    int minZ = std::max(0, pz - renderDistance);
    int maxZ = std::min(WORLD_Z - 1, pz + renderDistance);
    GLuint currentTex = 0;

    for (int y = 0; y < WORLD_Y; ++y) {
        for (int z = minZ; z <= maxZ; ++z) {
            for (int x = minX; x <= maxX; ++x) {
                auto b = getBlock(x, y, z);
                if (b == AIR) continue;
                GLuint tTop = blockTextures[b].top;
                GLuint tSide = blockTextures[b].side;
                GLuint tBottom = blockTextures[b].bottom;
                const bool tintGrass = (b == GRASS);

                if (getBlock(x + 1, y, z) == AIR) {
                    if (tintGrass) glColor3f(0.54f, 0.74f, 0.34f); else glColor3f(1, 1, 1);
                    if (currentTex != tSide) { glBindTexture(GL_TEXTURE_2D, tSide); currentTex = tSide; }
                    drawFace((float)x, (float)y, (float)z, 0);
                }
                if (getBlock(x - 1, y, z) == AIR) {
                    if (tintGrass) glColor3f(0.54f, 0.74f, 0.34f); else glColor3f(1, 1, 1);
                    if (currentTex != tSide) { glBindTexture(GL_TEXTURE_2D, tSide); currentTex = tSide; }
                    drawFace((float)x, (float)y, (float)z, 1);
                }
                if (getBlock(x, y + 1, z) == AIR) {
                    if (tintGrass) glColor3f(0.54f, 0.74f, 0.34f); else glColor3f(1, 1, 1);
                    if (currentTex != tTop) { glBindTexture(GL_TEXTURE_2D, tTop); currentTex = tTop; }
                    drawFace((float)x, (float)y, (float)z, 2);
                }
                if (getBlock(x, y - 1, z) == AIR) {
                    glColor3f(1, 1, 1);
                    if (currentTex != tBottom) { glBindTexture(GL_TEXTURE_2D, tBottom); currentTex = tBottom; }
                    drawFace((float)x, (float)y, (float)z, 3);
                }
                if (getBlock(x, y, z + 1) == AIR) {
                    if (tintGrass) glColor3f(0.54f, 0.74f, 0.34f); else glColor3f(1, 1, 1);
                    if (currentTex != tSide) { glBindTexture(GL_TEXTURE_2D, tSide); currentTex = tSide; }
                    drawFace((float)x, (float)y, (float)z, 4);
                }
                if (getBlock(x, y, z - 1) == AIR) {
                    if (tintGrass) glColor3f(0.54f, 0.74f, 0.34f); else glColor3f(1, 1, 1);
                    if (currentTex != tSide) { glBindTexture(GL_TEXTURE_2D, tSide); currentTex = tSide; }
                    drawFace((float)x, (float)y, (float)z, 5);
                }
            }
        }
    }
    glColor3f(1, 1, 1);
}

void drawBlockOutline() {
    IVec3 hit{}, prev{};
    if (!raycastBlock(hit, prev)) return;

    const float e = 0.002f;
    float x0 = (float)hit.x - e, y0 = (float)hit.y - e, z0 = (float)hit.z - e;
    float x1 = (float)hit.x + 1.0f + e, y1 = (float)hit.y + 1.0f + e, z1 = (float)hit.z + 1.0f + e;

    glDisable(GL_TEXTURE_2D);
    glDisable(GL_CULL_FACE);
    glColor3f(0.0f, 0.0f, 0.0f);
    glLineWidth(2.0f);
    glBegin(GL_LINES);
    // bottom
    glVertex3f(x0, y0, z0); glVertex3f(x1, y0, z0);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y0, z1);
    glVertex3f(x1, y0, z1); glVertex3f(x0, y0, z1);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y0, z0);
    // top
    glVertex3f(x0, y1, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x1, y1, z0); glVertex3f(x1, y1, z1);
    glVertex3f(x1, y1, z1); glVertex3f(x0, y1, z1);
    glVertex3f(x0, y1, z1); glVertex3f(x0, y1, z0);
    // verticals
    glVertex3f(x0, y0, z0); glVertex3f(x0, y1, z0);
    glVertex3f(x1, y0, z0); glVertex3f(x1, y1, z0);
    glVertex3f(x1, y0, z1); glVertex3f(x1, y1, z1);
    glVertex3f(x0, y0, z1); glVertex3f(x0, y1, z1);
    glEnd();
    glLineWidth(1.0f);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glColor3f(1, 1, 1);
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    gluPerspective(75.0, (double)windowW / (double)windowH, 0.05, 500.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    applyCamera();

    glColor3f(1, 1, 1);
    renderWorld();
    drawBlockOutline();
    drawCrosshair();
    drawHotbar();
    drawInventory();

    glutSwapBuffers();
}

void idle() {
    int now = glutGet(GLUT_ELAPSED_TIME);
    deltaTime = std::max(0.001f, (now - lastTicks) / 1000.0f);
    lastTicks = now;
    if (!inventoryOpen) physicsStep();
    glutPostRedisplay();
}

void reshape(int w, int h) {
    windowW = w; windowH = h;
    glViewport(0, 0, w, h);
}

void keyboard(unsigned char key, int, int) {
    keyDown[key] = true;
    if (key >= '1' && key <= '9') selectedSlot = key - '1';
    if (selectedSlot < 0) selectedSlot = 0;
    if (selectedSlot >= HOTBAR_SLOTS) selectedSlot = HOTBAR_SLOTS - 1;
    if (key == 'e' || key == 'E') {
        inventoryOpen = !inventoryOpen;
        firstMouse = true;
        if (inventoryOpen) {
            glutSetCursor(GLUT_CURSOR_LEFT_ARROW);
        } else {
            glutSetCursor(GLUT_CURSOR_NONE);
            warpingMouse = true;
            glutWarpPointer(windowW / 2, windowH / 2);
        }
    }
    if (key == 27) std::exit(0);
}

void keyboardUp(unsigned char key, int, int) { keyDown[key] = false; }

void mouseMotion(int x, int y) {
    if (inventoryOpen) return;
    if (warpingMouse) { warpingMouse = false; return; }

    int cx = windowW / 2;
    int cy = windowH / 2;
    if (firstMouse) { lastMouseX = cx; lastMouseY = cy; firstMouse = false; }
    int dx = x - cx;
    int dy = y - cy;
    if (dx == 0 && dy == 0) return;

    float sens = 0.0022f;
    yaw += dx * sens;
    pitch -= dy * sens;
    if (pitch > 1.55f) pitch = 1.55f;
    if (pitch < -1.55f) pitch = -1.55f;

    warpingMouse = true;
    glutWarpPointer(cx, cy);
}

void mouseButton(int button, int state, int, int) {
    if (inventoryOpen) return;
    if (state != GLUT_DOWN) return;
    IVec3 hit{}, prev{};
    if (!raycastBlock(hit, prev)) return;

    if (button == GLUT_LEFT_BUTTON) {
        BlockType broken = (BlockType)getBlock(hit.x, hit.y, hit.z);
        setBlock(hit.x, hit.y, hit.z, AIR);
        addToInventory(broken, 1);
    } else if (button == GLUT_RIGHT_BUTTON) {
        if (inventory[selectedSlot].count <= 0 || inventory[selectedSlot].block == AIR) return;
        if (getBlock(prev.x, prev.y, prev.z) == AIR) {
            setBlock(prev.x, prev.y, prev.z, inventory[selectedSlot].block);
            consumeHotbarSelected();
        }
    }
}

void initGL() {
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.05f);
    glClearColor(0.53f, 0.81f, 0.98f, 1.0f);
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
    glutInitWindowSize(windowW, windowH);
    glutCreateWindow("minecraft-2 (C++ / OpenGL)");
    glutFullScreen();
    windowW = glutGet(GLUT_WINDOW_WIDTH);
    windowH = glutGet(GLUT_WINDOW_HEIGHT);

    initGL();
    generateWorld();
    initInventory();
    preloadBlockTextures();
    preloadGuiTextures();

    std::cout << "Asset root: " << (assetRoot().empty() ? "(none, using fallback textures)" : assetRoot()) << "\n";

    glutDisplayFunc(display);
    glutIdleFunc(idle);
    glutReshapeFunc(reshape);
    glutKeyboardFunc(keyboard);
    glutKeyboardUpFunc(keyboardUp);
    glutPassiveMotionFunc(mouseMotion);
    glutMotionFunc(mouseMotion);
    glutMouseFunc(mouseButton);

    lastTicks = glutGet(GLUT_ELAPSED_TIME);
    glutSetCursor(GLUT_CURSOR_NONE);
    warpingMouse = true;
    glutWarpPointer(windowW / 2, windowH / 2);
    firstMouse = true;

    glutMainLoop();
    return 0;
}
