// Space Invaders (스페이스 인베이더) — test game on the Nara engine.
//
// Exercises the engine: window + D3D11 + Renderer2D batching + a packed sprite
// atlas + bitmap-font text + keyboard input. Controls: Left/Right move,
// Space fires, ESC quits, Enter restarts after game over.

#include <nara/assets/TextureLoader.h>
#include <nara/core/Application.h>
#include <nara/core/EntryPoint.h>
#include <nara/core/Log.h>
#include <nara/audio/AudioClip.h>
#include <nara/audio/AudioDevice.h>
#include <nara/input/Input.h>
#include <nara/io/ConfigFile.h>
#include <nara/math/Math.h>
#include <nara/renderer/BitmapFont.h>
#include <nara/renderer/Renderer2D.h>
#include <nara/renderer/TextureAtlas.h>
#include <nara/renderer/rhi/RHIDevice.h>

#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef NARA_ASSETS_DIR
#define NARA_ASSETS_DIR "assets"
#endif

using namespace nara;

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

bool Overlaps(const Vec2& aPos, const Vec2& aSize, const Vec2& bPos, const Vec2& bSize) {
    return aPos.x < bPos.x + bSize.x && aPos.x + aSize.x > bPos.x &&
           aPos.y < bPos.y + bSize.y && aPos.y + aSize.y > bPos.y;
}

struct Alien {
    int row = 0;
    int col = 0;
    bool alive = true;
};

struct Bomb {
    Vec2 position;
    bool active = false;
};

} // namespace

class SpaceInvadersApp : public Application {
public:
    SpaceInvadersApp() : Application(MakeConfig()), m_rng(std::random_device{}()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARA_ASSETS_DIR;

        m_spritesTexture = LoadTexture(GetDevice(), assets + "/invaders.png");
        m_atlas = TextureAtlas(m_spritesTexture.get());
        m_atlas.Add("crabA", 0.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("crabB", 24.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("squidA", 48.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("squidB", 72.0f, 0.0f, 24.0f, 16.0f);
        m_atlas.Add("player", 0.0f, 18.0f, 36.0f, 20.0f);
        m_atlas.Add("bullet", 40.0f, 18.0f, 4.0f, 16.0f);
        m_atlas.Add("bomb", 46.0f, 18.0f, 6.0f, 16.0f);

        m_fontTexture = LoadTexture(GetDevice(), assets + "/font.png");
        m_font = BitmapFont(m_fontTexture.get(), ' ', 16, 14, 24);
        m_font.SetTracking(-2);

        m_audio = &GetAudio();
        m_shoot = LoadWav(assets + "/shoot.wav");
        m_explosion = LoadWav(assets + "/explosion.wav");

        // Persisted high score (survives across runs).
        m_save.Load(kSavePath);
        m_hiScore = m_save.GetInt("hiscore", 0);

        ResetGame();
        NARA_INFO("Space Invaders: hi-score %d (from %s)", m_hiScore, kSavePath);
    }

    void OnStop() override {
        m_spritesTexture.reset();
        m_fontTexture.reset();
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
        UpdateBullet(dt);
        UpdateAliens(dt);
        UpdateBombs(dt);
        CheckCollisions();

        // Beat the record? Persist it immediately.
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

        // Aliens (animated, colored per row).
        for (const Alien& a : m_aliens) {
            if (!a.alive)
                continue;
            m_renderer.DrawQuad(AlienPos(a), {kAlienW, kAlienH}, AlienSprite(a.row), RowTint(a.row));
        }

        // Bombs, player, bullet.
        for (const Bomb& b : m_bombs) {
            if (b.active)
                m_renderer.DrawQuad(b.position, {kBombW, kBombH}, m_atlas.Get("bomb"),
                                    Color(1.0f, 0.4f, 0.35f));
        }
        m_renderer.DrawQuad(m_playerPos, {kPlayerW, kPlayerH}, m_atlas.Get("player"),
                            Color(0.4f, 0.95f, 0.5f));
        if (m_bulletActive)
            m_renderer.DrawQuad(m_bulletPos, {kBulletW, kBulletH}, m_atlas.Get("bullet"),
                                Color(1.0f, 1.0f, 0.6f));

        DrawHud();
        if (m_gameOver) {
            DrawCentered("GAME  OVER", 380.0f, 4.0f, Color(1.0f, 0.35f, 0.3f));
            DrawCentered("FINAL SCORE " + std::to_string(m_score), 450.0f, 1.6f,
                         Color(0.9f, 0.9f, 1.0f));
            DrawCentered("PRESS ENTER TO RESTART", 500.0f, 1.4f, Color(0.6f, 0.6f, 0.7f));
        }

        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig config;
        config.title = "Nara - Space Invaders";
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
        m_aliens.clear();
        m_aliens.reserve(kTotal);
        for (int row = 0; row < kRows; ++row)
            for (int col = 0; col < kCols; ++col)
                m_aliens.push_back(Alien{row, col, true});
        m_aliveCount = kTotal;

        m_formation = {kStartX, kStartY};
        m_dir = 1.0f;
        m_bulletActive = false;
        for (Bomb& b : m_bombs)
            b.active = false;

        m_playerPos = {(kWidth - kPlayerW) * 0.5f, kPlayerY};
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
            case 0: return Color(0.4f, 0.9f, 1.0f);  // squid - cyan
            case 1: return Color(1.0f, 0.5f, 0.8f);  // crab - pink
            case 2: return Color(1.0f, 0.8f, 0.3f);  // crab - amber
            case 3: return Color(0.6f, 1.0f, 0.5f);  // crab - green
            default: return Color(0.85f, 0.85f, 0.95f);
        }
    }

    static int RowPoints(int row) { return row == 0 ? 30 : (row <= 2 ? 20 : 10); }

    void UpdatePlayer(float dt) {
        if (Input::IsKeyDown(Key::Left))
            m_playerPos.x -= kPlayerSpeed * dt;
        if (Input::IsKeyDown(Key::Right))
            m_playerPos.x += kPlayerSpeed * dt;
        m_playerPos.x = Clamp(m_playerPos.x, 0.0f, kWidth - kPlayerW);

        if (Input::IsKeyDown(Key::Space) && !m_bulletActive) {
            m_bulletActive = true;
            m_bulletPos = {m_playerPos.x + (kPlayerW - kBulletW) * 0.5f, m_playerPos.y - kBulletH};
            if (m_shoot.Valid())
                m_audio->PlaySound(m_shoot, 0.4f);
        }
    }

    void UpdateBullet(float dt) {
        if (!m_bulletActive)
            return;
        m_bulletPos.y -= kBulletSpeed * dt;
        if (m_bulletPos.y + kBulletH < 0.0f)
            m_bulletActive = false;
    }

    void UpdateAliens(float dt) {
        m_animTimer += dt;
        if (m_animTimer >= 0.5f) {
            m_animTimer = 0.0f;
            m_frame ^= 1;
        }

        // Speed grows as the wave thins out and with each level.
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

        if (maxBottom >= m_playerPos.y)
            m_gameOver = true; // aliens reached the player
    }

    void UpdateBombs(float dt) {
        for (Bomb& b : m_bombs) {
            if (!b.active)
                continue;
            b.position.y += kBombSpeed * dt;
            if (b.position.y > kHeight)
                b.active = false;
        }

        m_bombTimer -= dt;
        if (m_bombTimer > 0.0f || m_aliveCount == 0)
            return;
        m_bombTimer = std::uniform_real_distribution<float>(0.6f, 1.5f)(m_rng);

        // Find a free bomb slot and a random living alien to fire from.
        Bomb* slot = nullptr;
        for (Bomb& b : m_bombs)
            if (!b.active) {
                slot = &b;
                break;
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
    }

    void CheckCollisions() {
        // Player bullet vs aliens.
        if (m_bulletActive) {
            for (Alien& a : m_aliens) {
                if (!a.alive)
                    continue;
                if (Overlaps(m_bulletPos, {kBulletW, kBulletH}, AlienPos(a), {kAlienW, kAlienH})) {
                    a.alive = false;
                    --m_aliveCount;
                    m_score += RowPoints(a.row);
                    m_bulletActive = false;
                    if (m_explosion.Valid())
                        m_audio->PlaySound(m_explosion, 0.4f);
                    break;
                }
            }
        }

        // Bombs vs player.
        for (Bomb& b : m_bombs) {
            if (!b.active)
                continue;
            if (Overlaps(b.position, {kBombW, kBombH}, m_playerPos, {kPlayerW, kPlayerH})) {
                b.active = false;
                if (--m_lives <= 0)
                    m_gameOver = true;
            }
        }
    }

    void DrawHud() {
        m_renderer.DrawText(m_font, "SCORE " + std::to_string(m_score), {16.0f, 16.0f},
                            Color(0.95f, 0.95f, 1.0f), 1.4f);
        m_renderer.DrawText(m_font, "LEVEL " + std::to_string(m_level), {16.0f, 46.0f},
                            Color(0.7f, 0.8f, 1.0f), 1.0f);

        const std::string hi = "HI-SCORE " + std::to_string(m_hiScore);
        const float hiW = m_font.Measure(hi, 1.4f).x;
        m_renderer.DrawText(m_font, hi, {(kWidth - hiW) * 0.5f, 16.0f}, Color(1.0f, 0.85f, 0.3f),
                            1.4f);

        // Lives as little ships on the right.
        for (int i = 0; i < m_lives; ++i) {
            const Vec2 pos{kWidth - 40.0f - i * 40.0f, 18.0f};
            m_renderer.DrawQuad(pos, {32.0f, 20.0f}, m_atlas.Get("player"), Color(0.4f, 0.95f, 0.5f));
        }
    }

    void DrawCentered(const std::string& text, float y, float scale, const Color& color) {
        const float textWidth = m_font.Measure(text, scale).x;
        m_renderer.DrawText(m_font, text, {(kWidth - textWidth) * 0.5f, y}, color, scale);
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_spritesTexture;
    std::unique_ptr<RHITexture> m_fontTexture;
    TextureAtlas m_atlas;
    BitmapFont m_font;

    std::vector<Alien> m_aliens;
    int m_aliveCount = 0;
    Vec2 m_formation;
    float m_dir = 1.0f;
    float m_animTimer = 0.0f;
    int m_frame = 0;

    Vec2 m_playerPos;
    Vec2 m_bulletPos;
    bool m_bulletActive = false;

    Bomb m_bombs[kMaxBombs];
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

Application* nara::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new SpaceInvadersApp();
}
