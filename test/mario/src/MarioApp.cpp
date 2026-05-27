// Super Mario clone (슈퍼마리오) — scene/state management + file-loaded levels.
//
// Title -> Game <-> Pause flow via a SceneManager stack, two levels loaded from
// text files (level1.txt / level2.txt), platformer physics from the previous
// step. Controls: Enter start, Left/Right or A/D move, Space/Up jump, P pause,
// Esc quit (to title from pause, or close from title).

#include <NaraGame/assets/BMFont.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/assets/TileLevel.h>
#include <NaraGame/audio/AudioClip.h>
#include <NaraGame/audio/AudioDevice.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/physics/Physics2D.h>
#include <NaraGame/renderer/BitmapFont.h>
#include <NaraGame/renderer/Camera2D.h>
#include <NaraGame/renderer/Font.h>
#include <NaraGame/renderer/ParticleSystem.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/TextureAtlas.h>
#include <NaraGame/renderer/Tilemap.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>
#include <NaraGame/scene/Scene.h>
#include <NaraGame/scene/SceneManager.h>

#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {
constexpr float kScreenW = 1024.0f;
constexpr float kScreenH = 576.0f;
constexpr int kTile = 32;
constexpr float kT = static_cast<float>(kTile);
constexpr int kMaxLevel = 2;

enum Tile { EMPTY = -1, GROUND = 0, DIRT = 1, BRICK = 2, QUESTION = 3,
            PIPE_TL = 4, PIPE_TR = 5, PIPE_BL = 6, PIPE_BR = 7, HARD = 8, COIN = 9 };

bool IsSolidTile(int id) { return id >= GROUND && id <= HARD; }

constexpr float kGravity = 2300.0f;
constexpr float kFastFall = 2.1f;
constexpr float kMaxFall = 950.0f;
constexpr float kMoveSpeed = 235.0f;
constexpr float kJumpSpeed = 720.0f;
constexpr float kStompBounce = 430.0f;
constexpr float kPlayerW = 26.0f;
constexpr float kPlayerH = 34.0f;
constexpr float kGoombaW = 26.0f;
constexpr float kGoombaH = 24.0f;
constexpr float kGoombaSpeed = 60.0f;

enum BodyTag : uint32_t { TagTile = 1, TagPlayer = 2, TagGoomba = 3 };

struct Goomba {
    int bodyId = -1;
    Vec2 lastPos;          // cached for drawing while squashed (body removed)
    float dir = -1.0f;     // -1 left, +1 right
    bool alive = true;
    float squashTimer = 0.0f;
    float animTimer = 0.0f;
    int frame = 0;
};

// Shared resources + scene factories, owned by the Application.
struct GameContext {
    Renderer2D* renderer = nullptr;
    BitmapFont* font = nullptr;
    Font* propFont = nullptr; // proportional BMFont
    TextureAtlas* chars = nullptr;
    RHITexture* tilesTexture = nullptr;
    SpriteSheet tileset;
    float screenW = kScreenW;
    float screenH = kScreenH;
    std::string assetsDir;

    AudioDevice* audio = nullptr;
    const AudioClip* sfxJump = nullptr;
    const AudioClip* sfxCoin = nullptr;
    const AudioClip* sfxStomp = nullptr;
    const AudioClip* music = nullptr;

    void Sfx(const AudioClip* clip, float volume = 0.6f) const {
        if (audio != nullptr && clip != nullptr && clip->Valid())
            audio->PlaySound(*clip, volume);
    }

    int score = 0;
    int coins = 0;
    int lives = 3;

    std::function<std::unique_ptr<Scene>()> makeTitle;
    std::function<std::unique_ptr<Scene>(int)> makeGame;

    void DrawTextCentered(const std::string& text, float y, float scale, const Color& color) {
        const float w = font->Measure(text, scale).x;
        renderer->DrawText(*font, text, {(screenW - w) * 0.5f, y}, color, scale);
    }
};
} // namespace

// ===========================================================================
// PauseScene — overlay: freezes gameplay (blocks update) but lets it show
// through (does not block render).
// ===========================================================================
class PauseScene : public Scene {
public:
    explicit PauseScene(GameContext* ctx) : m_ctx(ctx) {}

    bool BlocksRender() const override { return false; }

    void OnUpdate(float) override {
        if (Input::IsKeyPressed(Key::Enter) || Input::IsKeyPressed(Key::P))
            Manager().Pop(); // resume
        else if (Input::IsKeyPressed(Key::Escape))
            Manager().Change(m_ctx->makeTitle()); // quit to title
    }

    void OnRender() override {
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        r.DrawQuad({0.0f, 0.0f}, {m_ctx->screenW, m_ctx->screenH}, Color(0.0f, 0.0f, 0.0f, 0.6f));
        m_ctx->DrawTextCentered("PAUSED", 210.0f, 4.0f, Color(1.0f, 1.0f, 1.0f));
        m_ctx->DrawTextCentered("ENTER  RESUME", 300.0f, 1.6f, Color(0.85f, 0.95f, 0.85f));
        m_ctx->DrawTextCentered("ESC  QUIT TO TITLE", 340.0f, 1.6f, Color(0.95f, 0.85f, 0.8f));
        r.End();
    }

private:
    GameContext* m_ctx;
};

// ===========================================================================
// GameScene — the platformer, driven by the shared PhysicsWorld:
// solid tiles -> static bodies, player + goombas -> dynamic bodies.
// ===========================================================================
class GameScene : public Scene {
public:
    GameScene(GameContext* ctx, int level) : m_ctx(ctx), m_level(level) {}

    void OnEnter() override {
        const std::unordered_map<char, int> legend = {
            {'#', GROUND}, {'=', DIRT}, {'b', BRICK}, {'?', QUESTION}, {'(', PIPE_TL},
            {')', PIPE_TR}, {'{', PIPE_BL}, {'}', PIPE_BR}, {'X', HARD}, {'o', COIN}};

        const std::string path = m_ctx->assetsDir + "/level" + std::to_string(m_level) + ".txt";
        TileLevel level = LoadTileLevel(path, kT, kT, m_ctx->tileset, legend);
        m_map = std::move(level.map);
        m_worldSize = m_map.WorldSize();

        m_camera = Camera2D(m_ctx->screenW, m_ctx->screenH);
        m_camera.SetBounds({0.0f, 0.0f}, m_worldSize);

        // Physics world: a static body per solid tile, dynamic bodies for actors.
        m_world.Clear();
        m_world.SetGravity({0.0f, kGravity});
        for (int y = 0; y < m_map.Height(); ++y) {
            for (int x = 0; x < m_map.Width(); ++x) {
                if (!IsSolidTile(m_map.GetTile(x, y)))
                    continue;
                Body tile;
                tile.type = BodyType::Static;
                tile.tag = TagTile;
                tile.position = {x * kT, y * kT};
                tile.size = {kT, kT};
                m_world.AddBody(tile);
            }
        }

        m_goombas.clear();
        m_spawn = {120.0f, kT * 14.0f};
        for (const LevelObject& obj : level.objects) {
            const float px = obj.tileX * kT;
            if (obj.symbol == 'M') {
                m_spawn = {px, (obj.tileY + 1) * kT - kPlayerH};
            } else if (obj.symbol == 'g') {
                Body gb;
                gb.type = BodyType::Dynamic;
                gb.tag = TagGoomba;
                gb.size = {kGoombaW, kGoombaH};
                gb.position = {px, (obj.tileY + 1) * kT - kGoombaH};
                Goomba g;
                g.bodyId = m_world.AddBody(gb);
                g.lastPos = gb.position;
                g.dir = -1.0f;
                m_goombas.push_back(g);
            }
        }

        Body player;
        player.type = BodyType::Dynamic;
        player.tag = TagPlayer;
        player.size = {kPlayerW, kPlayerH};
        player.position = m_spawn;
        m_playerId = m_world.AddBody(player);
        m_facing = 1.0f;
        m_walkFrame = 0;
        m_walkTimer = 0.0f;

        if (m_ctx->audio != nullptr && m_ctx->music != nullptr && m_ctx->music->Valid())
            m_ctx->audio->PlayMusic(*m_ctx->music, 0.35f, true);
    }

    void OnExit() override {
        if (m_ctx->audio != nullptr)
            m_ctx->audio->StopMusic();
    }

    void OnUpdate(float dtRaw) override {
        if (Input::IsKeyPressed(Key::P) || Input::IsKeyPressed(Key::Escape)) {
            Manager().Push(std::make_unique<PauseScene>(m_ctx));
            return;
        }

        const float dt = Min(dtRaw, 1.0f / 30.0f);
        ControlPlayer(dt);
        ControlGoombas(dt);

        m_world.Step(dt); // integrates gravity + resolves tile collisions

        TurnGoombasAtWalls();
        CollectCoins();
        ResolveGoombaHits();
        m_particles.Update(dt);

        Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;
        if (p->position.y > m_worldSize.y + 80.0f) {
            Die();
            return;
        }
        if (p->position.x + kPlayerW >= m_worldSize.x - 2.0f) {
            NextLevel();
            return;
        }
        m_camera.CenterOn({p->position.x + kPlayerW * 0.5f, p->position.y + kPlayerH * 0.5f});
    }

    void OnRender() override {
        Application::Get().GetDevice().Clear(Color(0.42f, 0.55f, 1.0f, 1.0f));
        Renderer2D& r = *m_ctx->renderer;

        r.Begin(m_camera.ViewProjection());
        m_map.Draw(r, m_camera.VisibleBounds());
        for (const Goomba& g : m_goombas) {
            if (!g.alive)
                continue;
            if (g.squashTimer > 0.0f) {
                r.DrawQuad({g.lastPos.x, g.lastPos.y + kGoombaH - 10.0f}, {kGoombaW, 10.0f},
                           m_ctx->chars->Get("goombaSquashed"));
            } else if (const Body* gb = m_world.Get(g.bodyId)) {
                r.DrawQuad(gb->position, {kGoombaW, kGoombaH},
                           m_ctx->chars->Get(g.frame ? "goomba1" : "goomba0"));
            }
        }
        if (const Body* p = m_world.Get(m_playerId)) {
            const char* name = !p->onGround
                                   ? "jump"
                                   : (std::fabs(p->velocity.x) > 5.0f && m_walkFrame ? "walk"
                                                                                     : "stand");
            r.DrawQuad(p->position, {kPlayerW, kPlayerH}, m_ctx->chars->Get(name), Color::White(),
                       m_facing < 0.0f);
        }
        m_particles.Draw(r);
        r.End();

        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        r.DrawText(*m_ctx->font, "SCORE " + std::to_string(m_ctx->score), {16.0f, 14.0f},
                   Color(1.0f, 1.0f, 1.0f), 1.4f);
        r.DrawText(*m_ctx->font, "COINS " + std::to_string(m_ctx->coins), {300.0f, 14.0f},
                   Color(1.0f, 0.9f, 0.4f), 1.4f);
        r.DrawText(*m_ctx->font, "WORLD " + std::to_string(m_level), {540.0f, 14.0f},
                   Color(0.8f, 0.9f, 1.0f), 1.4f);
        r.DrawText(*m_ctx->font, "LIVES " + std::to_string(m_ctx->lives),
                   {m_ctx->screenW - 170.0f, 14.0f}, Color(0.8f, 1.0f, 0.8f), 1.4f);
        r.End();
    }

private:
    // Set the player body's intended velocity from input (the world integrates
    // gravity and resolves tile collisions during Step).
    void ControlPlayer(float dt) {
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;

        float dir = 0.0f;
        if (Input::IsKeyDown(Key::Left) || Input::IsKeyDown(Key::A))
            dir -= 1.0f;
        if (Input::IsKeyDown(Key::Right) || Input::IsKeyDown(Key::D))
            dir += 1.0f;
        p->velocity.x = dir * kMoveSpeed;
        if (dir != 0.0f)
            m_facing = dir;

        const bool jumpPressed = Input::IsKeyPressed(Key::Space) ||
                                 Input::IsKeyPressed(Key::Up) || Input::IsKeyPressed(Key::W);
        const bool jumpHeld = Input::IsKeyDown(Key::Space) || Input::IsKeyDown(Key::Up) ||
                              Input::IsKeyDown(Key::W);
        if (jumpPressed && p->onGround) {
            p->velocity.y = -kJumpSpeed;
            m_ctx->Sfx(m_ctx->sfxJump);
        }

        // Variable-height jump: heavier gravity while rising without holding jump.
        p->gravityScale = (p->velocity.y < 0.0f && !jumpHeld) ? kFastFall : 1.0f;
        if (p->velocity.y > kMaxFall)
            p->velocity.y = kMaxFall;

        if (std::fabs(p->velocity.x) > 5.0f && p->onGround) {
            m_walkTimer += dt;
            if (m_walkTimer >= 0.12f) { m_walkTimer = 0.0f; m_walkFrame ^= 1; }
        } else {
            m_walkFrame = 0;
        }
    }

    void ControlGoombas(float dt) {
        for (Goomba& g : m_goombas) {
            if (!g.alive)
                continue;
            if (g.squashTimer > 0.0f) {
                g.squashTimer -= dt;
                if (g.squashTimer <= 0.0f)
                    g.alive = false;
                continue;
            }
            Body* gb = m_world.Get(g.bodyId);
            if (gb == nullptr || !gb->active)
                continue;
            gb->velocity.x = g.dir * kGoombaSpeed;
            if (gb->velocity.y > kMaxFall)
                gb->velocity.y = kMaxFall;
            g.lastPos = gb->position;
            g.animTimer += dt;
            if (g.animTimer >= 0.2f) { g.animTimer = 0.0f; g.frame ^= 1; }
        }
    }

    // A goomba that hit a wall (horizontal contact normal) reverses direction.
    void TurnGoombasAtWalls() {
        for (const Contact& c : m_world.Contacts()) {
            if (std::fabs(c.normal.x) < 0.5f)
                continue;
            for (Goomba& g : m_goombas) {
                if (g.alive && g.squashTimer <= 0.0f && g.bodyId == c.a) {
                    g.dir = (c.normal.x > 0.0f) ? 1.0f : -1.0f;
                    break;
                }
            }
        }
        // Goombas that fell into a pit.
        for (Goomba& g : m_goombas) {
            if (!g.alive || g.squashTimer > 0.0f)
                continue;
            Body* gb = m_world.Get(g.bodyId);
            if (gb != nullptr && gb->position.y > m_worldSize.y + 80.0f) {
                g.alive = false;
                m_world.RemoveBody(g.bodyId);
            }
        }
    }

    void ResolveGoombaHits() {
        Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;
        for (Goomba& g : m_goombas) {
            if (!g.alive || g.squashTimer > 0.0f)
                continue;
            Body* gb = m_world.Get(g.bodyId);
            if (gb == nullptr || !gb->active)
                continue;
            const bool overlap = p->position.x < gb->position.x + kGoombaW &&
                                 p->position.x + kPlayerW > gb->position.x &&
                                 p->position.y < gb->position.y + kGoombaH &&
                                 p->position.y + kPlayerH > gb->position.y;
            if (!overlap)
                continue;
            const bool stomp = p->velocity.y > 0.0f &&
                               (p->position.y + kPlayerH) < gb->position.y + kGoombaH * 0.6f;
            if (stomp) {
                g.lastPos = gb->position;
                g.squashTimer = 0.35f;
                m_world.RemoveBody(g.bodyId);
                p->velocity.y = -kStompBounce;
                m_ctx->score += 200;
                m_ctx->Sfx(m_ctx->sfxStomp);
                ParticleBurst burst;
                burst.position = {gb->position.x + kGoombaW * 0.5f, gb->position.y + kGoombaH};
                burst.count = 14;
                burst.minSpeed = 40.0f;
                burst.maxSpeed = 170.0f;
                burst.startSize = 6.0f;
                burst.startColor = Color(0.6f, 0.4f, 0.2f);
                burst.endColor = Color(0.6f, 0.4f, 0.2f, 0.0f);
                burst.gravity = {0.0f, 600.0f};
                m_particles.Emit(burst);
            } else {
                Die();
                return;
            }
        }
    }

    void CollectCoins() {
        const Body* p = m_world.Get(m_playerId);
        if (p == nullptr)
            return;
        const int left = static_cast<int>(std::floor(p->position.x / kT));
        const int right = static_cast<int>(std::floor((p->position.x + kPlayerW - 1.0f) / kT));
        const int top = static_cast<int>(std::floor(p->position.y / kT));
        const int bottom = static_cast<int>(std::floor((p->position.y + kPlayerH - 1.0f) / kT));
        for (int ty = top; ty <= bottom; ++ty)
            for (int tx = left; tx <= right; ++tx)
                if (m_map.GetTile(tx, ty) == COIN) {
                    m_map.SetTile(tx, ty, EMPTY);
                    ++m_ctx->coins;
                    m_ctx->score += 100;
                    m_ctx->Sfx(m_ctx->sfxCoin);
                    ParticleBurst burst;
                    burst.position = {tx * kT + kT * 0.5f, ty * kT + kT * 0.5f};
                    burst.count = 12;
                    burst.minSpeed = 50.0f;
                    burst.maxSpeed = 150.0f;
                    burst.minLife = 0.25f;
                    burst.maxLife = 0.5f;
                    burst.startSize = 5.0f;
                    burst.startColor = Color(1.0f, 0.9f, 0.3f);
                    burst.endColor = Color(1.0f, 0.9f, 0.3f, 0.0f);
                    m_particles.Emit(burst);
                }
    }

    void RespawnPlayer() {
        if (Body* p = m_world.Get(m_playerId)) {
            p->position = m_spawn;
            p->velocity = {0.0f, 0.0f};
            p->onGround = false;
        }
        m_facing = 1.0f;
        m_camera.CenterOn(m_spawn);
    }

    void Die() {
        if (--m_ctx->lives <= 0)
            Manager().Change(m_ctx->makeTitle());
        else
            RespawnPlayer();
    }

    void NextLevel() {
        if (m_level < kMaxLevel)
            Manager().Change(m_ctx->makeGame(m_level + 1));
        else
            Manager().Change(m_ctx->makeTitle()); // game complete
    }

    GameContext* m_ctx;
    int m_level;
    Tilemap m_map;
    Camera2D m_camera;
    Vec2 m_worldSize;
    Vec2 m_spawn;

    PhysicsWorld m_world;
    ParticleSystem m_particles;
    int m_playerId = -1;
    std::vector<Goomba> m_goombas;
    float m_facing = 1.0f;
    float m_walkTimer = 0.0f;
    int m_walkFrame = 0;
};

// ===========================================================================
// TitleScene
// ===========================================================================
class TitleScene : public Scene {
public:
    explicit TitleScene(GameContext* ctx) : m_ctx(ctx) {}

    void OnUpdate(float dt) override {
        m_time += dt;
        if (Input::IsKeyPressed(Key::Enter)) {
            m_ctx->score = 0; // fresh run
            m_ctx->coins = 0;
            m_ctx->lives = 3;
            Manager().Change(m_ctx->makeGame(1));
        } else if (Input::IsKeyPressed(Key::Escape)) {
            Application::Get().Close();
        }
    }

    void OnRender() override {
        Application::Get().GetDevice().Clear(Color(0.10f, 0.12f, 0.28f, 1.0f));
        Renderer2D& r = *m_ctx->renderer;
        r.Begin(Renderer2D::ScreenCamera(m_ctx->screenW, m_ctx->screenH));
        m_ctx->DrawTextCentered("N A R A", 150.0f, 5.0f, Color(0.95f, 0.3f, 0.3f));
        m_ctx->DrawTextCentered("SUPER MARIO", 240.0f, 3.0f, Color(1.0f, 1.0f, 1.0f));
        // Blink the prompt.
        if (std::fmod(m_time, 1.0f) < 0.65f)
            m_ctx->DrawTextCentered("PRESS ENTER TO START", 360.0f, 1.6f,
                                    Color(0.9f, 0.9f, 0.6f));
        m_ctx->DrawTextCentered("A/D MOVE   SPACE JUMP   P PAUSE", 470.0f, 1.1f,
                                Color(0.6f, 0.65f, 0.8f));
        // Proportional (variable-width) BMFont tagline.
        if (m_ctx->propFont != nullptr && m_ctx->propFont->Valid()) {
            const std::string tag = "Built from scratch with the NaraGame engine";
            const float s = 0.9f;
            const float w = m_ctx->propFont->Measure(tag, s).x;
            r.DrawText(*m_ctx->propFont, tag, {(m_ctx->screenW - w) * 0.5f, 410.0f},
                       Color(0.7f, 0.85f, 1.0f), s);
        }
        r.End();
    }

private:
    GameContext* m_ctx;
    float m_time = 0.0f;
};

// ===========================================================================
// Application
// ===========================================================================
class MarioApp : public Application {
public:
    MarioApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;

        m_tilesTexture = LoadTexture(GetDevice(), assets + "/tiles.png");
        m_charTexture = LoadTexture(GetDevice(), assets + "/characters.png");
        m_fontTexture = LoadTexture(GetDevice(), assets + "/font.png");
        m_font = BitmapFont(m_fontTexture.get(), ' ', 16, 14, 24);
        m_font.SetTracking(-2);

        m_propFont = LoadBMFont(GetDevice(), assets + "/propfont.fnt");

        m_jump = LoadWav(assets + "/jump.wav");
        m_coin = LoadWav(assets + "/coin.wav");
        m_stomp = LoadWav(assets + "/stomp.wav");
        m_bgm = LoadWav(assets + "/bgm.wav");

        m_chars = TextureAtlas(m_charTexture.get());
        m_chars.Add("stand", 0.0f, 0.0f, 24.0f, 32.0f);
        m_chars.Add("walk", 24.0f, 0.0f, 24.0f, 32.0f);
        m_chars.Add("jump", 48.0f, 0.0f, 24.0f, 32.0f);
        m_chars.Add("goomba0", 0.0f, 34.0f, 24.0f, 22.0f);
        m_chars.Add("goomba1", 24.0f, 34.0f, 24.0f, 22.0f);
        m_chars.Add("goombaSquashed", 48.0f, 46.0f, 24.0f, 10.0f);

        m_ctx.renderer = &m_renderer;
        m_ctx.font = &m_font;
        m_ctx.propFont = &m_propFont;
        m_ctx.chars = &m_chars;
        m_ctx.tilesTexture = m_tilesTexture.get();
        m_ctx.tileset = SpriteSheet(m_tilesTexture.get(), kTile, kTile);
        m_ctx.assetsDir = assets;
        m_ctx.audio = &GetAudio();
        m_ctx.sfxJump = &m_jump;
        m_ctx.sfxCoin = &m_coin;
        m_ctx.sfxStomp = &m_stomp;
        m_ctx.music = &m_bgm;
        m_ctx.makeTitle = [this]() { return std::make_unique<TitleScene>(&m_ctx); };
        m_ctx.makeGame = [this](int level) { return std::make_unique<GameScene>(&m_ctx, level); };

        m_scenes.Change(m_ctx.makeTitle());
        NARAGAME_INFO("Mario: scene/state demo (title -> game <-> pause, file-loaded levels)");
    }

    void OnStop() override {
        m_tilesTexture.reset();
        m_charTexture.reset();
        m_fontTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override { m_scenes.Update(dt); }
    void OnRender() override { m_scenes.Render(); }

private:
    static AppConfig MakeConfig() {
        AppConfig config;
        config.title = "NaraGame - Super Mario";
        config.width = static_cast<uint32_t>(kScreenW);
        config.height = static_cast<uint32_t>(kScreenH);
        return config;
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_tilesTexture;
    std::unique_ptr<RHITexture> m_charTexture;
    std::unique_ptr<RHITexture> m_fontTexture;
    BitmapFont m_font;
    Font m_propFont;
    TextureAtlas m_chars;
    AudioClip m_jump;
    AudioClip m_coin;
    AudioClip m_stomp;
    AudioClip m_bgm;
    GameContext m_ctx;
    SceneManager m_scenes;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new MarioApp();
}
