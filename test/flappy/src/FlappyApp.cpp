// Flappy Bird (플래피버드) — small enough that the engine's physics + tile
// machinery aren't worth pulling in. The bird is a single AABB with manual
// gravity + flap impulse; pipes are an ever-shrinking deque of AABBs that
// scroll left. Collision is two pairs of AABB overlaps per pipe. Score
// increments the first frame the bird's left edge passes a pipe's right edge.
//
// Controls: Space jump / start / restart, ESC quit.

#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/audio/AudioClip.h>
#include <NaraGame/audio/AudioDevice.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/ParticleSystem.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <cmath>
#include <cstdio>
#include <fstream>
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

// Pipes — base values (used when difficulty = 0). Per-round numbers are
// scaled up/down by Difficulty() so the game gets harder as the score climbs.
constexpr float kPipeW     = 70.0f;
constexpr float kPipeCapH  = 22.0f;
constexpr float kGapH      = 195.0f;     // gap shrinks toward 145 at full difficulty
constexpr float kScrollVx  = -200.0f;    // speeds up toward -310 at full difficulty
constexpr float kSpawnDx   = 230.0f;     // closer spawning toward 195 at full difficulty
constexpr float kGapMargin = 90.0f;      // min distance gap stays from ceiling/floor
constexpr float kDifficultyScoreCap = 30.0f;  // score at which scaling maxes out

constexpr float kDeadLockoutS = 3.0f;    // ignore Space for this long after death

// Parallax background. Hills sit just above the ground line and drift slowly;
// clouds float a bit faster, halfway between hills and the foreground pipes.
constexpr float kCloudW   = 60.0f;
constexpr float kCloudH   = 30.0f;
constexpr float kCloudVx  = -30.0f;
constexpr int   kCloudCnt = 5;
constexpr float kHillsW   = 480.0f;
constexpr float kHillsH   = 80.0f;
constexpr float kHillsVx  = -12.0f;

// Medals — single sprite sheet with four 64x64 frames (bronze, silver, gold,
// platinum). Earned at 5 / 10 / 20 / 30 score thresholds; below 5 no medal
// shows, so the worst run isn't celebrated.
constexpr float kMedalPx   = 64.0f;     // source frame size in medals.png
constexpr float kMedalSize = 80.0f;     // display size on the Dead overlay

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
        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");

        m_birdTexture = LoadTexture(GetDevice(), assets + "/bird.png");
        m_birdRegion  = TextureRegion::FromPixels(
            m_birdTexture.get(), 0.0f, 0.0f,
            static_cast<float>(m_birdTexture->Width()),
            static_cast<float>(m_birdTexture->Height()));

        m_cloudTexture = LoadTexture(GetDevice(), assets + "/cloud.png");
        m_cloudRegion  = TextureRegion::FromPixels(
            m_cloudTexture.get(), 0.0f, 0.0f, kCloudW, kCloudH);
        m_hillsTexture = LoadTexture(GetDevice(), assets + "/hills.png");
        m_hillsRegion  = TextureRegion::FromPixels(
            m_hillsTexture.get(), 0.0f, 0.0f, kHillsW, kHillsH);

        m_medalsTexture = LoadTexture(GetDevice(), assets + "/medals.png");
        for (int i = 0; i < 4; ++i) {
            m_medalRegions[i] = TextureRegion::FromPixels(
                m_medalsTexture.get(), i * kMedalPx, 0.0f, kMedalPx, kMedalPx);
        }

        // Pre-populate the cloud pool spread across the screen so the title
        // doesn't open with an empty sky and clouds drifting in from the right.
        std::uniform_real_distribution<float> xDist(0.0f, kWidth);
        for (int i = 0; i < kCloudCnt; ++i) m_clouds.push_back(SpawnCloud(xDist(m_rng)));

        m_flapClip  = LoadWav(assets + "/flap.wav");
        m_scoreClip = LoadWav(assets + "/score.wav");
        m_dieClip   = LoadWav(assets + "/die.wav");

        LoadHighScore();
        ResetRound();
        NARAGAME_INFO("Flappy: Space to flap / start / restart, ESC to quit");
    }

    void OnStop() override {
        m_renderer.Shutdown();
    }

    void OnUpdate(float dtRaw) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const float dt = Min(dtRaw, 1.0f / 30.0f);  // step cap so a hitch doesn't tunnel the bird through pipes
        const bool flapPressed = Input::IsKeyPressed(Key::Space);

        // Particles tick every frame regardless of state — they're spawned
        // in Die() right before transitioning to Dead, and need to keep
        // animating while the GAME OVER overlay is showing.
        m_particles.Update(dt);

        // Parallax always animates — clouds drift and hills creep on every
        // screen, including Title and Dead, so the world never freezes.
        m_hillsOffsetX += kHillsVx * dt;
        if (m_hillsOffsetX <= -kHillsW) m_hillsOffsetX += kHillsW;
        for (Cloud& cl : m_clouds) {
            cl.x += kCloudVx * dt;
            if (cl.x + kCloudW * cl.scale < 0.0f) cl = SpawnCloud(kWidth + 20.0f);
        }

        if (m_state == State::Title) {
            m_titleTimer += dt;
            if (flapPressed) {
                m_state = State::Playing;
                m_birdVy = kFlapVy; // first flap starts the round
                GetAudio().PlaySound(m_flapClip, 0.7f);
            }
            return;
        }

        if (m_state == State::Dead) {
            // Brief lockout so a Space held at the moment of impact doesn't
            // immediately consume the death frame and restart. The HUD hides
            // "SPACE TO RESTART" until the timer clears.
            m_deadTimer = Max(0.0f, m_deadTimer - dt);
            if (flapPressed && m_deadTimer <= 0.0f) ResetRound();
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
        // enough, push a new one off-screen to the right. Speed and spacing
        // shrink with score via CurrentScrollSpeed / CurrentSpawnDx.
        const float scroll = CurrentScrollSpeed();
        for (Pipe& p : m_pipes) p.x += scroll * dt;
        if (m_pipes.empty() || m_pipes.back().x < kWidth - CurrentSpawnDx())
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
            const float botY = p.gapY + p.gapH;              // bottom pipe starts here
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

        // Parallax background — hills behind, clouds in front of hills,
        // pipes/ground/bird in front of both. Hills are 480 wide and drawn
        // twice side-by-side so the wrap is seamless as m_hillsOffsetX
        // marches from 0 to -kHillsW and rolls back.
        const float hillsY = kFloor - kHillsH;
        m_renderer.DrawQuad({m_hillsOffsetX, hillsY},
                            {kHillsW, kHillsH}, m_hillsRegion);
        m_renderer.DrawQuad({m_hillsOffsetX + kHillsW, hillsY},
                            {kHillsW, kHillsH}, m_hillsRegion);
        for (const Cloud& cl : m_clouds) {
            m_renderer.DrawQuad({cl.x, cl.y},
                                {kCloudW * cl.scale, kCloudH * cl.scale},
                                m_cloudRegion);
        }

        // Pipes
        const Color pipeBody{0.28f, 0.70f, 0.30f, 1.0f};
        const Color pipeCap {0.18f, 0.52f, 0.22f, 1.0f};
        const Color pipeRim {0.10f, 0.35f, 0.14f, 1.0f};
        for (const Pipe& p : m_pipes) {
            const float topH = p.gapY;
            const float botY = p.gapY + p.gapH;
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

        // Bird sprite, pitched by current vertical velocity so the nose comes
        // up on a flap and drops on a fall. Eye/beak/wing detail is baked
        // into bird.png, so one rotated textured quad replaces what used to
        // be three coloured quads with manual rotation math.
        //
        // On the title screen the bird isn't being simulated, so it would
        // sit dead still — instead we offset it by a small sine bob and
        // give the pitch the cosine of the same wave, so the nose lifts on
        // the way up and dips on the way down, just like an in-flight flap.
        float drawBirdY = m_birdY;
        float pitch = Clamp(m_birdVy / 500.0f, -0.6f, 1.4f);
        if (m_state == State::Title) {
            constexpr float kHoverFreq = 4.0f;   // rad/s — gentle ~1.5s period
            constexpr float kHoverAmp  = 8.0f;   // pixels
            const float w = m_titleTimer * kHoverFreq;
            drawBirdY = m_birdY + std::sin(w) * kHoverAmp;
            pitch     = std::cos(w) * 0.22f;
        }
        const Vec2 birdCenter{kBirdX + kBirdW * 0.5f, drawBirdY + kBirdH * 0.5f};
        m_renderer.DrawQuad(birdCenter, {kBirdW, kBirdH}, pitch, m_birdRegion);

        // Feathers on top of the bird so the burst reads as coming off the body.
        m_particles.Draw(m_renderer);

        // Score (top centre). MSDFFont takes pixel-size directly (em-box
        // height in pixels), so these numbers are the actual rendered size.
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%d", m_score);
        const Vec2 scoreSize = m_font.Measure(buf, 56.0f);
        m_renderer.DrawText(m_font, buf, {(kWidth - scoreSize.x) * 0.5f, 18.0f},
                            Color(1.0f, 1.0f, 1.0f, 1.0f), 56.0f);

        // Overlays per state.
        char hs[32];
        if (m_state == State::Title) {
            // FLAPPY at the top; bird hovers in the middle band; SPACE/BEST
            // sit below the bird's hover zone so the iconic hovering bird
            // isn't covered by text.
            DrawCentered("FLAPPY", kHeight * 0.18f, 100.0f, Color(1, 1, 1, 1));
            DrawCentered("SPACE TO START", kHeight * 0.58f, 28.0f, Color(1, 1, 1, 1));
            if (m_highScore > 0) {
                std::snprintf(hs, sizeof(hs), "BEST  %d", m_highScore);
                DrawCentered(hs, kHeight * 0.66f, 26.0f, Color(1.0f, 0.95f, 0.40f, 1.0f));
            }
        } else if (m_state == State::Dead) {
            DrawCentered("GAME OVER", kHeight * 0.20f, 64.0f, Color(1.0f, 0.45f, 0.45f, 1.0f));
            // Medal sits between GAME OVER and the score line. Skipped
            // entirely when the run didn't clear the bronze threshold —
            // a sub-5 round shouldn't feel rewarded.
            const int tier = MedalTier();
            if (tier >= 0) {
                m_renderer.DrawQuad(
                    {(kWidth - kMedalSize) * 0.5f, kHeight * 0.27f},
                    {kMedalSize, kMedalSize}, m_medalRegions[tier]);
            }
            if (m_newBest) {
                std::snprintf(hs, sizeof(hs), "NEW BEST  %d", m_highScore);
                DrawCentered(hs, kHeight * 0.46f, 32.0f, Color(1.0f, 0.95f, 0.40f, 1.0f));
            } else {
                std::snprintf(hs, sizeof(hs), "BEST  %d", m_highScore);
                DrawCentered(hs, kHeight * 0.46f, 30.0f, Color(0.90f, 0.90f, 0.95f, 1.0f));
            }
            // Suppress the restart hint while the lockout is active.
            if (m_deadTimer <= 0.0f)
                DrawCentered("SPACE TO RESTART", kHeight * 0.54f, 28.0f, Color(1, 1, 1, 1));
        }

        m_renderer.End();
    }

private:
    enum class State { Title, Playing, Dead };

    struct Pipe {
        float x;
        float gapY;     // y of the top of the gap (top pipe extends 0..gapY)
        float gapH;     // captured at spawn — preserves the gap a pipe was born with
        bool  scored = false;
    };

    struct Cloud { float x, y, scale; };

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
        m_titleTimer = 0.0f;
        m_pipes.clear();
        m_score = 0;
        m_newBest = false;
        m_particles.Clear();
        // Seed one pipe near the right so the title screen doesn't look empty.
        SpawnPipe();
        m_pipes.back().x = kWidth + 60.0f;
    }

    // 0 at score 0, ramps to 1 by kDifficultyScoreCap, then plateaus.
    float Difficulty() const {
        return Min(1.0f, m_score / kDifficultyScoreCap);
    }
    float CurrentScrollSpeed() const { return kScrollVx + (-110.0f) * Difficulty(); }
    float CurrentSpawnDx()    const { return kSpawnDx + (-35.0f)  * Difficulty(); }
    float CurrentGapH()       const { return kGapH    + (-50.0f)  * Difficulty(); }

    // -1 = no medal, 0 = bronze, 1 = silver, 2 = gold, 3 = platinum.
    // Thresholds are tuned to the difficulty curve: 5 is "you saw a few
    // pipes", 10 is "you're getting it", 20 is solid, 30 = at the cap.
    int MedalTier() const {
        if (m_score >= 30) return 3;
        if (m_score >= 20) return 2;
        if (m_score >= 10) return 1;
        if (m_score >=  5) return 0;
        return -1;
    }

    Cloud SpawnCloud(float x) {
        std::uniform_real_distribution<float> yDist(20.0f, kHeight * 0.45f);
        std::uniform_real_distribution<float> sDist(0.7f, 1.3f);
        return Cloud{ x, yDist(m_rng), sDist(m_rng) };
    }

    void SpawnPipe() {
        Pipe p;
        p.x = kWidth;
        p.gapH = CurrentGapH();
        std::uniform_real_distribution<float> dist(
            kGapMargin, kFloor - p.gapH - kGapMargin);
        p.gapY = dist(m_rng);
        m_pipes.push_back(p);
    }

    void Die() {
        m_state = State::Dead;
        m_birdVy = 0.0f;
        m_deadTimer = kDeadLockoutS;
        GetAudio().PlaySound(m_dieClip, 1.0f);

        // Yellow-feather burst out of the bird's centre with gravity, so the
        // feathers spray outward then settle downward like a real crash.
        ParticleBurst burst;
        burst.position  = {kBirdX + kBirdW * 0.5f, m_birdY + kBirdH * 0.5f};
        burst.count     = 30;
        burst.minSpeed  = 80.0f;
        burst.maxSpeed  = 280.0f;
        burst.minLife   = 0.5f;
        burst.maxLife   = 1.1f;
        burst.startSize = 12.0f;
        burst.endSize   = 2.0f;
        burst.startColor = Color(0.98f, 0.86f, 0.20f, 1.0f);  // bird yellow
        burst.endColor   = Color(0.98f, 0.86f, 0.20f, 0.0f);  // fade out
        burst.gravity    = {0.0f, 800.0f};
        m_particles.Emit(burst);

        m_newBest = false;
        if (m_score > m_highScore) {
            m_highScore = m_score;
            SaveHighScore();
            m_newBest = true;
        }
    }

    static constexpr const char* kHighScorePath = "flappy_high.txt";
    void LoadHighScore() {
        std::ifstream in(kHighScorePath);
        if (in) in >> m_highScore;
    }
    void SaveHighScore() {
        std::ofstream out(kHighScorePath);
        if (out) out << m_highScore << '\n';
    }

    void DrawCentered(const std::string& s, float y, float pixelSize, const Color& c) {
        const Vec2 size = m_font.Measure(s, pixelSize);
        m_renderer.DrawText(m_font, s, {(kWidth - size.x) * 0.5f, y}, c, pixelSize);
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_birdTexture;
    TextureRegion m_birdRegion;
    std::unique_ptr<RHITexture> m_cloudTexture;
    TextureRegion m_cloudRegion;
    std::unique_ptr<RHITexture> m_hillsTexture;
    TextureRegion m_hillsRegion;
    std::unique_ptr<RHITexture> m_medalsTexture;
    TextureRegion m_medalRegions[4];
    std::vector<Cloud> m_clouds;
    float m_hillsOffsetX = 0.0f;
    MSDFFont   m_font;
    AudioClip  m_flapClip;
    AudioClip  m_scoreClip;
    AudioClip  m_dieClip;

    State m_state = State::Title;
    float m_birdY = 0.0f;
    float m_birdVy = 0.0f;
    std::vector<Pipe> m_pipes;
    int m_score = 0;
    int m_highScore = 0;
    bool m_newBest = false;
    float m_deadTimer = 0.0f;
    float m_titleTimer = 0.0f;
    std::mt19937 m_rng;
    ParticleSystem m_particles;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new FlappyApp();
}
