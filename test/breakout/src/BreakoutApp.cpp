// Breakout (벽돌깨기) — first playable test game on the NaraGame engine.
//
// Demonstrates the engine end to end: Win32 window + D3D11 + Renderer2D batched
// quads + keyboard input. Controls: Left/Right arrows move the paddle, ESC quits.

#include <NaraGame/assets/TextureLoader.h>
#include <NaraGame/core/Application.h>
#include <NaraGame/core/EntryPoint.h>
#include <NaraGame/core/Log.h>
#include <NaraGame/input/Input.h>
#include <NaraGame/math/Math.h>
#include <NaraGame/renderer/BitmapFont.h>
#include <NaraGame/renderer/Renderer2D.h>
#include <NaraGame/renderer/SpriteSheet.h>
#include <NaraGame/renderer/TextureAtlas.h>
#include <NaraGame/renderer/rhi/RHIDevice.h>

#include <memory>
#include <string>
#include <vector>

#ifndef NARAGAME_ASSETS_DIR
#define NARAGAME_ASSETS_DIR "assets"
#endif

using namespace NaraGame;

namespace {

struct Brick {
    Vec2 position;
    Vec2 size;
    Color color;
    int sprite = 0; // which sprite-sheet cell to draw
    bool alive = true;
};

// Axis-aligned overlap test for two rectangles given by top-left + size.
bool Overlaps(const Vec2& aPos, const Vec2& aSize, const Vec2& bPos, const Vec2& bSize) {
    return aPos.x < bPos.x + bSize.x && aPos.x + aSize.x > bPos.x &&
           aPos.y < bPos.y + bSize.y && aPos.y + aSize.y > bPos.y;
}

} // namespace

class BreakoutApp : public Application {
public:
    BreakoutApp() : Application(MakeConfig()) {}

protected:
    void OnStart() override {
        m_renderer.Init(GetDevice());

        const std::string assets = NARAGAME_ASSETS_DIR;

        // Sprite sheet: a 3x2 grid of 64x32 brick styles in one texture.
        m_brickSheetTexture = LoadTexture(GetDevice(), assets + "/bricks.png");
        m_brickSheet = SpriteSheet(m_brickSheetTexture.get(), 64, 32);

        // Packed atlas: ball + paddle share one texture, addressed by name.
        m_spritesTexture = LoadTexture(GetDevice(), assets + "/sprites.png");
        m_atlas = TextureAtlas(m_spritesTexture.get());
        m_atlas.Add("ball", 0.0f, 0.0f, 32.0f, 32.0f);
        m_atlas.Add("paddle", 0.0f, 32.0f, 120.0f, 20.0f);

        // Bitmap font: 16-column grid starting at space, 14x24 glyph cells.
        m_fontTexture = LoadTexture(GetDevice(), assets + "/font.png");
        m_font = BitmapFont(m_fontTexture.get(), ' ', 16, 14, 24);
        m_font.SetTracking(-2);

        ResetBricks();
        ResetBall();
        NARAGAME_INFO("Breakout: Left/Right to move, ESC to quit");
    }

    void OnStop() override {
        m_brickSheetTexture.reset();
        m_spritesTexture.reset();
        m_fontTexture.reset();
        m_renderer.Shutdown();
    }

    void OnUpdate(float dt) override {
        if (Input::IsKeyDown(Key::Escape))
            Close();

        const float w = static_cast<float>(Config().width);

        // Paddle.
        const float paddleSpeed = 700.0f;
        if (Input::IsKeyDown(Key::Left))
            m_paddlePos.x -= paddleSpeed * dt;
        if (Input::IsKeyDown(Key::Right))
            m_paddlePos.x += paddleSpeed * dt;
        m_paddlePos.x = Clamp(m_paddlePos.x, 0.0f, w - m_paddleSize.x);

        // Ball integration.
        m_ballPos += m_ballVel * dt;

        BounceOffWalls();
        BounceOffPaddle();
        BounceOffBricks();

        if (AllBricksCleared())
            ResetBricks();
    }

    void OnRender() override {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);

        GetDevice().Clear(Color(0.05f, 0.06f, 0.10f, 1.0f));

        m_renderer.Begin(Renderer2D::ScreenCamera(w, h));
        // Bricks: each row draws a different sheet cell, tinted per row. All share
        // one texture, so the whole wall is still a single batched draw call.
        for (const Brick& brick : m_bricks) {
            if (brick.alive)
                m_renderer.DrawQuad(brick.position, brick.size, m_brickSheet.At(brick.sprite),
                                    brick.color);
        }
        // Paddle and ball come from the packed atlas, addressed by name.
        m_renderer.DrawQuad(m_paddlePos, m_paddleSize, m_atlas.Get("paddle"),
                            Color(0.9f, 0.9f, 0.95f, 1.0f));
        m_renderer.DrawQuad(m_ballPos, m_ballSize, m_atlas.Get("ball"),
                            Color(1.0f, 0.85f, 0.3f, 1.0f));

        // --- Text (bitmap font) ---
        m_renderer.DrawText(m_font, "SCORE " + std::to_string(m_score), {16.0f, 18.0f},
                            Color(0.95f, 0.95f, 1.0f), 1.5f);

        DrawCentered("NARA  BREAKOUT", 300.0f, 3.0f, Color(0.35f, 0.45f, 0.7f));
        DrawCentered("ARROW KEYS  -  MOVE      ESC  -  QUIT", 360.0f, 1.3f,
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

    void ResetBricks() {
        m_bricks.clear();
        const float w = static_cast<float>(Config().width);
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
                brick.sprite = row % 6; // a different sheet cell per row
                m_bricks.push_back(brick);
            }
        }
    }

    void ResetBall() {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        m_paddleSize = {110.0f, 18.0f};
        m_paddlePos = {(w - m_paddleSize.x) * 0.5f, h - 48.0f};
        m_ballSize = {14.0f, 14.0f};
        m_ballPos = {(w - m_ballSize.x) * 0.5f, h - 90.0f};
        m_ballVel = {220.0f, -320.0f};
    }

    void BounceOffWalls() {
        const float w = static_cast<float>(Config().width);
        const float h = static_cast<float>(Config().height);
        if (m_ballPos.x < 0.0f) {
            m_ballPos.x = 0.0f;
            m_ballVel.x = -m_ballVel.x;
        }
        if (m_ballPos.x + m_ballSize.x > w) {
            m_ballPos.x = w - m_ballSize.x;
            m_ballVel.x = -m_ballVel.x;
        }
        if (m_ballPos.y < 0.0f) {
            m_ballPos.y = 0.0f;
            m_ballVel.y = -m_ballVel.y;
        }
        if (m_ballPos.y > h) // fell past the paddle: lose the ball
            ResetBall();
    }

    void BounceOffPaddle() {
        if (m_ballVel.y <= 0.0f || !Overlaps(m_ballPos, m_ballSize, m_paddlePos, m_paddleSize))
            return;
        m_ballPos.y = m_paddlePos.y - m_ballSize.y;
        m_ballVel.y = -m_ballVel.y;
        // Deflect based on where the ball struck the paddle.
        const float ballCenter = m_ballPos.x + m_ballSize.x * 0.5f;
        const float paddleCenter = m_paddlePos.x + m_paddleSize.x * 0.5f;
        const float offset = (ballCenter - paddleCenter) / (m_paddleSize.x * 0.5f);
        m_ballVel.x = Clamp(offset, -1.0f, 1.0f) * 320.0f;
    }

    void BounceOffBricks() {
        for (Brick& brick : m_bricks) {
            if (!brick.alive)
                continue;
            if (Overlaps(m_ballPos, m_ballSize, brick.position, brick.size)) {
                brick.alive = false;
                m_score += 10;
                m_ballVel.y = -m_ballVel.y;
                break; // one brick per frame keeps the response stable
            }
        }
    }

    bool AllBricksCleared() const {
        for (const Brick& brick : m_bricks)
            if (brick.alive)
                return false;
        return true;
    }

    void DrawCentered(const std::string& text, float y, float scale, const Color& color) {
        const float w = static_cast<float>(Config().width);
        const float textWidth = m_font.Measure(text, scale).x;
        m_renderer.DrawText(m_font, text, {(w - textWidth) * 0.5f, y}, color, scale);
    }

    Renderer2D m_renderer;
    std::unique_ptr<RHITexture> m_brickSheetTexture;
    std::unique_ptr<RHITexture> m_spritesTexture;
    std::unique_ptr<RHITexture> m_fontTexture;
    SpriteSheet m_brickSheet;
    TextureAtlas m_atlas;
    BitmapFont m_font;
    int m_score = 0;
    std::vector<Brick> m_bricks;
    Vec2 m_paddlePos;
    Vec2 m_paddleSize;
    Vec2 m_ballPos;
    Vec2 m_ballSize;
    Vec2 m_ballVel;
};

Application* NaraGame::CreateApplication(int /*argc*/, char** /*argv*/) {
    return new BreakoutApp();
}
