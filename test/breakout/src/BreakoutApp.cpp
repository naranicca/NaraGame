// Breakout (벽돌깨기) — driven by the engine's shared 2D physics (PhysicsWorld).
//
// Ball = dynamic body (restitution 1, no gravity); walls/bricks = static; paddle
// = kinematic. The swept-AABB solver bounces the ball and reports contacts, which
// the game uses to break bricks and add paddle "english". Left/Right move, ESC quits.

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/physics/Physics2D.h>
#include <NaraGame/assets/MSDFFontLoader.h>
#include <NaraGame/renderer/MSDFFont.h>
#include <NaraGame/renderer/ParticleSystem.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/TextureAtlas.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {
constexpr float kBallSize = 14.0f;
constexpr float kPaddleW = 110.0f;
constexpr float kPaddleH = 18.0f;
constexpr float kPaddleSpeed = 700.0f;

enum Tag : uint32_t { TagBall = 1, TagPaddle = 2, TagBrick = 3, TagWall = 4 };

struct Brick {
    Vec2 position;
    Vec2 size;
    Color color;
    int sprite = 0;
    int bodyId = -1;
    bool alive = true;
};
} // namespace

class BreakoutApp : public Application {
public:
    BreakoutApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());
        const std::string assets = NARAGAME_ASSETS_DIR;

        m_brickSheetTexture = LoadTexture(GetDevice(), assets + "/bricks.png");
        m_brickSheet = SpriteSheet(m_brickSheetTexture.get(), 64, 32);

        m_spritesTexture = LoadTexture(GetDevice(), assets + "/sprites.png");
        m_atlas = TextureAtlas(m_spritesTexture.get());
        m_atlas.Add("ball", 0.0f, 0.0f, 32.0f, 32.0f);
        m_atlas.Add("paddle", 0.0f, 32.0f, 120.0f, 20.0f);

        m_font = LoadMSDFFont(GetDevice(), assets + "/msdf-inter.json");

        BuildWorld();
        NARAGAME_INFO("Breakout: Left/Right to move, ESC to quit (physics-driven)");
    }

    void OnStop() override {
        m_brickSheetTexture.reset();
        m_spritesTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dtRaw) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const float dt = Min(dtRaw, 1.0f / 30.0f);
        const float w = static_cast<float>(Config().width);

        // Drive the kinematic paddle directly from input.
        if (Body* paddle = m_world.Get(m_paddleId)) {
            float x = paddle->position.x;
            if (Input::IsKeyDown(Key::Left))
                x -= kPaddleSpeed * dt;
            if (Input::IsKeyDown(Key::Right))
                x += kPaddleSpeed * dt;
            paddle->position.x = Clamp(x, 0.0f, w - kPaddleW);
        }

        m_world.Step(dt);
        HandleContacts();
        m_particles.Update(dt);

        if (Body* ball = m_world.Get(m_ballId))
            if (ball->position.y > static_cast<float>(Config().height))
                ResetBall();

        if (AllBricksCleared())
            BuildWorld();
    }

    void OnRender() override {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        GetDevice().Clear(Color(0.05f, 0.06f, 0.10f, 1.0f));

        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));
        for (const Brick& brick : m_bricks) {
            if (brick.alive)
                m_renderer.DrawQuad(brick.position, brick.size, m_brickSheet.At(brick.sprite),
                                    brick.color);
        }
        if (Body* paddle = m_world.Get(m_paddleId))
            m_renderer.DrawQuad(paddle->position, {kPaddleW, kPaddleH}, m_atlas.Get("paddle"),
                                Color(0.9f, 0.9f, 0.95f, 1.0f));
        if (Body* ball = m_world.Get(m_ballId))
            m_renderer.DrawQuad(ball->position, {kBallSize, kBallSize}, m_atlas.Get("ball"),
                                Color(1.0f, 0.85f, 0.3f, 1.0f));

        m_particles.Draw(m_renderer);

        m_renderer.DrawText(m_font, "SCORE " + std::to_string(m_score), {16.0f, 14.0f},
                            Color(0.95f, 0.95f, 1.0f), 30.0f);
        DrawCentered("NARAGAME  BREAKOUT", 290.0f, 64.0f, Color(0.35f, 0.45f, 0.7f));
        DrawCentered("ARROW KEYS  -  MOVE      ESC  -  QUIT", 370.0f, 24.0f,
                     Color(0.5f, 0.5f, 0.6f));
        m_renderer.End();
    }

private:
    static AppConfig MakeConfig() {
        AppConfig config;
        config.title = "NaraGame - Breakout";
        config.width = 800;
        config.height = 600;
        return config;
    }

    void BuildWorld() {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        m_world.Clear();
        m_world.SetGravity({0.0f, 0.0f});
        m_bricks.clear();

        // Walls (static): left, right, top — thick rectangles just off-screen.
        Body wall;
        wall.type = BodyType::Static;
        wall.tag = TagWall;
        wall.position = {-40.0f, -200.0f}; wall.size = {40.0f, h + 400.0f}; m_world.AddBody(wall);
        wall.position = {w, -200.0f};       wall.size = {40.0f, h + 400.0f}; m_world.AddBody(wall);
        wall.position = {-200.0f, -40.0f};  wall.size = {w + 400.0f, 40.0f}; m_world.AddBody(wall);

        // Paddle (kinematic).
        Body paddle;
        paddle.type = BodyType::Kinematic;
        paddle.tag = TagPaddle;
        paddle.size = {kPaddleW, kPaddleH};
        paddle.position = {(w - kPaddleW) * 0.5f, h - 48.0f};
        m_paddleId = m_world.AddBody(paddle);

        // Ball (dynamic, perfect bounce, no gravity).
        Body ball;
        ball.type = BodyType::Dynamic;
        ball.tag = TagBall;
        ball.size = {kBallSize, kBallSize};
        ball.position = {(w - kBallSize) * 0.5f, h - 90.0f};
        ball.velocity = {220.0f, -320.0f};
        ball.restitution = 1.0f;
        ball.gravityScale = 0.0f;
        m_ballId = m_world.AddBody(ball);

        // Bricks (static), 6 rows x 10 cols.
        const int cols = 10;
        const int rows = 6;
        const float margin = 40.0f;
        const float gap = 6.0f;
        const float top = 60.0f;
        const float brickW = (w - 2.0f * margin - (cols - 1) * gap) / cols;
        const float brickH = 22.0f;
        const Color rowColors[] = {
            Color(0.91f, 0.30f, 0.24f), Color(0.95f, 0.61f, 0.07f),
            Color(0.95f, 0.85f, 0.10f), Color(0.18f, 0.80f, 0.44f),
            Color(0.20f, 0.60f, 0.86f), Color(0.61f, 0.35f, 0.71f),
        };
        for (int row = 0; row < rows; ++row) {
            for (int col = 0; col < cols; ++col) {
                Brick brick;
                brick.position = {margin + col * (brickW + gap), top + row * (brickH + gap)};
                brick.size = {brickW, brickH};
                brick.color = rowColors[row % 6];
                brick.sprite = row % 6;

                Body body;
                body.type = BodyType::Static;
                body.tag = TagBrick;
                body.position = brick.position;
                body.size = brick.size;
                brick.bodyId = m_world.AddBody(body);
                m_bricks.push_back(brick);
            }
        }
    }

    void ResetBall() {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        if (Body* ball = m_world.Get(m_ballId)) {
            ball->position = {(w - kBallSize) * 0.5f, h - 90.0f};
            ball->velocity = {220.0f, -320.0f};
        }
        if (Body* paddle = m_world.Get(m_paddleId))
            paddle->position.x = (w - kPaddleW) * 0.5f;
    }

    void HandleContacts() {
        for (const Contact& c : m_world.Contacts()) {
            const int other = (c.a == m_ballId) ? c.b : (c.b == m_ballId ? c.a : -1);
            if (other < 0)
                continue;
            Body* ob = m_world.Get(other);
            if (ob == nullptr)
                continue;

            if (ob->tag == TagBrick) {
                for (Brick& brick : m_bricks) {
                    if (brick.alive && brick.bodyId == other) {
                        brick.alive = false;
                        m_world.RemoveBody(other);
                        m_score += 10;
                        EmitBrickBurst(brick);
                        break;
                    }
                }
            } else if (ob->tag == TagPaddle) {
                // Deflect based on where the ball struck the paddle.
                if (Body* ball = m_world.Get(m_ballId)) {
                    const float ballCenter = ball->position.x + kBallSize * 0.5f;
                    const float paddleCenter = ob->position.x + kPaddleW * 0.5f;
                    const float offset = (ballCenter - paddleCenter) / (kPaddleW * 0.5f);
                    ball->velocity.x = Clamp(offset, -1.0f, 1.0f) * 320.0f;
                }
            }
        }
    }

    void EmitBrickBurst(const Brick& brick) {
        ParticleBurst burst;
        burst.position = {brick.position.x + brick.size.x * 0.5f,
                          brick.position.y + brick.size.y * 0.5f};
        burst.count = 16;
        burst.minSpeed = 60.0f;
        burst.maxSpeed = 240.0f;
        burst.minLife = 0.3f;
        burst.maxLife = 0.7f;
        burst.startSize = 7.0f;
        burst.startColor = brick.color;
        burst.endColor = Color(brick.color.r, brick.color.g, brick.color.b, 0.0f);
        burst.gravity = {0.0f, 700.0f}; // debris falls
        m_particles.Emit(burst);
    }

    bool AllBricksCleared() const {
        for (const Brick& brick : m_bricks)
            if (brick.alive)
                return false;
        return true;
    }

    void DrawCentered(const std::string& text, float y, float pixelSize, const Color& color) {
        const float w = static_cast<float>(Config().width);
        const float textWidth = m_font.Measure(text, pixelSize).x;
        m_renderer.DrawText(m_font, text, {(w - textWidth) * 0.5f, y}, color, pixelSize);
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_brickSheetTexture;
    std::unique_ptr<RHITexture> m_spritesTexture;
    SpriteSheet m_brickSheet;
    TextureAtlas m_atlas;
    MSDFFont m_font;

    PhysicsWorld m_world;
    ParticleSystem m_particles;
    int m_ballId = -1;
    int m_paddleId = -1;
    std::vector<Brick> m_bricks;
    int m_score = 0;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new BreakoutApp();
}
