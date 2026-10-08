// Super Mario World physics para Geometry Dash (Geode 5.x, GD 2.2081, modo plataformas)
// Las constantes vienen de la fisica de SMW en unidades de "pixel de SNES por frame a 60fps".
// 1 tile SMW = 16 px = 1 bloque GD = 30 unidades  ->  1 px SMW = 1.875 unidades GD.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace smw {
    constexpr float PX      = 30.f / 16.f;   // px SMW -> unidades GD
    constexpr float FPS     = 60.f;
    // velocidades (px/frame) -> unidades GD/seg
    constexpr float WALK    = 1.5f  * PX * FPS;   // $18
    constexpr float RUN     = 2.5f  * PX * FPS;   // $28
    constexpr float PSPEED  = 3.5f  * PX * FPS;   // $38
    // aceleracion (aprox. SMW: ~1/16 px por frame^2 al caminar)
    constexpr float ACCEL   = (1.f/16.f) * 2.f * PX * FPS * FPS;
    constexpr float DECEL   = ACCEL * 1.6f;       // frenar / soltar
    constexpr float SKID    = ACCEL * 3.0f;       // cambio de direccion
    // vertical (px/frame y px/frame^2)
    constexpr float GRAV_HELD = 0.1875f * PX * FPS * FPS; // $03 con boton A mantenido
    constexpr float GRAV_FREE = 0.375f  * PX * FPS * FPS; // $06 sin mantener
    constexpr float FALL_MAX  = 4.0f    * PX * FPS;       // $40
    constexpr float JUMP_BASE = 5.0f    * PX * FPS;       // $B0 aprox
    constexpr float JUMP_BONUS= 0.25f   * PX * FPS;       // extra por velocidad
    constexpr float SPIN_JUMP = 4.6f    * PX * FPS;       // salto giratorio (no depende de vel.)
    constexpr float CAPE_FALL = 1.0f    * PX * FPS;       // planeo con capa

    struct State {
        float vx = 0.f, vy = 0.f;
        float pMeter = 0.f;   // 0..1, llena = P-speed
        bool  jumping = false;
        bool  spinning = false;
    };
    inline State s;
}

class $modify(SMWPlayer, PlayerObject) {
    void update(float dt) {
        if (!Mod::get()->getSettingValue<bool>("enabled")) {
            PlayerObject::update(dt);
            return;
        }
        // Solo en el jugador principal y en modo plataformas
        auto pl = PlayLayer::get();
        if (!pl || !m_isPlatformer || this != pl->m_player1) {
            PlayerObject::update(dt);
            return;
        }

        auto kb = CCDirector::get()->getKeyboardDispatcher();
        bool left  = m_holdingLeft;
        bool right = m_holdingRight;
        bool run   = kb->getShiftKeyPressed();                            // boton "B" = Shift
        bool spin  = kb->getControlKeyPressed();                          // boton "A" spin = Ctrl
        auto jb = m_holdingButtons.find(static_cast<int>(PlayerButton::Jump));
        bool jumpHeld = jb != m_holdingButtons.end() && jb->second;       // salto mantenido
        static bool prevJump = false, prevSpin = false;
        bool jumpPressed = jumpHeld && !prevJump;                          // solo al pulsar (como SMW)
        bool spinPressed = spin && !prevSpin;
        prevJump = jumpHeld; prevSpin = spin;

        auto& s = smw::s;
        const bool state_cape = Mod::get()->getSettingValue<std::string>("powerup") == "Cape";

        // ---- Horizontal ----
        float maxV = run ? (s.pMeter >= 1.f ? smw::PSPEED : smw::RUN) : smw::WALK;
        float dir = (right ? 1.f : 0.f) - (left ? 1.f : 0.f);
        if (dir != 0.f) {
            bool skidding = (s.vx * dir) < 0.f;
            float a = skidding ? smw::SKID : smw::ACCEL;
            s.vx += dir * a * dt;
            float lim = maxV;
            s.vx = std::clamp(s.vx, -lim, lim);
        } else {
            float d = smw::DECEL * dt;
            s.vx = (std::fabs(s.vx) <= d) ? 0.f : s.vx - std::copysign(d, s.vx);
        }
        // P-meter: sube corriendo a velocidad de carrera en el suelo, baja si no
        if (m_isOnGround && run && std::fabs(s.vx) >= smw::RUN * 0.98f)
            s.pMeter = std::min(1.f, s.pMeter + dt / (7.f / 60.f * 7.f)); // ~7 ticks
        else if (m_isOnGround)
            s.pMeter = std::max(0.f, s.pMeter - dt * 2.f);

        // ---- Vertical ----
        if (m_isOnGround) {
            s.jumping = s.spinning = false;
            s.vy = 0.f;
            if (jumpPressed || spinPressed) {
                s.jumping = true;
                s.spinning = spinPressed;
                s.vy = spinPressed ? smw::SPIN_JUMP
                            : smw::JUMP_BASE + smw::JUMP_BONUS * (std::fabs(s.vx) / smw::WALK);
            }
        } else {
            bool held = jumpHeld || s.spinning;
            float g = (held && s.vy > 0.f) ? smw::GRAV_HELD : smw::GRAV_FREE;
            s.vy -= g * dt;
            if (state_cape && s.pMeter >= 1.f && jumpHeld && s.vy < 0.f)
                s.vy = std::max(s.vy, -smw::CAPE_FALL);   // planeo
            s.vy = std::max(s.vy, -smw::FALL_MAX);
        }

        if (Mod::get()->getSettingValue<bool>("debug-log"))
            log::info("SMW vx={:.1f} vy={:.1f} P={:.2f} | GD platX={:.3f} yVel={:.3f} ground={}",
                s.vx, s.vy, s.pMeter, m_platformerXVelocity, m_yVelocity, m_isOnGround);

        // Aplicar a GD. m_platformerXVelocity = factor sobre la velocidad base (calibrar con el log).
        float base = 311.58f; // velocidad "1x" de GD
        m_platformerXVelocity = s.vx / base;
        m_yVelocity = s.vy / 60.f; // en GD m_yVelocity ~ unidades por frame a 60Hz (salto cubo ~11.18)

        PlayerObject::update(dt);
    }
};
