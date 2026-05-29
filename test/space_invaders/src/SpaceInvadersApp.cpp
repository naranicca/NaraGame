// Space Invaders (스페이스 인베이더) — collision unified through the engine's
// PhysicsWorld using sensors: aliens + player are kinematic sensor bodies (the
// game positions them), bullets + bombs are dynamic bodies that move by velocity
// (no gravity). The world's sensor pass reports overlaps as contacts.
// Controls: Left/Right move, Space fires, ESC quits, Enter restarts.

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/audio/AudioClip.h>
#include <NaraGame/audio/AudioDevice.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/io/ConfigFile.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/physics/Physics2D.h>
#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/ParticleSystem.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/TextureAtlas.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

constexpr const char* kSavePath = "invaders_save.cfg"; // relative to working dir

constexpr float kWidth = 720.0f;
constexpr float kHeight = 900.0f;
constexpr int kCols = 11;
constexpr int kRows = 5;
constexpr int kTotal = kCols * kRows;

constexpr float kAlienW = 33.0f;
constexpr float kAlienH = 24.0f;
constexpr float kSpacingX = 48.0f;
constexpr float kSpacingY = 40.0f;
constexpr float kStartX = 100.0f;
constexpr float kStartY = 120.0f;
constexpr float kMargin = 24.0f;
constexpr float kDropY = 24.0f;

constexpr float kPlayerW = 42.0f;
constexpr float kPlayerH = 26.0f;
constexpr float kPlayerY = kHeight - 70.0f;
constexpr float kPlayerSpeed = 480.0f;

constexpr float kBulletW = 6.0f;
constexpr float kBulletH = 18.0f;
constexpr float kBulletSpeed = 760.0f;

constexpr float kBombW = 10.0f;
constexpr float kBombH = 20.0f;
constexpr float kBombSpeed = 300.0f;
constexpr int kMaxBombs = 3;

enum BodyTag : uint32_t { TagAlien = 1, TagPlayer = 2, TagBullet = 3, TagBomb = 4 };

struct Alien {
    int row = 0;
    int col = 0;
    int bodyId = -1;
    bool alive = true;
};

} // namespace

class SpaceInvadersApp : public Application {
public:
    SpaceInvadersApp() : Application(MakeConfig()), m_rng(std::random_device{}()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;

        m_spritesTexture = LoadTexture(GetDevice(), assets + "/invaders.png");
        m_atlas = TextureAtlas(m_spritesTexture.get());
        m_atlas.Add("crabA", 0.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("crabB", 24.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("squidA", 48.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("squidB", 72.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("player", 0.0f, 18.0f, 36.0f, 20.0f);
        m_atlas.Add("bullet", 40.0f, 18.0f, 4.0f, 16.0f);
        m_atlas.Add("bomb", 46.0f, 18.0f, 6.0f, 16.0f);

        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");

        m_audio = &GetAudio();
        m_shoot = LoadWav(assets + "/shoot.wav");
        m_explosion = LoadWav(assets + "/explosion.wav");

        m_save.Load(kSavePath);
        m_hiScore = m_save.GetInt("hiscore", 0);

        ResetGame();
        NARAGAME_INFO("Space Invaders: hi-score %d (from %s)", m_hiScore, kSavePath);
    }

    void OnStop() override {
        m_spritesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        if (m_gameOver) {
            if (Input::IsKeyDown(Key::Enter))
                ResetGame();
            return;
        }

        UpdatePlayer(dt);
        UpdateAliens(dt);
        UpdateBombs(dt);

        m_world.Step(dt); // moves bullet + bombs, reports sensor overlaps
        ProcessContacts();
        CleanupProjectiles();
        m_particles.Update(dt);

        if (m_score > m_hiScore) {
            m_hiScore = m_score;
            m_save.SetInt("hiscore", m_hiScore);
            m_save.Save(kSavePath);
        }

        if (m_aliveCount == 0) {
            ++m_level;
            SpawnWave();
        }
    }

    void OnRender() override {
        GetDevice().Clear(Color(0.02f, 0.02f, 0.06f, 1.0f));
        m_renderer.Begin(Renderer2D::ScreenCamera(kWidth, kHeight));

        for (const Alien& a : m_aliens) {
            if (a.alive)
                m_renderer.DrawQuad(AlienPos(a), {kAlienW, kAlienH}, AlienSprite(a.row),
                                    RowTint(a.row));
        }
        for (int id : m_bombIds) {
            if (const Body* b = m_world.Get(id); b != nullptr && b->active)
                m_renderer.DrawQuad(b->position, {kBombW, kBombH}, m_atlas.Get("bomb"),
                                    Color(1.0f, 0.4f, 0.35f));
        }
        m_renderer.DrawQuad(m_playerPos, {kPlayerW, kPlayerH}, m_atlas.Get("player"),
                            Color(0.4f, 0.95f, 0.5f));
        if (const Body* bullet = m_world.Get(m_bulletId); bullet != nullptr && bullet->active)
            m_renderer.DrawQuad(bullet->position, {kBulletW, kBulletH}, m_atlas.Get("bullet"),
                                Color(1.0f, 1.0f, 0.6f));

        m_particles.Draw(m_renderer);

        DrawHud();
        if (m_gameOver) {
            DrawCentered("GAME  OVER", 370.0f, 90.0f, Color(1.0f, 0.35f, 0.3f));
            DrawCentered("FINAL SCORE " + std::to_string(m_score), 470.0f, 30.0f,
                         Color(0.9f, 0.9f, 1.0f));
            DrawCentered("PRESS ENTER TO RESTART", 520.0f, 26.0f, Color(0.6f, 0.6f, 0.7f));
        }
        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig config;
        config.title = "NaraGame - Space Invaders";
        config.width = static_cast<uint32_t>(kWidth);
        config.height = static_cast<uint32_t>(kHeight);
        return config;
    }

    void ResetGame() {
        m_score = 0;
        m_lives = 3;
        m_level = 1;
        m_gameOver = false;
        SpawnWave();
    }

    void SpawnWave() {
        m_world.Clear();
        m_world.SetGravity({0.0f, 0.0f});

        m_aliens.clear();
        m_aliens.reserve(kTotal);
        m_formation = {kStartX, kStartY};
        for (int row = 0; row < kRows; ++row) {
            for (int col = 0; col < kCols; ++col) {
                Alien a{row, col, -1, true};
                Body body;
                body.type = BodyType::Kinematic; // positioned by the game
                body.sensor = true;              // overlap-only
                body.tag = TagAlien;
                body.size = {kAlienW, kAlienH};
                body.position = AlienPos(a);
                a.bodyId = m_world.AddBody(body);
                m_aliens.push_back(a);
            }
        }
        m_aliveCount = kTotal;

        m_playerPos = {(kWidth - kPlayerW) * 0.5f, kPlayerY};
        Body player;
        player.type = BodyType::Kinematic;
        player.sensor = true;
        player.tag = TagPlayer;
        player.size = {kPlayerW, kPlayerH};
        player.position = m_playerPos;
        m_playerId = m_world.AddBody(player);

        Body bullet;
        bullet.type = BodyType::Dynamic;
        bullet.tag = TagBullet;
        bullet.size = {kBulletW, kBulletH};
        bullet.gravityScale = 0.0f;
        m_bulletId = m_world.AddBody(bullet);
        m_world.Get(m_bulletId)->active = false;

        for (int i = 0; i < kMaxBombs; ++i) {
            Body bomb;
            bomb.type = BodyType::Dynamic;
            bomb.tag = TagBomb;
            bomb.size = {kBombW, kBombH};
            bomb.gravityScale = 0.0f;
            m_bombIds[i] = m_world.AddBody(bomb);
            m_world.Get(m_bombIds[i])->active = false;
        }

        m_dir = 1.0f;
        m_bombTimer = 1.0f;
        m_animTimer = 0.0f;
        m_frame = 0;
    }

    Vec2 AlienPos(const Alien& a) const {
        return {m_formation.x + a.col * kSpacingX, m_formation.y + a.row * kSpacingY};
    }

    TextureRegion AlienSprite(int row) const {
        if (row == 0)
            return m_atlas.Get(m_frame ? "squidB" : "squidA");
        return m_atlas.Get(m_frame ? "crabB" : "crabA");
    }

    static Color RowTint(int row) {
        switch (row) {
            case 0: return Color(0.4f, 0.9f, 1.0f);
            case 1: return Color(1.0f, 0.5f, 0.8f);
            case 2: return Color(1.0f, 0.8f, 0.3f);
            case 3: return Color(0.6f, 1.0f, 0.5f);
            default: return Color(0.85f, 0.85f, 0.95f);
        }
    }

    static int RowPoints(int row) { return row == 0 ? 30 : (row <= 2 ? 20 : 10); }

    void EmitAlienBurst(const Alien& a) {
        const Vec2 p = AlienPos(a);
        const Color tint = RowTint(a.row);
        ParticleBurst burst;
        burst.position = {p.x + kAlienW * 0.5f, p.y + kAlienH * 0.5f};
        burst.count = 18;
        burst.minSpeed = 60.0f;
        burst.maxSpeed = 220.0f;
        burst.minLife = 0.3f;
        burst.maxLife = 0.6f;
        burst.startSize = 6.0f;
        burst.startColor = tint;
        burst.endColor = Color(tint.r, tint.g, tint.b, 0.0f);
        m_particles.Emit(burst);
    }

    void UpdatePlayer(float dt) {
        if (Input::IsKeyDown(Key::Left))
            m_playerPos.x -= kPlayerSpeed * dt;
        if (Input::IsKeyDown(Key::Right))
            m_playerPos.x += kPlayerSpeed * dt;
        m_playerPos.x = Clamp(m_playerPos.x, 0.0f, kWidth - kPlayerW);

        Body* bullet = m_world.Get(m_bulletId);
        if (Input::IsKeyDown(Key::Space) && bullet != nullptr && !bullet->active) {
            bullet->active = true;
            bullet->position = {m_playerPos.x + (kPlayerW - kBulletW) * 0.5f,
                                m_playerPos.y - kBulletH};
            bullet->velocity = {0.0f, -kBulletSpeed};
            if (m_shoot.Valid())
                m_audio->PlaySound(m_shoot, 0.4f);
        }

        if (Body* p = m_world.Get(m_playerId))
            p->position = m_playerPos; // keep the sensor body in sync
    }

    void UpdateAliens(float dt) {
        m_animTimer += dt;
        if (m_animTimer >= 0.5f) {
            m_animTimer = 0.0f;
            m_frame ^= 1;
        }

        const float thinning = 1.0f + (static_cast<float>(kTotal - m_aliveCount) / kTotal) * 2.2f;
        const float speed = 36.0f * (1.0f + (m_level - 1) * 0.25f) * thinning;
        m_formation.x += m_dir * speed * dt;

        float minX = kWidth, maxX = 0.0f, maxBottom = 0.0f;
        for (const Alien& a : m_aliens) {
            if (!a.alive)
                continue;
            const Vec2 p = AlienPos(a);
            minX = Min(minX, p.x);
            maxX = Max(maxX, p.x + kAlienW);
            maxBottom = Max(maxBottom, p.y + kAlienH);
        }

        if (m_dir > 0.0f && maxX >= kWidth - kMargin) {
            m_formation.x -= (maxX - (kWidth - kMargin));
            m_dir = -1.0f;
            m_formation.y += kDropY;
        } else if (m_dir < 0.0f && minX <= kMargin) {
            m_formation.x += (kMargin - minX);
            m_dir = 1.0f;
            m_formation.y += kDropY;
        }

        // Sync the kinematic sensor bodies to the formation.
        for (const Alien& a : m_aliens) {
            if (!a.alive)
                continue;
            if (Body* ab = m_world.Get(a.bodyId))
                ab->position = AlienPos(a);
        }

        if (maxBottom >= m_playerPos.y)
            m_gameOver = true;
    }

    void UpdateBombs(float dt) {
        m_bombTimer -= dt;
        if (m_bombTimer > 0.0f || m_aliveCount == 0)
            return;
        m_bombTimer = std::uniform_real_distribution<float>(0.6f, 1.5f)(m_rng);

        Body* slot = nullptr;
        for (int id : m_bombIds) {
            Body* b = m_world.Get(id);
            if (b != nullptr && !b->active) { slot = b; break; }
        }
        if (slot == nullptr)
            return;

        std::vector<const Alien*> living;
        for (const Alien& a : m_aliens)
            if (a.alive)
                living.push_back(&a);
        if (living.empty())
            return;
        const Alien* shooter =
            living[std::uniform_int_distribution<size_t>(0, living.size() - 1)(m_rng)];
        const Vec2 p = AlienPos(*shooter);
        slot->active = true;
        slot->position = {p.x + (kAlienW - kBombW) * 0.5f, p.y + kAlienH};
        slot->velocity = {0.0f, kBombSpeed};
    }

    void ProcessContacts() {
        bool bulletConsumed = false;
        for (const Contact& c : m_world.Contacts()) {
            const Body* dyn = m_world.Get(c.a); // dynamic side
            const Body* sen = m_world.Get(c.b); // sensor side
            if (dyn == nullptr || sen == nullptr)
                continue;

            if (dyn->tag == TagBullet && sen->tag == TagAlien && !bulletConsumed) {
                for (Alien& a : m_aliens) {
                    if (a.alive && a.bodyId == c.b) {
                        a.alive = false;
                        --m_aliveCount;
                        m_score += RowPoints(a.row);
                        if (Body* ab = m_world.Get(a.bodyId))
                            ab->active = false;
                        if (Body* bullet = m_world.Get(m_bulletId))
                            bullet->active = false;
                        bulletConsumed = true;
                        EmitAlienBurst(a);
                        if (m_explosion.Valid())
                            m_audio->PlaySound(m_explosion, 0.4f);
                        break;
                    }
                }
            } else if (dyn->tag == TagBomb && sen->tag == TagPlayer) {
                if (Body* bomb = m_world.Get(c.a))
                    bomb->active = false;
                if (--m_lives <= 0)
                    m_gameOver = true;
            }
        }
    }

    void CleanupProjectiles() {
        if (Body* bullet = m_world.Get(m_bulletId))
            if (bullet->active && bullet->position.y + kBulletH < 0.0f)
                bullet->active = false;
        for (int id : m_bombIds) {
            if (Body* b = m_world.Get(id))
                if (b->active && b->position.y > kHeight)
                    b->active = false;
        }
    }

    void DrawHud() {
        m_renderer.DrawText(m_font, "SCORE " + std::to_string(m_score), {16.0f, 14.0f},
                            Color(0.95f, 0.95f, 1.0f), 28.0f);
        m_renderer.DrawText(m_font, "LEVEL " + std::to_string(m_level), {16.0f, 46.0f},
                            Color(0.7f, 0.8f, 1.0f), 22.0f);
        const std::string hi = "HI-SCORE " + std::to_string(m_hiScore);
        const float hiW = m_font.Measure(hi, 28.0f).x;
        m_renderer.DrawText(m_font, hi, {(kWidth - hiW) * 0.5f, 14.0f}, Color(1.0f, 0.85f, 0.3f),
                            28.0f);
        for (int i = 0; i < m_lives; ++i) {
            const Vec2 pos{kWidth - 40.0f - i * 40.0f, 18.0f};
            m_renderer.DrawQuad(pos, {32.0f, 20.0f}, m_atlas.Get("player"),
                                Color(0.4f, 0.95f, 0.5f));
        }
    }

    void DrawCentered(const std::string& text, float y, float pixelSize, const Color& color) {
        const float textWidth = m_font.Measure(text, pixelSize).x;
        m_renderer.DrawText(m_font, text, {(kWidth - textWidth) * 0.5f, y}, color, pixelSize);
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_spritesTexture;
    TextureAtlas m_atlas;
    MSDFFont m_font;

    PhysicsWorld m_world;
    ParticleSystem m_particles;
    int m_playerId = -1;
    int m_bulletId = -1;
    int m_bombIds[kMaxBombs] = {-1, -1, -1};

    std::vector<Alien> m_aliens;
    int m_aliveCount = 0;
    Vec2 m_formation;
    float m_dir = 1.0f;
    float m_animTimer = 0.0f;
    int m_frame = 0;

    Vec2 m_playerPos;
    float m_bombTimer = 1.0f;

    int m_score = 0;
    int m_lives = 3;
    int m_level = 1;
    bool m_gameOver = false;

    ConfigFile m_save;
    int m_hiScore = 0;

    AudioDevice* m_audio = nullptr;
    AudioClip m_shoot;
    AudioClip m_explosion;

    std::mt19937 m_rng;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new SpaceInvadersApp();
}
