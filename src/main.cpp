// Underwatch - very minimal 3v3 hero shooter prototype.
// Human controls one hero; the other 5 slots are filled by simple bots.
// All graphics are drawn as vector shapes (lines/circles/rects), no sprites or text.

#include <SDL3/SDL.h>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <array>
#include <algorithm>
#include <ctime>
#include <string>

// ---------------------------------------------------------------------------
// Constants / world setup
// ---------------------------------------------------------------------------

static const int WINDOW_W = 1600;
static const int WINDOW_H = 900;

static const float ARENA_X0 = 40, ARENA_Y0 = 70;
static const float ARENA_X1 = 1560, ARENA_Y1 = 800;

static const float POINT_CX = (ARENA_X0 + ARENA_X1) * 0.5f;
static const float POINT_CY = (ARENA_Y0 + ARENA_Y1) * 0.5f;
static const float POINT_RADIUS = 100.0f;

static const float SPAWN_ZONE_DEPTH = 220.0f; // strip at each side only that team may enter

static const float CAPTURE_TIME_SECONDS = 25.0f;
static const float CAPTURE_RATE_PER_SEC = 100.0f / CAPTURE_TIME_SECONDS;

enum class Team { A = 0, B = 1 };
enum class Role { Tank = 0, Damage = 1, Healer = 2 };

static constexpr float PI = 3.14159265358979323846f; // M_PI isn't defined by MSVC without _USE_MATH_DEFINES

struct Vec2 { float x = 0, y = 0; };

static float vlen(Vec2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }
static Vec2 vnorm(Vec2 v) { float l = vlen(v); if (l < 0.0001f) return {0,0}; return {v.x/l, v.y/l}; }
static float dist(Vec2 a, Vec2 b) { return vlen({a.x-b.x, a.y-b.y}); }

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

struct Projectile {
    Vec2 pos, vel;
    Team team;
    Role sourceRole;
    int sourceId = -1;
    float damage = 0;
    float heal = 0;
    bool pierce = false;
    float radius = 5;
    std::vector<int> alreadyHit; // player ids already hit (for pierce)
    bool alive = true;
};

// Short-lived visual pop drawn wherever a projectile connects with a player.
struct HitMarker {
    Vec2 pos;
    float timer;
};
static const float HIT_MARKER_LIFETIME = 0.18f;

struct Player {
    int id = 0;
    Team team;
    Role role;
    bool isBot = true;

    Vec2 pos, spawnPos;
    float angle = 0; // aim direction, radians

    float maxHealth = 100, health = 100;
    float shield = 0, maxShield = 0;
    float shieldTimer = 0;      // time remaining shield stays up
    float shieldCooldown = 0;

    float moveSpeed = 180;

    float shootCooldown = 0;

    int ammo = -1, maxAmmo = -1; // -1 = unlimited (Tank)
    bool reloading = false;
    float reloadTimer = 0;

    float abilityCooldown = 0;  // secondary ability cooldown
    float sprintTimer = 0;      // Damage: speed boost remaining

    float ultCharge = 0, ultMax = 100;
    bool ultActive = false;
    float ultTimer = 0;

    bool alive = true;
    float respawnTimer = 0;

    // Tank ultimate barrier
    float barrierHP = 0;
    Vec2 barrierPos;

    // Healer ultimate auto-pulse timer
    float autoPulseTimer = 0;

    // Bot movement AI state
    float decisionTimer = 0;
    Vec2 targetPos;
    float strafePhase = 0;
};

// ---------------------------------------------------------------------------
// Colors
// ---------------------------------------------------------------------------

struct Col { Uint8 r,g,b,a; };
static Col teamColor(Team t) {
    return (t == Team::A) ? Col{70, 150, 255, 255} : Col{255, 70, 70, 255};
}

// ---------------------------------------------------------------------------
// Drawing helpers (vector graphics only)
// ---------------------------------------------------------------------------

static void setColor(SDL_Renderer* r, Col c) { SDL_SetRenderDrawColor(r, c.r, c.g, c.b, c.a); }

static void drawFilledCircle(SDL_Renderer* r, float cx, float cy, float radius) {
    int steps = (int)radius;
    if (steps < 4) steps = 4;
    for (int i = -steps; i <= steps; i++) {
        float dy = (float)i;
        if (std::fabs(dy) > radius) continue;
        float dx = std::sqrt(radius*radius - dy*dy);
        SDL_RenderLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

static void drawCircleOutline(SDL_Renderer* r, float cx, float cy, float radius, int segments = 32) {
    std::vector<SDL_FPoint> pts;
    pts.reserve(segments + 1);
    for (int i = 0; i <= segments; i++) {
        float a = (float)i / segments * 2.0f * PI;
        pts.push_back({cx + std::cos(a) * radius, cy + std::sin(a) * radius});
    }
    SDL_RenderLines(r, pts.data(), (int)pts.size());
}

// Fills an arbitrary convex polygon by fanning triangles out from its center point.
static void drawFilledPolygon(SDL_Renderer* r, Vec2 center, const std::vector<Vec2>& pts, Col color) {
    int n = (int)pts.size();
    if (n < 3) return;
    SDL_FColor fc{ color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f };
    std::vector<SDL_Vertex> verts;
    verts.reserve(n * 3);
    SDL_Vertex cv{ { center.x, center.y }, fc, {0,0} };
    for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        SDL_Vertex v1{ { pts[i].x, pts[i].y }, fc, {0,0} };
        SDL_Vertex v2{ { pts[j].x, pts[j].y }, fc, {0,0} };
        verts.push_back(cv);
        verts.push_back(v1);
        verts.push_back(v2);
    }
    SDL_RenderGeometry(r, nullptr, verts.data(), (int)verts.size(), nullptr, 0);
}

static void drawPolygonOutline(SDL_Renderer* r, const std::vector<Vec2>& pts) {
    if (pts.size() < 2) return;
    std::vector<SDL_FPoint> fpts;
    fpts.reserve(pts.size() + 1);
    for (auto& p : pts) fpts.push_back({p.x, p.y});
    fpts.push_back({pts[0].x, pts[0].y});
    SDL_RenderLines(r, fpts.data(), (int)fpts.size());
}

static std::vector<Vec2> hexagonPoints(Vec2 center, float r) {
    std::vector<Vec2> pts;
    for (int i = 0; i < 6; i++) {
        float a = i * PI / 3.0f;
        pts.push_back({ center.x + r * std::cos(a), center.y + r * std::sin(a) });
    }
    return pts;
}

// Points in the aim direction, like a ship's nose - doubles as a facing indicator.
static std::vector<Vec2> trianglePoints(Vec2 center, float r, float angle) {
    Vec2 local[3] = { {r, 0.0f}, {-r * 0.6f, r * 0.8f}, {-r * 0.6f, -r * 0.8f} };
    float ca = std::cos(angle), sa = std::sin(angle);
    std::vector<Vec2> pts;
    for (auto& lp : local) {
        pts.push_back({ center.x + lp.x * ca - lp.y * sa, center.y + lp.x * sa + lp.y * ca });
    }
    return pts;
}

static std::vector<Vec2> crossPoints(Vec2 center, float r) {
    float a = r, t = r * 0.4f;
    Vec2 local[12] = {
        {-t,-a}, {t,-a}, {t,-t}, {a,-t}, {a,t}, {t,t},
        {t,a}, {-t,a}, {-t,t}, {-a,t}, {-a,-t}, {-t,-t}
    };
    std::vector<Vec2> pts;
    for (auto& lp : local) pts.push_back({ center.x + lp.x, center.y + lp.y });
    return pts;
}

static std::vector<Vec2> playerShapePoints(const Player& p, float radius) {
    switch (p.role) {
        case Role::Tank:   return hexagonPoints(p.pos, radius);
        case Role::Damage: return trianglePoints(p.pos, radius, p.angle);
        case Role::Healer: return crossPoints(p.pos, radius);
    }
    return {};
}

// Slim pointed cartridge, nose first along the direction of travel.
static std::vector<Vec2> bulletPoints(Vec2 center, float r, float angle) {
    Vec2 local[5] = {
        { 1.5f * r, 0.0f }, { 0.5f * r, 0.55f * r }, { -0.9f * r, 0.55f * r },
        { -0.9f * r, -0.55f * r }, { 0.5f * r, -0.55f * r }
    };
    float ca = std::cos(angle), sa = std::sin(angle);
    std::vector<Vec2> pts;
    for (auto& lp : local) {
        pts.push_back({ center.x + lp.x * ca - lp.y * sa, center.y + lp.x * sa + lp.y * ca });
    }
    return pts;
}

// Small flask silhouette (narrow neck, round body) for the healer's shot.
static std::vector<Vec2> potionPoints(Vec2 center, float r, float angle) {
    Vec2 local[7] = {
        { 1.2f * r, 0.0f }, { 0.7f * r, 0.22f * r }, { 0.7f * r, 0.85f * r },
        { -0.9f * r, 0.65f * r }, { -0.9f * r, -0.65f * r },
        { 0.7f * r, -0.85f * r }, { 0.7f * r, -0.22f * r }
    };
    float ca = std::cos(angle), sa = std::sin(angle);
    std::vector<Vec2> pts;
    for (auto& lp : local) {
        pts.push_back({ center.x + lp.x * ca - lp.y * sa, center.y + lp.x * sa + lp.y * ca });
    }
    return pts;
}

static void drawBar(SDL_Renderer* r, float x, float y, float w, float h, float frac, Col fg, Col bg) {
    if (frac < 0) frac = 0;
    if (frac > 1) frac = 1;
    SDL_FRect back{ x, y, w, h };
    setColor(r, bg);
    SDL_RenderFillRect(r, &back);
    SDL_FRect front{ x, y, w * frac, h };
    setColor(r, fg);
    SDL_RenderFillRect(r, &front);
    setColor(r, {10,10,10,255});
    SDL_RenderRect(r, &back);
}

static void drawWallFrame(SDL_Renderer* r, float x0, float y0, float x1, float y1, float thickness, Col c) {
    setColor(r, c);
    SDL_FRect top{ x0 - thickness, y0 - thickness, (x1 - x0) + thickness * 2, thickness };
    SDL_FRect bottom{ x0 - thickness, y1, (x1 - x0) + thickness * 2, thickness };
    SDL_FRect left{ x0 - thickness, y0 - thickness, thickness, (y1 - y0) + thickness * 2 };
    SDL_FRect right{ x1, y0 - thickness, thickness, (y1 - y0) + thickness * 2 };
    SDL_RenderFillRect(r, &top);
    SDL_RenderFillRect(r, &bottom);
    SDL_RenderFillRect(r, &left);
    SDL_RenderFillRect(r, &right);
}

// ---------------------------------------------------------------------------
// Minimal vector font (5x7 block glyphs) - keeps everything drawn, no font files
// ---------------------------------------------------------------------------

static const char* const* glyphRows(char c) {
    static const char* G_0[7] = {".###.","#...#","#..##","#.#.#","##..#","#...#",".###."};
    static const char* G_1[7] = {"..#..",".##..","..#..","..#..","..#..","..#..",".###."};
    static const char* G_2[7] = {".###.","#...#","....#","...#.","..#..",".#...","#####"};
    static const char* G_3[7] = {".###.","#...#","....#","..##.","....#","#...#",".###."};
    static const char* G_4[7] = {"...#.","..##.",".#.#.","#..#.","#####","...#.","...#."};
    static const char* G_5[7] = {"#####","#....","#....","####.","....#","#...#",".###."};
    static const char* G_6[7] = {"..##.",".#...","#....","####.","#...#","#...#",".###."};
    static const char* G_7[7] = {"#####","....#","...#.","..#..",".#...",".#...",".#..."};
    static const char* G_8[7] = {".###.","#...#","#...#",".###.","#...#","#...#",".###."};
    static const char* G_9[7] = {".###.","#...#","#...#",".####","....#","...#.",".##.."};
    static const char* G_A[7] = {"..#..",".#.#.","#...#","#...#","#####","#...#","#...#"};
    static const char* G_B[7] = {"####.","#...#","#...#","####.","#...#","#...#","####."};
    static const char* G_D[7] = {"####.","#...#","#...#","#...#","#...#","#...#","####."};
    static const char* G_E[7] = {"#####","#....","#....","####.","#....","#....","#####"};
    static const char* G_H[7] = {"#...#","#...#","#...#","#####","#...#","#...#","#...#"};
    static const char* G_L[7] = {"#....","#....","#....","#....","#....","#....","#####"};
    static const char* G_P[7] = {"####.","#...#","#...#","####.","#....","#....","#...."};
    static const char* G_R[7] = {"####.","#...#","#...#","####.","..#..","...#.","....#"};
    static const char* G_T[7] = {"#####","..#..","..#..","..#..","..#..","..#..","..#.."};
    static const char* G_U[7] = {"#...#","#...#","#...#","#...#","#...#","#...#",".###."};
    static const char* G_C[7] = {".###.","#...#","#....","#....","#....","#...#",".###."};
    static const char* G_N[7] = {"#...#","##..#","#.#.#","#.#.#","#..##","#...#","#...#"};
    static const char* G_O[7] = {".###.","#...#","#...#","#...#","#...#","#...#",".###."};
    static const char* G_S[7] = {".####","#....","#....",".###.","....#","....#","####."};
    static const char* G_V[7] = {"#...#","#...#","#...#","#...#","#...#",".#.#.","..#.."};
    static const char* G_I[7] = {"#####","..#..","..#..","..#..","..#..","..#..","#####"};
    static const char* G_M[7] = {"#...#","##.##","#.#.#","#...#","#...#","#...#","#...#"};
    static const char* G_Y[7] = {"#...#","#...#",".#.#.","..#..","..#..","..#..","..#.."};
    static const char* G_W[7] = {"#...#","#...#","#...#","#.#.#","#.#.#","##.##","#...#"};
    static const char* G_K[7] = {"#...#","#..#.","#.#..","##...","#.#..","#..#.","#...#"};
    static const char* G_F[7] = {"#####","#....","#....","####.","#....","#....","#...."};
    static const char* G_Qc[7] = {".###.","#...#","#...#","#...#","#.#.#","#..#.",".##.#"};

    switch (c) {
        case '0': return G_0; case '1': return G_1; case '2': return G_2;
        case '3': return G_3; case '4': return G_4; case '5': return G_5;
        case '6': return G_6; case '7': return G_7; case '8': return G_8;
        case '9': return G_9;
        case 'A': return G_A; case 'B': return G_B; case 'C': return G_C;
        case 'D': return G_D; case 'E': return G_E; case 'F': return G_F;
        case 'H': return G_H; case 'I': return G_I; case 'K': return G_K;
        case 'L': return G_L; case 'M': return G_M; case 'N': return G_N;
        case 'O': return G_O; case 'P': return G_P; case 'Q': return G_Qc;
        case 'R': return G_R; case 'S': return G_S; case 'T': return G_T;
        case 'U': return G_U; case 'V': return G_V; case 'W': return G_W;
        case 'Y': return G_Y;
        default: return nullptr;
    }
}

static void drawChar(SDL_Renderer* r, float x, float y, char c, float px, Col color) {
    const char* const* rows = glyphRows(c);
    if (!rows) return;
    setColor(r, color);
    for (int row = 0; row < 7; row++) {
        for (int col = 0; col < 5; col++) {
            if (rows[row][col] == '#') {
                SDL_FRect cell{ x + col * px, y + row * px, px, px };
                SDL_RenderFillRect(r, &cell);
            }
        }
    }
}

static float textWidth(const std::string& text, float px) {
    if (text.empty()) return 0.0f;
    return text.size() * 6.0f * px - px;
}

static void drawText(SDL_Renderer* r, float x, float y, const std::string& text, float px, Col color) {
    float cx = x;
    for (char c : text) {
        drawChar(r, cx, y, c, px, color);
        cx += 6.0f * px;
    }
}

static void drawTextCentered(SDL_Renderer* r, float cx, float y, const std::string& text, float px, Col color) {
    drawText(r, cx - textWidth(text, px) / 2.0f, y, text, px, color);
}

// ---------------------------------------------------------------------------
// Game state
// ---------------------------------------------------------------------------

struct Game {
    std::vector<Player> players;
    std::vector<Projectile> projectiles;
    std::vector<HitMarker> hitMarkers;

    float progress = 0;       // 0..100
    int controllingTeam = -1; // -1 none, 0 = A, 1 = B

    // 5-second "uncontested" requirement to gain or flip control of the point.
    float flipTimer = 0;
    int flipCandidateTeam = -1;
    bool pointContested = false;
    bool captureSoundPlayed = false; // so the capture chime only plays once per capture

    bool matchOver = false;
    int winningTeam = -1;
    float matchOverTimer = 0;

    float preRoundTimer = 10.0f;
    bool hasStartedOnce = false; // first round gets a longer countdown than later ones
    int lastCountdownTick = -1;

    float ammoPopupTimer = 0; // "out of ammo" popup for the human player

    int humanIndex = 0;
};

static void setupHeroStats(Player& p) {
    switch (p.role) {
        case Role::Tank:
            p.maxHealth = 250; p.health = p.maxHealth;
            p.moveSpeed = 90;
            p.maxShield = 500;
            break;
        case Role::Damage:
            p.maxHealth = 150; p.health = p.maxHealth;
            p.moveSpeed = 125;
            p.maxAmmo = 20;
            break;
        case Role::Healer:
            p.maxHealth = 130; p.health = p.maxHealth;
            p.moveSpeed = 110;
            p.maxAmmo = 12;
            break;
    }
}

static void resetPlayer(Player& p) {
    p.pos = p.spawnPos;
    p.health = p.maxHealth;
    p.shield = 0; p.shieldTimer = 0; p.shieldCooldown = 0;
    p.shootCooldown = 0;
    p.ammo = p.maxAmmo;
    p.reloading = false;
    p.reloadTimer = 0;
    p.abilityCooldown = 0;
    p.sprintTimer = 0;
    p.ultCharge = 0; p.ultActive = false; p.ultTimer = 0;
    p.alive = true; p.respawnTimer = 0;
    p.barrierHP = 0;
    p.autoPulseTimer = 0;
}

static void initGame(Game& g) {
    g.players.clear();
    g.projectiles.clear();
    g.hitMarkers.clear();
    g.progress = 0;
    g.controllingTeam = -1;
    g.flipTimer = 0;
    g.flipCandidateTeam = -1;
    g.pointContested = false;
    g.captureSoundPlayed = false;
    g.ammoPopupTimer = 0;
    g.matchOver = false;
    g.winningTeam = -1;
    g.matchOverTimer = 0;
    g.preRoundTimer = g.hasStartedOnce ? 5.0f : 10.0f;
    g.lastCountdownTick = -1;
    g.hasStartedOnce = true;

    struct Setup { Team t; Role r; bool bot; };
    std::array<Setup, 6> setups = {{
        { Team::A, Role::Damage, false }, // human
        { Team::A, Role::Tank,   true  },
        { Team::A, Role::Healer, true  },
        { Team::B, Role::Tank,   true  },
        { Team::B, Role::Damage, true  },
        { Team::B, Role::Healer, true  },
    }};

    int teamACount = 0, teamBCount = 0;
    for (int i = 0; i < (int)setups.size(); i++) {
        Player p;
        p.id = i;
        p.team = setups[i].t;
        p.role = setups[i].r;
        p.isBot = setups[i].bot;
        setupHeroStats(p);

        float baseX = (p.team == Team::A) ? ARENA_X0 + 90 : ARENA_X1 - 90;
        int slot = (p.team == Team::A) ? teamACount++ : teamBCount++;
        float baseY = ARENA_Y0 + 100 + slot * 130;
        p.spawnPos = { baseX, baseY };
        p.angle = (p.team == Team::A) ? 0.0f : PI;

        resetPlayer(p);
        g.players.push_back(p);
    }
    g.humanIndex = 0;
}

// ---------------------------------------------------------------------------
// Audio: a tiny procedural tone mixer - no sound files, everything here is
// synthesized, the same way the graphics are all drawn rather than loaded.
// ---------------------------------------------------------------------------

static float frand(float lo, float hi);

static const int AUDIO_SAMPLE_RATE = 44100;

struct Tone {
    float freq;
    float freqSlidePerSample = 0.0f;
    float phase = 0.0f;
    int delaySamples = 0;
    int samplesLeft = 0;
    int totalSamples = 0;
    float volume = 0.5f;
    int waveform = 0; // 0 = sine, 1 = square
};

static SDL_AudioStream* g_audioStream = nullptr;
static std::vector<Tone> g_activeTones;

// Runs on the audio thread; mixes every active tone into the requested buffer.
static void audioCallback(void* userdata, SDL_AudioStream* stream, int additional_amount, int total_amount) {
    (void)userdata; (void)total_amount;
    if (additional_amount <= 0) return;
    int numSamples = additional_amount / (int)sizeof(float);
    std::vector<float> buf(numSamples, 0.0f);

    for (auto& t : g_activeTones) {
        for (int i = 0; i < numSamples; i++) {
            if (t.delaySamples > 0) { t.delaySamples--; continue; }
            if (t.samplesLeft <= 0) break;

            int played = t.totalSamples - t.samplesLeft;
            int fadeSamples = std::min(300, t.totalSamples / 4 + 1);
            float envelope = 1.0f;
            if (played < fadeSamples) envelope = (float)played / fadeSamples;
            else if (t.samplesLeft < fadeSamples) envelope = (float)t.samplesLeft / fadeSamples;

            float raw = (t.waveform == 1) ? (std::sin(t.phase) >= 0.0f ? 1.0f : -1.0f) : std::sin(t.phase);
            buf[i] += raw * t.volume * envelope;

            t.phase += 2.0f * PI * t.freq / AUDIO_SAMPLE_RATE;
            if (t.phase > 2.0f * PI) t.phase -= 2.0f * PI;
            t.freq += t.freqSlidePerSample;
            if (t.freq < 20.0f) t.freq = 20.0f;
            t.samplesLeft--;
        }
    }

    g_activeTones.erase(std::remove_if(g_activeTones.begin(), g_activeTones.end(),
        [](const Tone& t) { return t.samplesLeft <= 0; }), g_activeTones.end());

    // Several bots can fire in the same frame; scale down as tones pile up so
    // combat doesn't collapse into one constantly-clipped wall of sound.
    size_t voices = std::max<size_t>(1, g_activeTones.size());
    float mixScale = 1.0f / std::sqrt((float)voices);
    for (auto& s : buf) {
        s *= mixScale;
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
    }
    SDL_PutAudioStreamData(stream, buf.data(), (int)(buf.size() * sizeof(float)));
}

static void playTone(float freq, float durationSec, float volume, int waveform,
                      float freqSlidePerSec = 0.0f, float delaySec = 0.0f) {
    if (!g_audioStream) return;
    Tone t;
    t.freq = freq;
    t.freqSlidePerSample = freqSlidePerSec / AUDIO_SAMPLE_RATE;
    t.volume = volume;
    t.waveform = waveform;
    t.totalSamples = std::max(1, (int)(durationSec * AUDIO_SAMPLE_RATE));
    t.samplesLeft = t.totalSamples;
    t.delaySamples = (int)(delaySec * AUDIO_SAMPLE_RATE);
    SDL_LockAudioStream(g_audioStream);
    g_activeTones.push_back(t);
    SDL_UnlockAudioStream(g_audioStream);
}

// Shot/hit sounds fire constantly once several bots are in combat, so they're
// kept quiet and given a little pitch/volume jitter so they don't sound like
// the exact same sample looping.
static void sfxShotTank()      { playTone(frand(140.0f, 160.0f), 0.14f, frand(0.22f, 0.28f), 1); }
static void sfxShotDamage()    { playTone(frand(920.0f, 1080.0f), 0.05f, frand(0.14f, 0.19f), 0, -3000.0f); }
static void sfxShotHealer()    { playTone(frand(560.0f, 640.0f), 0.12f, frand(0.16f, 0.21f), 0); }
static void sfxHitMarker()     { playTone(frand(1400.0f, 1600.0f), 0.04f, frand(0.22f, 0.28f), 1); }
static void sfxContested()     { playTone(300.0f, 0.12f, 0.28f, 1); playTone(460.0f, 0.12f, 0.28f, 1, 0.0f, 0.10f); }
static void sfxControlGained() { playTone(300.0f, 0.25f, 0.35f, 0, 1600.0f); }
static void sfxDeath()         { playTone(500.0f, 0.35f, 0.4f, 1, -1100.0f); }
static void sfxCapture()       { playTone(500.0f, 0.2f, 0.4f, 0, 2000.0f); }
static void sfxCountdownTick() { playTone(700.0f, 0.08f, 0.35f, 1); }
static void sfxWin() {
    playTone(523.0f, 0.15f, 0.4f, 0);
    playTone(659.0f, 0.15f, 0.4f, 0, 0.0f, 0.15f);
    playTone(784.0f, 0.25f, 0.45f, 0, 0.0f, 0.30f);
}

// ---------------------------------------------------------------------------
// Combat helpers
// ---------------------------------------------------------------------------

static void applyDamage(Game& g, Player& target, float dmg, Player* source) {
    if (!target.alive || dmg <= 0) return;
    float absorbedByShield = 0;
    if (target.shield > 0) {
        absorbedByShield = std::min(target.shield, dmg);
        target.shield -= absorbedByShield;
        dmg -= absorbedByShield;
    }
    target.health -= dmg;

    if (source) {
        source->ultCharge = std::min(source->ultMax, source->ultCharge + dmg * 0.22f);
        if (source->id == g.humanIndex) sfxHitMarker();
    }
    if (absorbedByShield > 0) {
        target.ultCharge = std::min(target.ultMax, target.ultCharge + absorbedByShield * 0.1f);
    }

    if (target.health <= 0) {
        target.health = 0;
        target.alive = false;
        target.respawnTimer = 5.0f;
        target.shield = 0; target.shieldTimer = 0;
        target.ultActive = false; target.ultTimer = 0; target.barrierHP = 0;
        if (target.id == g.humanIndex) sfxDeath();
    }
}

static void applyHeal(Game& g, Player& target, float amount, Player* source) {
    if (!target.alive || amount <= 0) return;
    float before = target.health;
    target.health = std::min(target.maxHealth, target.health + amount);
    float actual = target.health - before;
    if (source && actual > 0) {
        source->ultCharge = std::min(source->ultMax, source->ultCharge + actual * 0.22f);
    }
}

static void healerPulse(Game& g, Player& src, float radius, float healAmt) {
    for (auto& other : g.players) {
        if (other.team != src.team || !other.alive) continue;
        if (dist(other.pos, src.pos) <= radius) {
            applyHeal(g, other, healAmt, &src);
        }
    }
}

static bool canShoot(const Player& p) {
    return !p.reloading && (p.maxAmmo < 0 || p.ammo > 0);
}

static void startReload(Player& p) {
    if (p.maxAmmo < 0 || p.reloading || p.ammo >= p.maxAmmo) return;
    p.reloading = true;
    p.reloadTimer = 2.0f;
}

static void fireShot(Game& g, Player& p) {
    Projectile proj;
    proj.team = p.team;
    proj.sourceRole = p.role;
    proj.sourceId = p.id;
    proj.pos = p.pos;
    Vec2 dir{ std::cos(p.angle), std::sin(p.angle) };

    if (p.maxAmmo >= 0) p.ammo--;

    switch (p.role) {
        case Role::Tank:
            proj.vel = { dir.x * 210.0f, dir.y * 210.0f };
            proj.damage = 50; proj.radius = 9;
            p.shootCooldown = 1.0f;
            sfxShotTank();
            break;
        case Role::Damage:
            proj.vel = { dir.x * 400.0f, dir.y * 400.0f };
            proj.damage = 18; proj.radius = 4;
            proj.pierce = p.ultActive;
            p.shootCooldown = 0.28f;
            sfxShotDamage();
            break;
        case Role::Healer:
            proj.vel = { dir.x * 270.0f, dir.y * 270.0f };
            proj.damage = 12; proj.heal = 12; proj.radius = 6;
            p.shootCooldown = 0.3f;
            sfxShotHealer();
            break;
    }
    g.projectiles.push_back(proj);
}

static void useSecondaryAbility(Game& g, Player& p) {
    if (p.abilityCooldown > 0 || !p.alive) return;
    switch (p.role) {
        case Role::Tank:
            p.shield = p.maxShield;
            p.shieldTimer = 6.0f;
            p.abilityCooldown = 10.0f;
            break;
        case Role::Damage:
            p.sprintTimer = 3.0f;
            p.abilityCooldown = 7.0f;
            break;
        case Role::Healer:
            healerPulse(g, p, 150.0f, 25.0f);
            p.abilityCooldown = 9.0f;
            break;
    }
}

static void useUltimate(Game& g, Player& p) {
    if (p.ultCharge < p.ultMax || !p.alive) return;
    p.ultCharge = 0;
    p.ultActive = true;
    switch (p.role) {
        case Role::Tank:
            p.ultTimer = 6.0f;
            p.barrierHP = 2000.0f;
            p.barrierPos = p.pos;
            break;
        case Role::Damage:
            p.ultTimer = 5.0f;
            break;
        case Role::Healer:
            p.ultTimer = 10.0f;
            p.autoPulseTimer = 0.0f;
            break;
    }
}

// ---------------------------------------------------------------------------
// Bot AI
// ---------------------------------------------------------------------------

static Player* nearestEnemy(Game& g, Player& p) {
    Player* best = nullptr;
    float bestD = 1e9f;
    for (auto& o : g.players) {
        if (o.team == p.team || !o.alive) continue;
        float d = dist(o.pos, p.pos);
        if (d < bestD) { bestD = d; best = &o; }
    }
    return best;
}

static float frand(float lo, float hi) {
    return lo + (float)std::rand() / (float)RAND_MAX * (hi - lo);
}

static Vec2 clampPoint(Vec2 v) {
    if (v.x < ARENA_X0 + 30) v.x = ARENA_X0 + 30;
    if (v.x > ARENA_X1 - 30) v.x = ARENA_X1 - 30;
    if (v.y < ARENA_Y0 + 30) v.y = ARENA_Y0 + 30;
    if (v.y > ARENA_Y1 - 30) v.y = ARENA_Y1 - 30;
    return v;
}

// Picks the teammate most in need of help: low health, or currently being closed on by an enemy.
static Player* findAllyInTrouble(Game& g, Player& p) {
    Player* best = nullptr;
    float bestScore = -1e9f;
    for (auto& o : g.players) {
        if (o.id == p.id || o.team != p.team || !o.alive) continue;
        Player* threat = nearestEnemy(g, o);
        bool underAttack = threat && dist(threat->pos, o.pos) < 260.0f;
        bool lowHP = o.health < o.maxHealth * 0.5f;
        if (!underAttack && !lowHP) continue;
        float score = (o.maxHealth - o.health) + (underAttack ? 50.0f : 0.0f);
        if (score > bestScore) { bestScore = score; best = &o; }
    }
    return best;
}

// Chooses a movement destination for a bot: flank (Damage), intervene (Tank),
// fall back behind the team (Healer). Regrouping to help a threatened teammate
// is rolled probabilistically so it doesn't happen every single time. Bots also
// prioritize taking the capture point while their team doesn't control it, and
// retreat home instead of fighting on if both of their teammates are dead.
static Vec2 decideBotTarget(Game& g, Player& p) {
    int aliveTeammates = 0;
    for (auto& o : g.players) {
        if (o.id != p.id && o.team == p.team && o.alive) aliveTeammates++;
    }
    if (aliveTeammates == 0) {
        Vec2 target = { p.spawnPos.x + frand(-35.0f, 35.0f), p.spawnPos.y + frand(-35.0f, 35.0f) };
        return clampPoint(target);
    }

    Player* enemy = nearestEnemy(g, p);
    Player* trouble = findAllyInTrouble(g, p);
    bool weControlPoint = (g.controllingTeam == (p.team == Team::A ? 0 : 1));

    // When our team doesn't hold the point, contesting it takes priority over regrouping
    // unless the teammate in trouble is also near the point (helping them still serves the contest).
    bool regroup = trouble && frand(0.0f, 1.0f) < 0.55f;
    if (regroup && !weControlPoint && dist(trouble->pos, {POINT_CX, POINT_CY}) > POINT_RADIUS * 2.0f) {
        regroup = false;
    }

    Vec2 target = p.pos;

    if (p.role == Role::Tank) {
        if (regroup) {
            Player* threat = nearestEnemy(g, *trouble);
            if (threat) {
                Vec2 dir = vnorm({ threat->pos.x - trouble->pos.x, threat->pos.y - trouble->pos.y });
                target = { trouble->pos.x + dir.x * 60.0f, trouble->pos.y + dir.y * 60.0f };
            } else {
                target = trouble->pos;
            }
        } else if (!weControlPoint && !(enemy && dist(enemy->pos, p.pos) < 140.0f)) {
            float a = frand(0.0f, 6.2831f);
            float r = frand(0.0f, POINT_RADIUS * 0.7f);
            target = { POINT_CX + std::cos(a) * r, POINT_CY + std::sin(a) * r };
        } else if (enemy) {
            Vec2 dir = vnorm({ enemy->pos.x - p.pos.x, enemy->pos.y - p.pos.y });
            target = { enemy->pos.x - dir.x * 150.0f, enemy->pos.y - dir.y * 150.0f };
        } else {
            float a = frand(0.0f, 6.2831f);
            float r = frand(0.0f, POINT_RADIUS * 1.6f);
            target = { POINT_CX + std::cos(a) * r, POINT_CY + std::sin(a) * r };
        }
    } else if (p.role == Role::Damage) {
        if (regroup && frand(0.0f, 1.0f) < 0.5f) {
            target = { trouble->pos.x + frand(-60.0f, 60.0f), trouble->pos.y + frand(-60.0f, 60.0f) };
        } else if (!weControlPoint && !(enemy && dist(enemy->pos, p.pos) < 110.0f)) {
            float a = frand(0.0f, 6.2831f);
            float r = frand(0.0f, POINT_RADIUS * 0.9f);
            target = { POINT_CX + std::cos(a) * r, POINT_CY + std::sin(a) * r };
        } else if (enemy) {
            Vec2 toEnemy = vnorm({ enemy->pos.x - p.pos.x, enemy->pos.y - p.pos.y });
            Vec2 perp{ -toEnemy.y, toEnemy.x };
            float side = (std::rand() % 2 == 0) ? 1.0f : -1.0f;
            target = {
                enemy->pos.x + perp.x * side * 200.0f - toEnemy.x * 60.0f,
                enemy->pos.y + perp.y * side * 200.0f - toEnemy.y * 60.0f
            };
        } else {
            float xLo = (p.team == Team::A) ? POINT_CX - 120 : ARENA_X0 + 60;
            float xHi = (p.team == Team::A) ? ARENA_X1 - 60 : POINT_CX + 120;
            target = { frand(xLo, xHi), frand(ARENA_Y0 + 60, ARENA_Y1 - 60) };
        }
    } else { // Healer - always trails the team rather than pushing onto the point itself
        if (regroup) {
            target = {
                p.pos.x + (trouble->pos.x - p.pos.x) * 0.5f,
                p.pos.y + (trouble->pos.y - p.pos.y) * 0.5f
            };
        } else {
            Vec2 sum{0,0}; int n = 0;
            for (auto& o : g.players) {
                if (o.team == p.team && o.alive && o.id != p.id) { sum.x += o.pos.x; sum.y += o.pos.y; n++; }
            }
            Vec2 centroid = (n > 0) ? Vec2{ sum.x / n, sum.y / n } : p.pos;
            target = {
                centroid.x + (p.spawnPos.x - centroid.x) * 0.4f,
                centroid.y + (p.spawnPos.y - centroid.y) * 0.4f
            };
        }
    }

    target.x += frand(-35.0f, 35.0f);
    target.y += frand(-35.0f, 35.0f);
    return clampPoint(target);
}

static void updateBot(Game& g, Player& p, float dt) {
    Player* enemy = nearestEnemy(g, p);

    if (p.role == Role::Healer) {
        // Prefer aiming the heal/damage shot at the most-hurt nearby ally over attacking;
        // this is what actually gets bot healers to heal teammates with their normal shot.
        Player* healTarget = nullptr;
        float bestFrac = 0.9f;
        for (auto& o : g.players) {
            if (o.team != p.team || !o.alive) continue;
            float frac = o.health / o.maxHealth;
            if (frac < bestFrac && dist(o.pos, p.pos) < 420.0f) { bestFrac = frac; healTarget = &o; }
        }
        if (healTarget) {
            Vec2 toAlly{ healTarget->pos.x - p.pos.x, healTarget->pos.y - p.pos.y };
            p.angle = std::atan2(toAlly.y, toAlly.x);
            if (p.shootCooldown <= 0 && canShoot(p)) fireShot(g, p);
        } else if (enemy) {
            Vec2 toEnemy{ enemy->pos.x - p.pos.x, enemy->pos.y - p.pos.y };
            p.angle = std::atan2(toEnemy.y, toEnemy.x);
            if (p.shootCooldown <= 0 && canShoot(p)) fireShot(g, p);
        }
    } else if (enemy) {
        Vec2 toEnemy{ enemy->pos.x - p.pos.x, enemy->pos.y - p.pos.y };
        p.angle = std::atan2(toEnemy.y, toEnemy.x);
        if (p.shootCooldown <= 0 && canShoot(p)) fireShot(g, p);
    }

    if (p.ammo <= 0) startReload(p);

    p.decisionTimer -= dt;
    if (p.decisionTimer <= 0.0f) {
        p.targetPos = decideBotTarget(g, p);
        p.decisionTimer = frand(0.8f, 1.8f);
    }

    Vec2 toTarget{ p.targetPos.x - p.pos.x, p.targetPos.y - p.pos.y };
    Vec2 moveDir{0,0};
    if (vlen(toTarget) > 12.0f) {
        moveDir = vnorm(toTarget);
        p.strafePhase += dt * 3.0f;
        Vec2 perp{ -moveDir.y, moveDir.x };
        float wiggle = std::sin(p.strafePhase + p.id * 1.7f) * 0.25f;
        moveDir = vnorm({ moveDir.x + perp.x * wiggle, moveDir.y + perp.y * wiggle });
    }

    float speed = p.moveSpeed;
    if (p.sprintTimer > 0) speed *= 1.8f;
    if (p.ultActive && p.role == Role::Tank) speed = 0;

    p.pos.x += moveDir.x * speed * dt;
    p.pos.y += moveDir.y * speed * dt;

    // secondary ability usage
    if (p.role == Role::Tank) {
        bool enemyNear = enemy && dist(enemy->pos, p.pos) < 220;
        if (p.shield <= 0 && p.abilityCooldown <= 0 && (enemyNear || p.health < p.maxHealth * 0.4f)) {
            useSecondaryAbility(g, p);
        }
    } else if (p.role == Role::Damage) {
        bool farFromEnemy = enemy && dist(enemy->pos, p.pos) > 320;
        bool lowHealth = p.health < p.maxHealth * 0.3f;
        if (p.sprintTimer <= 0 && p.abilityCooldown <= 0 && (farFromEnemy || lowHealth)) {
            useSecondaryAbility(g, p);
        }
    } else if (p.role == Role::Healer) {
        bool teammateHurt = false;
        for (auto& o : g.players) {
            if (o.team == p.team && o.alive && dist(o.pos, p.pos) < 150 && o.health < o.maxHealth * 0.7f) {
                teammateHurt = true; break;
            }
        }
        if (p.abilityCooldown <= 0 && teammateHurt) useSecondaryAbility(g, p);
    }

    if (p.ultCharge >= p.ultMax && !p.ultActive) useUltimate(g, p);
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------

static void clampToArena(Player& p) {
    float r = 16;
    if (p.pos.x < ARENA_X0 + r) p.pos.x = ARENA_X0 + r;
    if (p.pos.x > ARENA_X1 - r) p.pos.x = ARENA_X1 - r;
    if (p.pos.y < ARENA_Y0 + r) p.pos.y = ARENA_Y0 + r;
    if (p.pos.y > ARENA_Y1 - r) p.pos.y = ARENA_Y1 - r;
}

// Keeps each team out of the other team's spawn strip.
static void blockEnemySpawn(Player& p) {
    if (p.team != Team::A) {
        float limit = ARENA_X0 + SPAWN_ZONE_DEPTH;
        if (p.pos.x < limit) p.pos.x = limit;
    }
    if (p.team != Team::B) {
        float limit = ARENA_X1 - SPAWN_ZONE_DEPTH;
        if (p.pos.x > limit) p.pos.x = limit;
    }
}

static void updatePlayerTimers(Game& g, Player& p, float dt) {
    if (p.shootCooldown > 0) p.shootCooldown -= dt;
    if (p.abilityCooldown > 0) p.abilityCooldown -= dt;
    if (p.sprintTimer > 0) p.sprintTimer -= dt;

    if (p.reloading) {
        p.reloadTimer -= dt;
        if (p.reloadTimer <= 0.0f) {
            p.reloading = false;
            p.ammo = p.maxAmmo;
        }
    }

    if (p.shieldTimer > 0) {
        p.shieldTimer -= dt;
        if (p.shieldTimer <= 0 || p.shield <= 0) { p.shieldTimer = 0; p.shield = 0; }
    }

    if (p.ultActive) {
        p.ultTimer -= dt;
        if (p.role == Role::Healer) {
            p.autoPulseTimer -= dt;
            if (p.autoPulseTimer <= 0) {
                healerPulse(g, p, 150.0f, 15.0f);
                p.autoPulseTimer = 1.2f;
            }
        }
        if (p.role == Role::Tank) {
            if (p.barrierHP <= 0) p.ultTimer = 0;
            // push enemies out of barrier
            for (auto& o : g.players) {
                if (o.team == p.team || !o.alive) continue;
                float d = dist(o.pos, p.barrierPos);
                if (d < 140.0f && d > 0.01f) {
                    Vec2 dir = vnorm({o.pos.x - p.barrierPos.x, o.pos.y - p.barrierPos.y});
                    o.pos.x = p.barrierPos.x + dir.x * 140.0f;
                    o.pos.y = p.barrierPos.y + dir.y * 140.0f;
                }
            }
        }
        if (p.ultTimer <= 0) {
            p.ultActive = false;
            p.ultTimer = 0;
            p.barrierHP = 0;
        }
    }

    if (!p.alive) {
        p.respawnTimer -= dt;
        if (p.respawnTimer <= 0) resetPlayer(p);
    }
}

static void updateProjectiles(Game& g, float dt) {
    for (auto& proj : g.projectiles) {
        proj.pos.x += proj.vel.x * dt;
        proj.pos.y += proj.vel.y * dt;

        if (proj.pos.x < ARENA_X0 || proj.pos.x > ARENA_X1 || proj.pos.y < ARENA_Y0 || proj.pos.y > ARENA_Y1) {
            proj.alive = false;
            continue;
        }

        // projectiles can't fly into the enemy's spawn strip (mirrors the player-movement block)
        if (proj.team == Team::A && proj.pos.x > ARENA_X1 - SPAWN_ZONE_DEPTH) { proj.alive = false; continue; }
        if (proj.team == Team::B && proj.pos.x < ARENA_X0 + SPAWN_ZONE_DEPTH) { proj.alive = false; continue; }

        // barrier collision: barriers stop every projectile outright, pierce included
        // (only affects opposing team's projectiles)
        for (auto& owner : g.players) {
            if (owner.role != Role::Tank || !owner.ultActive || owner.barrierHP <= 0) continue;
            if (owner.team == proj.team) continue;
            if (dist(proj.pos, owner.barrierPos) < 140.0f) {
                owner.barrierHP -= proj.damage;
                proj.alive = false;
                break;
            }
        }
        if (!proj.alive) continue;

        for (auto& target : g.players) {
            if (!target.alive) continue;
            if (target.id == proj.sourceId) continue; // never hits its own shooter
            if (std::find(proj.alreadyHit.begin(), proj.alreadyHit.end(), target.id) != proj.alreadyHit.end()) continue;
            if (dist(proj.pos, target.pos) > (proj.radius + 16.0f)) continue;

            Player* srcPlayer = nullptr;
            for (auto& s : g.players) if (s.id == proj.sourceId) { srcPlayer = &s; break; }

            bool hadActiveShield = target.shield > 0;

            if (target.team == proj.team) {
                if (proj.heal > 0) applyHeal(g, target, proj.heal, srcPlayer);
                else continue; // friendly fire not applied for non-healers
            } else {
                applyDamage(g, target, proj.damage, srcPlayer);
            }
            g.hitMarkers.push_back({ proj.pos, HIT_MARKER_LIFETIME });

            // a personal shield stops a pierce shot too, same as a barrier would
            if (proj.pierce && !hadActiveShield) {
                proj.alreadyHit.push_back(target.id);
            } else {
                proj.alive = false;
            }
            break;
        }
    }
    g.projectiles.erase(std::remove_if(g.projectiles.begin(), g.projectiles.end(),
        [](const Projectile& p){ return !p.alive; }), g.projectiles.end());

    for (auto& m : g.hitMarkers) m.timer -= dt;
    g.hitMarkers.erase(std::remove_if(g.hitMarkers.begin(), g.hitMarkers.end(),
        [](const HitMarker& m){ return m.timer <= 0; }), g.hitMarkers.end());
}

// Point state machine:
//   Uncontrolled              - no one has ever won control (start of game only)
//   Uncontrolled & Contested  - both teams present, no progress for either
//   Controlled                - a team holds control, point empty or held alone, progress ticks
//   Controlled & Contested    - a team holds control, enemy also present, progress still ticks
//   Captured & Contested      - progress hit 100 while contested: overtime until resolved
//   Captured                  - progress at 100 and uncontested: win, match ends
// Gaining control (from neutral) or flipping it (from the other team) both require a team
// to be the ONLY team on the point for 5 continuous, uncontested seconds.
static void updateCapturePoint(Game& g, float dt) {
    int teamACount = 0, teamBCount = 0;
    for (auto& p : g.players) {
        if (!p.alive) continue;
        if (dist(p.pos, {POINT_CX, POINT_CY}) <= POINT_RADIUS) {
            if (p.team == Team::A) teamACount++; else teamBCount++;
        }
    }
    bool aPresent = teamACount > 0;
    bool bPresent = teamBCount > 0;
    bool contested = aPresent && bPresent;
    bool wasContested = g.pointContested;
    g.pointContested = contested;
    if (contested && !wasContested) sfxContested();

    int aloneTeam = -1;
    if (aPresent && !bPresent) aloneTeam = 0;
    else if (bPresent && !aPresent) aloneTeam = 1;

    if (aloneTeam != -1 && aloneTeam != g.controllingTeam) {
        // a team other than the current controller (or nobody, at game start) is alone here
        if (g.flipCandidateTeam != aloneTeam) {
            g.flipCandidateTeam = aloneTeam;
            g.flipTimer = 0.0f;
        }
        g.flipTimer += dt;
        if (g.flipTimer >= 5.0f) {
            g.controllingTeam = aloneTeam;
            g.progress = 0.0f;
            g.captureSoundPlayed = false;
            g.flipTimer = 0.0f;
            g.flipCandidateTeam = -1;
            sfxControlGained();
        }
    } else {
        // contested, empty, or the current controller is the one alone: no flip in progress
        g.flipTimer = 0.0f;
        g.flipCandidateTeam = -1;
    }

    // Once a team has control, progress advances regardless of contest state.
    if (g.controllingTeam != -1) {
        g.progress = std::min(100.0f, g.progress + CAPTURE_RATE_PER_SEC * dt);
    }

    if (g.progress >= 100.0f && !g.captureSoundPlayed) {
        g.captureSoundPlayed = true;
        sfxCapture();
    }

    // Only finalize the win once uncontested; an enemy on the point at 100% forces overtime.
    if (g.progress >= 100.0f && !contested && !g.matchOver) {
        g.matchOver = true;
        g.winningTeam = g.controllingTeam;
        g.matchOverTimer = 4.0f;
        sfxWin();
    }
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

struct Input {
    bool up=false, down=false, left=false, right=false;
    bool leftMouseDown=false, rightMouseDown=false;
    bool shiftDown=false;
    bool qPressed=false;   // edge-triggered
    bool rightPressed=false; // edge-triggered (right click or shift)
    float mouseX=0, mouseY=0;
};

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    std::srand((unsigned)std::time(nullptr));

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Underwatch (minimal prototype)", WINDOW_W, WINDOW_H, 0);
    if (!window) { SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) { SDL_Log("CreateRenderer failed: %s", SDL_GetError()); return 1; }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    SDL_AudioSpec audioSpec{ SDL_AUDIO_F32, 1, AUDIO_SAMPLE_RATE };
    g_audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &audioSpec, audioCallback, nullptr);
    if (g_audioStream) SDL_ResumeAudioStreamDevice(g_audioStream);
    else SDL_Log("Audio unavailable: %s", SDL_GetError());

    Game game;
    initGame(game);

    bool running = true;
    bool paused = false;
    Uint64 lastTicks = SDL_GetTicks();

    bool prevQ = false, prevRightEdge = false;

    while (running) {
        Uint64 now = SDL_GetTicks();
        float dt = (now - lastTicks) / 1000.0f;
        if (dt > 0.05f) dt = 0.05f; // clamp big pauses
        lastTicks = now;

        static Input input;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) running = false;
            else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_ESCAPE) {
                paused = !paused;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_BACKSPACE) {
                running = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_TAB) {
                // cycle human control among the 3 teammates (indices 0,1,2 are always Team A)
                int newIndex = (game.humanIndex + 1) % 3;
                game.players[game.humanIndex].isBot = true;
                game.players[newIndex].isBot = false;
                game.humanIndex = newIndex;
                prevQ = false;
                prevRightEdge = false;
            } else if (ev.type == SDL_EVENT_KEY_DOWN && !ev.key.repeat && ev.key.key == SDLK_R) {
                startReload(game.players[game.humanIndex]);
            } else if (ev.type == SDL_EVENT_KEY_DOWN || ev.type == SDL_EVENT_KEY_UP) {
                bool down = (ev.type == SDL_EVENT_KEY_DOWN);
                switch (ev.key.key) {
                    case SDLK_W: input.up = down; break;
                    case SDLK_S: input.down = down; break;
                    case SDLK_A: input.left = down; break;
                    case SDLK_D: input.right = down; break;
                    case SDLK_Q: input.qPressed = down; break;
                    case SDLK_LSHIFT: case SDLK_RSHIFT: input.shiftDown = down; break;
                    default: break;
                }
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN || ev.type == SDL_EVENT_MOUSE_BUTTON_UP) {
                bool down = (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN);
                if (ev.button.button == SDL_BUTTON_LEFT) input.leftMouseDown = down;
                if (ev.button.button == SDL_BUTTON_RIGHT) input.rightMouseDown = down;
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                input.mouseX = ev.motion.x;
                input.mouseY = ev.motion.y;
            }
        }

        if (paused) {
            // all gameplay frozen; only rendering (with a paused overlay) continues below
        } else if (game.preRoundTimer > 0) {
            game.preRoundTimer -= dt;
            if (game.preRoundTimer < 0) game.preRoundTimer = 0;
            int n = (int)std::ceil(game.preRoundTimer);
            if (n != game.lastCountdownTick && n >= 1) {
                game.lastCountdownTick = n;
                sfxCountdownTick();
            }
        } else if (!game.matchOver) {
            Player& human = game.players[game.humanIndex];

            if (human.alive) {
                Vec2 moveDir{0,0};
                if (input.up) moveDir.y -= 1;
                if (input.down) moveDir.y += 1;
                if (input.left) moveDir.x -= 1;
                if (input.right) moveDir.x += 1;
                moveDir = vnorm(moveDir);

                float speed = human.moveSpeed;
                if (human.sprintTimer > 0) speed *= 1.8f;
                if (human.ultActive && human.role == Role::Tank) speed = 0;

                human.pos.x += moveDir.x * speed * dt;
                human.pos.y += moveDir.y * speed * dt;

                human.angle = std::atan2(input.mouseY - human.pos.y, input.mouseX - human.pos.x);

                if (input.leftMouseDown && human.shootCooldown <= 0) {
                    if (canShoot(human)) {
                        fireShot(game, human);
                    } else if (human.maxAmmo >= 0 && human.ammo <= 0 && !human.reloading) {
                        game.ammoPopupTimer = 1.2f;
                    }
                }

                bool rightEdgeNow = input.rightMouseDown || input.shiftDown;
                if (rightEdgeNow && !prevRightEdge) useSecondaryAbility(game, human);
                prevRightEdge = rightEdgeNow;

                if (input.qPressed && !prevQ) useUltimate(game, human);
                prevQ = input.qPressed;
            }

            for (auto& p : game.players) {
                if (p.isBot && p.alive) updateBot(game, p, dt);
            }
            for (auto& p : game.players) {
                clampToArena(p);
                blockEnemySpawn(p);
                updatePlayerTimers(game, p, dt);
            }

            updateProjectiles(game, dt);
            updateCapturePoint(game, dt);
            if (game.ammoPopupTimer > 0) game.ammoPopupTimer -= dt;
        } else {
            game.matchOverTimer -= dt;
            if (game.matchOverTimer <= 0) initGame(game);
        }

        // -------------------- render --------------------
        SDL_SetRenderDrawColor(renderer, 18, 18, 22, 255);
        SDL_RenderClear(renderer);

        // arena floor + solid wall boundary
        setColor(renderer, {28,28,34,255});
        SDL_FRect arenaFloor{ ARENA_X0, ARENA_Y0, ARENA_X1-ARENA_X0, ARENA_Y1-ARENA_Y0 };
        SDL_RenderFillRect(renderer, &arenaFloor);
        drawWallFrame(renderer, ARENA_X0, ARENA_Y0, ARENA_X1, ARENA_Y1, 10.0f, {110,100,90,255});

        // enemy-spawn no-go strips (tinted, only the owning team may stand here)
        {
            Col ca = teamColor(Team::A), cb = teamColor(Team::B);
            SDL_FRect zoneA{ ARENA_X0, ARENA_Y0, SPAWN_ZONE_DEPTH, ARENA_Y1-ARENA_Y0 };
            SDL_FRect zoneB{ ARENA_X1-SPAWN_ZONE_DEPTH, ARENA_Y0, SPAWN_ZONE_DEPTH, ARENA_Y1-ARENA_Y0 };
            setColor(renderer, {ca.r,ca.g,ca.b,22});
            SDL_RenderFillRect(renderer, &zoneA);
            setColor(renderer, {cb.r,cb.g,cb.b,22});
            SDL_RenderFillRect(renderer, &zoneB);
            setColor(renderer, {ca.r,ca.g,ca.b,140});
            SDL_RenderLine(renderer, ARENA_X0+SPAWN_ZONE_DEPTH, ARENA_Y0, ARENA_X0+SPAWN_ZONE_DEPTH, ARENA_Y1);
            setColor(renderer, {cb.r,cb.g,cb.b,140});
            SDL_RenderLine(renderer, ARENA_X1-SPAWN_ZONE_DEPTH, ARENA_Y0, ARENA_X1-SPAWN_ZONE_DEPTH, ARENA_Y1);
        }

        // capture point
        setColor(renderer, {120,120,120,255});
        drawCircleOutline(renderer, POINT_CX, POINT_CY, POINT_RADIUS);
        if (game.controllingTeam != -1) {
            Col c = (game.controllingTeam == 0) ? Col{70,140,255,255} : Col{255,70,70,255};
            setColor(renderer, c);
        } else {
            setColor(renderer, {80,80,80,255});
        }
        float fillR = POINT_RADIUS * (game.progress / 100.0f);
        if (fillR > 1) drawFilledCircle(renderer, POINT_CX, POINT_CY, fillR);

        // tank barriers
        for (auto& p : game.players) {
            if (p.role == Role::Tank && p.ultActive && p.barrierHP > 0) {
                Col c = teamColor(p.team);
                setColor(renderer, {c.r, c.g, c.b, 90});
                drawFilledCircle(renderer, p.barrierPos.x, p.barrierPos.y, 140.0f);
                setColor(renderer, {c.r, c.g, c.b, 255});
                drawCircleOutline(renderer, p.barrierPos.x, p.barrierPos.y, 140.0f, 48);
            }
        }

        // projectiles: shape hints at who fired it - tank cannonballs are round,
        // damage shots are slim bullets, healer shots are little potion flasks.
        for (auto& proj : game.projectiles) {
            Col c = teamColor(proj.team);
            setColor(renderer, c);
            switch (proj.sourceRole) {
                case Role::Tank:
                    drawFilledCircle(renderer, proj.pos.x, proj.pos.y, proj.radius);
                    break;
                case Role::Damage: {
                    float angle = std::atan2(proj.vel.y, proj.vel.x);
                    drawFilledPolygon(renderer, proj.pos, bulletPoints(proj.pos, proj.radius, angle), c);
                    break;
                }
                case Role::Healer: {
                    float angle = std::atan2(proj.vel.y, proj.vel.x);
                    drawFilledPolygon(renderer, proj.pos, potionPoints(proj.pos, proj.radius, angle), c);
                    break;
                }
            }
        }

        // hit markers: brief white pop where a shot just connected
        for (auto& m : game.hitMarkers) {
            float t = m.timer / HIT_MARKER_LIFETIME;
            Uint8 alpha = (Uint8)std::clamp(t * 255.0f, 0.0f, 255.0f);
            float size = 6.0f + (1.0f - t) * 4.0f;
            setColor(renderer, {255, 255, 255, alpha});
            SDL_RenderLine(renderer, m.pos.x - size, m.pos.y - size, m.pos.x + size, m.pos.y + size);
            SDL_RenderLine(renderer, m.pos.x - size, m.pos.y + size, m.pos.x + size, m.pos.y - size);
        }

        // players
        for (auto& p : game.players) {
            if (!p.alive) continue;
            Col c = teamColor(p.team);
            float radius = (p.role == Role::Tank) ? 22.0f : (p.role == Role::Healer ? 18.0f : 18.0f);
            auto shape = playerShapePoints(p, radius);

            if (p.id == game.humanIndex) {
                drawFilledPolygon(renderer, p.pos, shape, c);
                setColor(renderer, {0,0,0,255});
                drawPolygonOutline(renderer, shape);
            } else {
                setColor(renderer, c);
                drawPolygonOutline(renderer, shape);
                drawPolygonOutline(renderer, playerShapePoints(p, radius - 2.0f));
            }

            // aim direction line
            setColor(renderer, {255,255,255,255});
            SDL_RenderLine(renderer, p.pos.x, p.pos.y,
                p.pos.x + std::cos(p.angle) * (radius + 14),
                p.pos.y + std::sin(p.angle) * (radius + 14));

            // shield ring
            if (p.shield > 0) {
                setColor(renderer, {255, 230, 80, 255});
                drawCircleOutline(renderer, p.pos.x, p.pos.y, radius + 6, 24);
            }

            // health bar
            float barW = 40, barH = 5;
            drawBar(renderer, p.pos.x - barW/2, p.pos.y - radius - 16, barW, barH,
                p.health / p.maxHealth, {60,200,60,255}, {40,40,40,255});

            // shield bar (thin, above health bar)
            if (p.maxShield > 0 && p.shield > 0) {
                drawBar(renderer, p.pos.x - barW/2, p.pos.y - radius - 22, barW, 3,
                    p.shield / p.maxShield, {255,220,60,255}, {40,40,40,255});
            }
        }

        // respawn indicators: ghost outline + countdown number while dead
        for (auto& p : game.players) {
            if (p.alive) continue;
            Col c = teamColor(p.team);
            setColor(renderer, {c.r,c.g,c.b,80});
            Player ghost = p;
            ghost.pos = p.spawnPos;
            drawPolygonOutline(renderer, playerShapePoints(ghost, 14));

            int secondsLeft = (int)std::ceil(p.respawnTimer);
            if (secondsLeft < 0) secondsLeft = 0;
            drawTextCentered(renderer, p.spawnPos.x, p.spawnPos.y - 4, std::to_string(secondsLeft), 2.5f, {c.r,c.g,c.b,255});
        }

        // ---- UI: human status bottom-left, with labels beside the bars ----
        {
            Player& h = game.players[game.humanIndex];
            Col labelCol{ 220, 220, 220, 255 };
            float barX = 70, barW = 220;

            float hpY = WINDOW_H - 78;
            drawText(renderer, 20, hpY + 1, "HP", 2.0f, labelCol);
            drawBar(renderer, barX, hpY, barW, 16, h.alive ? h.health / h.maxHealth : 0,
                {60,200,60,255}, {40,40,40,255});

            float ultY = WINDOW_H - 54;
            drawText(renderer, 20, ultY - 2, "ULT", 2.0f, labelCol);
            drawBar(renderer, barX, ultY, barW, 10, h.ultCharge / h.ultMax,
                {230,200,60,255}, {40,40,40,255});

            float ablY = WINDOW_H - 32;
            drawText(renderer, 20, ablY - 3, "ABL", 2.0f, labelCol);
            drawBar(renderer, barX, ablY, barW, 8,
                h.abilityCooldown > 0 ? 1.0f - (h.abilityCooldown / 10.0f) : 1.0f,
                {120,170,255,255}, {40,40,40,255});

            if (h.maxAmmo >= 0) {
                float ammoX = barX + barW + 20.0f;
                if (h.reloading) {
                    drawText(renderer, ammoX, ablY - 3, "RELOADING", 2.0f, {230,200,60,255});
                } else {
                    Col ammoCol = (h.ammo <= 0) ? Col{230,80,80,255} : labelCol;
                    drawText(renderer, ammoX, ablY - 3, "AMO " + std::to_string(h.ammo), 2.0f, ammoCol);
                }
            }
        }

        // ---- "out of ammo" popup ----
        if (game.ammoPopupTimer > 0) {
            drawTextCentered(renderer, WINDOW_W / 2.0f, WINDOW_H * 0.6f,
                "OUT OF AMMO   PRESS R TO RELOAD", 2.5f, {255,90,90,255});
        }

        // ---- UI: controls legend, aligned 3x3 grid centered in the leftover space ----
        {
            static const char* grid[3][3] = {
                { "WASD MOVE",   "MOUSE AIM",  "CLICK SHOOT" },
                { "R RELOAD",    "Q ULT",      "SHIFT ABILITY" },
                { "TAB SWITCH",  "ESC PAUSE",  "BACKSPACE QUIT" }
            };
            Col col{ 150, 150, 150, 255 };
            float px = 3.0f;
            float lineH = 7.0f * px + 4.0f;

            float colW[3] = { 190.0f, 170.0f, 260.0f };
            float gap = 40.0f;
            float totalW = colW[0] + gap + colW[1] + gap + colW[2];

            float leftBound = 70.0f + 220.0f + 40.0f;   // just past the HP/ULT/ABL bars
            float rightBound = (float)WINDOW_W - 40.0f;  // mirrors the arena's side margin
            float baseX = leftBound + ((rightBound - leftBound) - totalW) / 2.0f;

            float colX[3];
            colX[0] = baseX;
            colX[1] = colX[0] + colW[0] + gap;
            colX[2] = colX[1] + colW[1] + gap;

            float y = WINDOW_H - 78.0f;
            for (int row = 0; row < 3; row++) {
                for (int c = 0; c < 3; c++) {
                    drawText(renderer, colX[c], y + (float)row * lineH, grid[row][c], px, col);
                }
            }
        }

        // ---- UI: two capture meters growing from top-center (blue=left, red=right) ----
        {
            float halfW = 220, barH = 16;
            float y = 34;
            float centerX = WINDOW_W / 2.0f;

            float blueFrac = (game.controllingTeam == 0) ? (game.progress / 100.0f) : 0.0f;
            float redFrac  = (game.controllingTeam == 1) ? (game.progress / 100.0f) : 0.0f;

            setColor(renderer, {40,40,40,255});
            SDL_FRect leftBack{ centerX - halfW, y, halfW, barH };
            SDL_FRect rightBack{ centerX, y, halfW, barH };
            SDL_RenderFillRect(renderer, &leftBack);
            SDL_RenderFillRect(renderer, &rightBack);

            Col blueCol = teamColor(Team::A);
            setColor(renderer, blueCol);
            SDL_FRect blueFront{ centerX - halfW * blueFrac, y, halfW * blueFrac, barH };
            SDL_RenderFillRect(renderer, &blueFront);

            Col redCol = teamColor(Team::B);
            setColor(renderer, redCol);
            SDL_FRect redFront{ centerX, y, halfW * redFrac, barH };
            SDL_RenderFillRect(renderer, &redFront);

            setColor(renderer, {10,10,10,255});
            SDL_RenderRect(renderer, &leftBack);
            SDL_RenderRect(renderer, &rightBack);

            drawText(renderer, centerX - halfW - 8 - textWidth("BLUE", 3.0f), y + 3, "BLUE", 3.0f, blueCol);
            drawText(renderer, centerX + halfW + 8, y + 3, "RED", 3.0f, redCol);

            std::string stateLabel;
            if (game.matchOver) stateLabel = "CAPTURED";
            else if (game.progress >= 100.0f && game.pointContested) stateLabel = "OVERTIME";
            else if (game.controllingTeam == -1) stateLabel = game.pointContested ? "CONTESTED" : "UNCONTESTED";
            else stateLabel = game.pointContested ? "CONTESTED" : "CONTROLLED";
            drawTextCentered(renderer, centerX, y + barH + 8, stateLabel, 2.0f, {200,200,200,255});
        }

        // ---- 5-second control/flip timer indicator (near the point) ----
        if (game.flipTimer > 0 && game.flipCandidateTeam != -1) {
            float w = 100, h = 8;
            float x = POINT_CX - w / 2, y = POINT_CY + POINT_RADIUS + 18;
            setColor(renderer, {40,40,40,255});
            SDL_FRect back{x,y,w,h};
            SDL_RenderFillRect(renderer, &back);
            Col c = teamColor(game.flipCandidateTeam == 0 ? Team::A : Team::B);
            setColor(renderer, c);
            SDL_FRect front{x, y, w * (game.flipTimer / 5.0f), h};
            SDL_RenderFillRect(renderer, &front);
            setColor(renderer, {10,10,10,255});
            SDL_RenderRect(renderer, &back);
        }

        // ---- pre-round countdown ----
        if (game.preRoundTimer > 0) {
            int n = (int)std::ceil(game.preRoundTimer);
            if (n < 1) n = 1;
            drawTextCentered(renderer, WINDOW_W / 2.0f, WINDOW_H / 2.0f - 50, std::to_string(n), 10.0f, {255,255,255,255});
        }

        // ---- match over flash ----
        if (game.matchOver) {
            Col c = (game.winningTeam == 0) ? Col{70,140,255,90} : Col{255,70,70,90};
            setColor(renderer, c);
            SDL_FRect full{0,0,(float)WINDOW_W,(float)WINDOW_H};
            SDL_RenderFillRect(renderer, &full);
        }

        // ---- pause overlay ----
        if (paused) {
            setColor(renderer, {0,0,0,150});
            SDL_FRect full{0,0,(float)WINDOW_W,(float)WINDOW_H};
            SDL_RenderFillRect(renderer, &full);
            drawTextCentered(renderer, WINDOW_W / 2.0f, WINDOW_H / 2.0f - 40, "PAUSED", 6.0f, {255,255,255,255});
            drawTextCentered(renderer, WINDOW_W / 2.0f, WINDOW_H / 2.0f + 10, "ESC RESUME  BACKSPACE QUIT", 2.0f, {200,200,200,255});
        }

        SDL_RenderPresent(renderer);
    }

    if (g_audioStream) SDL_DestroyAudioStream(g_audioStream);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
