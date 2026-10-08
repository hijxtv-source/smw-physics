// Super Mario World physics para Geometry Dash (Geode 5.x, GD 2.2081, modo plataformas)  v0.3
// Enfoque: el mod calcula su propia velocidad (estilo SMW) y MUEVE la posicion del jugador
// despues del update original de GD. GD sigue resolviendo las colisiones (suelo, paredes, techo).
// 1 tile SMW = 16 px = 1 bloque GD = 30 unidades  ->  1 px SMW = 1.875 unidades GD.
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayerObject.hpp>
#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace smw {
    constexpr float PX  = 30.f / 16.f;
    constexpr float FPS = 60.f;

    // Horizontal (unidades GD / seg)
    constexpr float WALK   = 1.5f * PX * FPS;   // $18
    constexpr float RUN    = 2.5f * PX * FPS;   // $28
    constexpr float PSPEED = 3.5f * PX * FPS;   // $38
    constexpr float ACCEL  = (1.f/16.f) * 2.f * PX * FPS * FPS;
    constexpr float DECEL  = ACCEL * 1.6f;
    constexpr float SKID   = ACCEL * 3.0f;

    // Vertical (unidades GD / seg  y  / seg^2)
    constexpr float GRAV_HELD  = 0.1875f * PX * FPS * FPS;
    constexpr float GRAV_FREE  = 0.375f  * PX * FPS * FPS;
    constexpr float FALL_MAX   = 4.0f    * PX * FPS;
    constexpr float JUMP_BASE  = 5.0f    * PX * FPS;
    constexpr float JUMP_BONUS = 0.25f   * PX * FPS;
    constexpr float SPIN_JUMP  = 4.6f    * PX * FPS;
    constexpr float CAPE_FALL  = 1.0f    * PX * FPS;

    struct State {
        bool  init = false;
        float vx = 0.f, vy = 0.f;
        float pMeter = 0.f;
        bool  airborne = false, spinning = false;
        bool  left = false, right = false, jumpHeld = false;
        bool  jumpQueued = false, prevSpin = false;
        float expX = 0.f, expY = 0.f, lastDt = 0.f;
    };
    inline State s;

    inline bool enabled() { return Mod::get()->getSettingValue<bool>("enabled"); }
}

class $modify(SMWPlayer, PlayerObject) {
    bool isSMWPlayer() {
        if (!smw::enabled() || !m_isPlatformer) return false;
        auto pl = PlayLayer::get();
        return pl && this == pl->m_player1;
    }

    // --- Entrada: guardamos nosotros el estado de los botones ---
    bool pushButton(PlayerButton b) {
        if (isSMWPlayer()) {
            auto& s = smw::s;
            if (b == PlayerButton::Jump)  { s.jumpHeld = true; s.jumpQueued = m_isOnGround; }
            if (b == PlayerButton::Left)  s.left = true;
            if (b == PlayerButton::Right) s.right = true;
        }
        return PlayerObject::pushButton(b);
    }
    bool releaseButton(PlayerButton b) {
        if (isSMWPlayer()) {
            auto& s = smw::s;
            if (b == PlayerButton::Jump)  s.jumpHeld = false;
            if (b == PlayerButton::Left)  s.left = false;
            if (b == PlayerButton::Right) s.right = false;
        }
        return PlayerObject::releaseButton(b);
    }

    void update(float dt) {
        if (!isSMWPlayer() || dt <= 0.f) { PlayerObject::update(dt); return; }

        auto& s = smw::s;
        float x0 = this->getPositionX();
        float y0 = this->getPositionY();

        // Reinicio (muerte, nuevo intento, teletransporte)
        if (m_isDead || !s.init || std::fabs(x0 - s.expX) > 150.f || std::fabs(y0 - s.expY) > 300.f) {
            bool l = s.left, r = s.right, j = s.jumpHeld;
            s = smw::State{};
            s.init = true; s.left = l; s.right = r; s.jumpHeld = j;
            s.expX = x0; s.expY = y0;
        }

        // Si una colision nos empujo en contra de nuestro movimiento, frenamos
        float dx = x0 - s.expX, dy = y0 - s.expY;
        if (s.vx > 0.f && dx < -0.4f * s.vx * s.lastDt) s.vx = 0.f;
        if (s.vx < 0.f && dx >  0.4f * -s.vx * s.lastDt) s.vx = 0.f;
        if (s.airborne && s.vy > 0.f && dy < -0.4f * s.vy * s.lastDt) s.vy = 0.f; // golpe en el techo

        auto kb = CCDirector::get()->getKeyboardDispatcher();
        bool run  = kb->getShiftKeyPressed();
        bool spin = kb->getControlKeyPressed();
        bool spinPressed = spin && !s.prevSpin;
        s.prevSpin = spin;
        const bool cape = Mod::get()->getSettingValue<std::string>("powerup") == "Cape";

        // ---- Horizontal ----
        float maxV = run ? (s.pMeter >= 1.f ? smw::PSPEED : smw::RUN) : smw::WALK;
        float dir = (s.right ? 1.f : 0.f) - (s.left ? 1.f : 0.f);
        if (dir != 0.f) {
            bool skid = (s.vx * dir) < 0.f;
            s.vx += dir * (skid ? smw::SKID : smw::ACCEL) * dt;
            s.vx = std::clamp(s.vx, -maxV, maxV);
        } else {
            float d = smw::DECEL * dt;
            s.vx = (std::fabs(s.vx) <= d) ? 0.f : s.vx - std::copysign(d, s.vx);
        }
        if (!s.airborne && run && std::fabs(s.vx) >= smw::RUN * 0.98f)
            s.pMeter = std::min(1.f, s.pMeter + dt / 0.8f);
        else if (!s.airborne)
            s.pMeter = std::max(0.f, s.pMeter - dt * 2.f);

        // ---- Salto ----
        if (s.jumpQueued || (spinPressed && m_isOnGround && !s.airborne)) {
            s.airborne = true;
            s.spinning = !s.jumpQueued && spinPressed;
            s.vy = s.spinning ? smw::SPIN_JUMP
                              : smw::JUMP_BASE + smw::JUMP_BONUS * (std::fabs(s.vx) / smw::WALK);
        }
        s.jumpQueued = false;

        // ---- Suelo / aire ----
        bool rising = s.airborne && s.vy > 0.f;
        if (!rising) {
            if (m_isOnGround) { s.airborne = false; s.spinning = false; s.vy = 0.f; }
            else if (!s.airborne) { s.airborne = true; s.vy = std::min(0.0, m_yVelocity * 60.0); }
        }
        if (s.airborne) {
            bool held = s.jumpHeld || s.spinning;
            float g = (held && s.vy > 0.f) ? smw::GRAV_HELD : smw::GRAV_FREE;
            s.vy -= g * dt;
            if (cape && s.pMeter >= 1.f && s.jumpHeld && s.vy < 0.f) s.vy = std::max(s.vy, -smw::CAPE_FALL);
            s.vy = std::max(s.vy, -smw::FALL_MAX);
        }

        // ---- Dejar que GD haga su update y luego imponer NUESTRO movimiento ----
        PlayerObject::update(dt);

        this->setPositionX(x0 + s.vx * dt);
        if (s.airborne) {
            this->setPositionY(y0 + s.vy * dt);
            m_yVelocity = s.vy / 60.f;      // el signo le sirve a GD para detectar aterrizaje/techo
        }
        m_platformerXVelocity = s.vx / 311.58f;

        s.expX = this->getPositionX();
        s.expY = this->getPositionY();
        s.lastDt = dt;

        if (Mod::get()->getSettingValue<bool>("debug-log"))
            log::info("SMW vx={:.0f} vy={:.0f} P={:.2f} air={} ground={} pos=({:.0f},{:.0f})",
                s.vx, s.vy, s.pMeter, s.airborne, m_isOnGround, s.expX, s.expY);
    }
};
