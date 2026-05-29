// Flappy Bird (플래피버드) — small enough that the engine's physics + tile
// machinery aren't worth pulling in. The bird is a single AABB with manual
// gravity + flap impulse; pipes are an ever-shrinking deque of AABBs that
// scroll left. Collision is two pairs of AABB overlaps per pipe. Score
// increments the first frame the bird's left edge passes a pipe's right edge.
//
// Controls: Space jump / start / restart, ESC quit.

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/audio/AudioClip.h>
#include <NaraGame/audio/AudioDevice.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/BitmapFont.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <random>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

// World — letterbox-ish portrait that reads as one screenful of sky.
constexpr float kWidth     = 480.0f;
constexpr float kHeight    = 720.0f;
constexpr float kGroundH   = 80.0f;
constexpr float kCeiling   = 0.0f;
constexpr float kFloor     = kHeight - kGroundH;

// Bird.
constexpr float kBirdX     = 120.0f;
constexpr float kBirdW     = 36.0f;
constexpr float kBirdH     = 26.0f;
constexpr float kGravity   = 1700.0f;   // px/s^2
constexpr float kFlapVy    = -520.0f;   // upward impulse per flap
constexpr float kVyClamp   = 800.0f;    // terminal-ish velocity, both ways

// Pipes.
constexpr float kPipeW     = 70.0f;
constexpr float kPipeCapH  = 22.0f;
constexpr float kGapH      = 170.0f;
constexpr float kScrollVx  = -200.0f;   // px/s, pipes move left
constexpr float kSpawnDx   = 230.0f;    // distance between adjacent pipes
constexpr float kGapMargin = 90.0f;     // min distance gap-center stays from ceiling/floor

// AABB intersection — half-open rects, standard.
bool RectsOverlap(float ax, float ay, float aw, float ah,
                  float bx, float by, float bw, float bh) {
    return !(ax + aw <= bx || bx + bw <= ax
          || ay + ah <= by || by + bh <= ay);
}

} // namespace

class FlappyApp : public Application {
public:
    FlappyApp() : Application(MakeConfig()), m_rng(std::random_device{}()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;
        m_fontTexture = LoadTexture(GetDevice(), assets + "/font.png");
        m_font = BitmapFont(m_fontTexture.get(), ' ', 16, 14, 24);
        m_font.SetTracking(-2);

        m_flapClip  = LoadWav(assets + "/flap.wav");
        m_scoreClip = LoadWav(assets + "/score.wav");
        m_dieClip   = LoadWav(assets + "/die.wav");

        ResetRound();
        NARAGAME_INFO("Flappy: Space to flap / start / restart, ESC to quit");
    }

    void OnStop() override {
        m_fontTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dtRaw) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const float dt = Min(dtRaw, 1.0f / 30.0f);  // step cap so a hitch doesn't tunnel the bird through pipes
        const bool flapPressed = Input::IsKeyPressed(Key::Space);

        if (m_state == State::Title) {
            if (flapPressed) {
                m_state = State::Playing;
                m_birdVy = kFlapVy; // first flap starts the round
                GetAudio().PlaySound(m_flapClip, 0.7f);
            }
            return;
        }

        if (m_state == State::Dead) {
            if (flapPressed) ResetRound();
            return;
        }

        // --- Playing ---
        if (flapPressed) {
            m_birdVy = kFlapVy;
            GetAudio().PlaySound(m_flapClip, 0.7f);
        }
        m_birdVy = Clamp(m_birdVy + kGravity * dt, -kVyClamp, kVyClamp);
        m_birdY += m_birdVy * dt;

        // Move + spawn pipes. The deque is roughly ordered by x descending
        // (left to right); when the rightmost pipe has scrolled in far
        // enough, push a new one off-screen to the right.
        for (Pipe& p : m_pipes) p.x += kScrollVx * dt;
        if (m_pipes.empty() || m_pipes.back().x < kWidth - kSpawnDx)
            SpawnPipe();
        while (!m_pipes.empty() && m_pipes.front().x + kPipeW < 0.0f)
            m_pipes.erase(m_pipes.begin());

        // Collisions: ground / ceiling first, then each pipe.
        if (m_birdY < kCeiling || m_birdY + kBirdH > kFloor) {
            Die();
            return;
        }
        for (const Pipe& p : m_pipes) {
            const float topH = p.gapY;                       // pipe extends from y=0 down to gapY
            const float botY = p.gapY + kGapH;               // bottom pipe starts here
            const float botH = kFloor - botY;
            if (RectsOverlap(kBirdX, m_birdY, kBirdW, kBirdH, p.x, 0.0f, kPipeW, topH) ||
                RectsOverlap(kBirdX, m_birdY, kBirdW, kBirdH, p.x, botY, kPipeW, botH)) {
                Die();
                return;
            }
        }

        // Score: first frame the bird has passed a pipe.
        for (Pipe& p : m_pipes) {
            if (!p.scored && p.x + kPipeW < kBirdX) {
                p.scored = true;
                ++m_score;
                GetAudio().PlaySound(m_scoreClip, 0.8f);
            }
        }
    }

    void OnRender() override {
        GetDevice().Clear(Color(0.45f, 0.70f, 0.92f, 1.0f));   // sky
        m_renderer.Begin(Renderer2D::ScreenCamera(kWidth, kHeight));

        // Pipes
        const Color pipeBody{0.28f, 0.70f, 0.30f, 1.0f};
        const Color pipeCap {0.18f, 0.52f, 0.22f, 1.0f};
        const Color pipeRim {0.10f, 0.35f, 0.14f, 1.0f};
        for (const Pipe& p : m_pipes) {
            const float topH = p.gapY;
            const float botY = p.gapY + kGapH;
            const float botH = kFloor - botY;
            // Top pipe.
            m_renderer.DrawQuad({p.x, 0.0f}, {kPipeW, topH}, pipeBody);
            m_renderer.DrawQuad({p.x - 4.0f, topH - kPipeCapH}, {kPipeW + 8.0f, kPipeCapH}, pipeCap);
            m_renderer.DrawQuad({p.x - 4.0f, topH - kPipeCapH}, {kPipeW + 8.0f, 2.0f}, pipeRim);
            // Bottom pipe.
            m_renderer.DrawQuad({p.x, botY}, {kPipeW, botH}, pipeBody);
            m_renderer.DrawQuad({p.x - 4.0f, botY}, {kPipeW + 8.0f, kPipeCapH}, pipeCap);
            m_renderer.DrawQuad({p.x - 4.0f, botY + kPipeCapH - 2.0f}, {kPipeW + 8.0f, 2.0f}, pipeRim);
        }

        // Ground
        m_renderer.DrawQuad({0.0f, kFloor}, {kWidth, kGroundH}, Color(0.78f, 0.62f, 0.28f, 1.0f));
        m_renderer.DrawQuad({0.0f, kFloor}, {kWidth, 4.0f},     Color(0.40f, 0.78f, 0.32f, 1.0f));

        // Bird — body, eye, beak. A few rectangles make a recognisable
        // shape without a sprite asset.
        const float bx = kBirdX, by = m_birdY;
        m_renderer.DrawQuad({bx, by}, {kBirdW, kBirdH}, Color(0.98f, 0.86f, 0.20f, 1.0f));
        m_renderer.DrawQuad({bx + kBirdW - 8.0f, by + 4.0f}, {6.0f, 6.0f}, Color(0.10f, 0.10f, 0.12f, 1.0f));
        m_renderer.DrawQuad({bx + kBirdW, by + 10.0f}, {6.0f, 6.0f}, Color(0.95f, 0.45f, 0.15f, 1.0f));

        // Score (top centre).
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", m_score);
        const Vec2 size = m_font.Measure(buf, 2.0f);
        m_renderer.DrawText(m_font, buf, {(kWidth - size.x) * 0.5f, 24.0f},
                            Color(1.0f, 1.0f, 1.0f, 1.0f), 2.0f);

        // Overlays per state.
        if (m_state == State::Title) {
            DrawCentered("FLAPPY", kHeight * 0.32f, 3.5f, Color(1, 1, 1, 1));
            DrawCentered("SPACE TO START", kHeight * 0.46f, 1.4f, Color(1, 1, 1, 1));
        } else if (m_state == State::Dead) {
            DrawCentered("GAME OVER", kHeight * 0.32f, 2.5f, Color(1.0f, 0.45f, 0.45f, 1.0f));
            DrawCentered("SPACE TO RESTART", kHeight * 0.46f, 1.4f, Color(1, 1, 1, 1));
        }

        m_renderer.End();
    }

private:
    enum class State { Title, Playing, Dead };

    struct Pipe {
        float x;
        float gapY;     // y of the top of the gap (top pipe extends 0..gapY)
        bool  scored = false;
    };

    static AppConfig MakeConfig() {
        AppConfig c;
        c.title = "NaraGame - Flappy";
        c.width = static_cast<uint32_t>(kWidth);
        c.height = static_cast<uint32_t>(kHeight);
        return c;
    }

    void ResetRound() {
        m_state = State::Title;
        m_birdY = kHeight * 0.45f;
        m_birdVy = 0.0f;
        m_pipes.clear();
        m_score = 0;
        // Seed one pipe near the right so the title screen doesn't look empty.
        SpawnPipe();
        m_pipes.back().x = kWidth + 60.0f;
    }

    void SpawnPipe() {
        std::uniform_real_distribution<float> dist(
            kGapMargin, kFloor - kGapH - kGapMargin);
        Pipe p;
        p.x = kWidth;
        p.gapY = dist(m_rng);
        m_pipes.push_back(p);
    }

    void Die() {
        m_state = State::Dead;
        m_birdVy = 0.0f;
        GetAudio().PlaySound(m_dieClip, 1.0f);
    }

    void DrawCentered(const std::string& s, float y, float scale, const Color& c) {
        const Vec2 size = m_font.Measure(s, scale);
        m_renderer.DrawText(m_font, s, {(kWidth - size.x) * 0.5f, y}, c, scale);
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_fontTexture;
    BitmapFont m_font;
    AudioClip  m_flapClip;
    AudioClip  m_scoreClip;
    AudioClip  m_dieClip;

    State m_state = State::Title;
    float m_birdY = 0.0f;
    float m_birdVy = 0.0f;
    std::vector<Pipe> m_pipes;
    int m_score = 0;
    std::mt19937 m_rng;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new FlappyApp();
}
