#ifndef RWGAME_RWGAME_HPP
#define RWGAME_RWGAME_HPP

#include <chrono>
#include <engine/GameData.hpp>
#include <engine/GameState.hpp>
#include <engine/GameWorld.hpp>
#include <render/DebugDraw.hpp>
#include <render/GameRenderer.hpp>
#include <script/ScriptMachine.hpp>
#include <script/modules/GTA3Module.hpp>
#include "game.hpp"

#include "GameBase.hpp"

class PlayerController;

class RWGame : public GameBase {
    WorkContext work;
    GameData data;
    GameRenderer renderer;
    DebugDraw debug;
    GameState state;

    std::unique_ptr<GameWorld> world;

    GTA3Module opcodes;
    std::unique_ptr<ScriptMachine> vm;
    std::unique_ptr<SCMFile> script;

    std::chrono::steady_clock clock;
    std::chrono::steady_clock::time_point last_clock_time;

    bool inFocus = true;
    ViewCamera lastCam, nextCam;

    enum class DebugViewMode {
        Disabled,
        General,
        Physics,
        Navigation,
        Objects
    };

    DebugViewMode debugview_ = DebugViewMode::Disabled;
    int lastDraws;  /// Number of draws issued for the last frame.

    std::string cheatInputWindow = std::string(32, ' ');

    float accum = 0.f;
    float timescale = 1.f;

public:
    RWGame(Logger& log, int argc, char* argv[]);
    ~RWGame();

    int run();

    /**
     * Initalizes a new game
     */
    void newGame();

    GameState* getState() {
        return &state;
    }

    GameWorld* getWorld() {
        return world.get();
    }

    const GameData& getGameData() const {
        return data;
    }

    GameRenderer& getRenderer() {
        return renderer;
    }

    ScriptMachine *getScriptVM() const {
        return vm.get();
    }

    bool hitWorldRay(glm::vec3& hit, glm::vec3& normal,
                     GameObject** object = nullptr) {
        auto vc = nextCam;
        glm::vec3 from(vc.position.x, vc.position.y, vc.position.z);
        glm::vec3 tmp = vc.rotation * glm::vec3(1000.f, 0.f, 0.f);

        return hitWorldRay(from, tmp, hit, normal, object);
    }

    bool hitWorldRay(const glm::vec3& start, const glm::vec3& direction,
                     glm::vec3& hit, glm::vec3& normal,
                     GameObject** object = nullptr) {
        auto from = btVector3(start.x, start.y, start.z);
        auto to = btVector3(start.x + direction.x, start.y + direction.y,
                            start.z + direction.z);
        btCollisionWorld::ClosestRayResultCallback ray(from, to);

        world->dynamicsWorld->rayTest(from, to, ray);
        if (ray.hasHit()) {
            hit = glm::vec3(ray.m_hitPointWorld.x(), ray.m_hitPointWorld.y(),
                            ray.m_hitPointWorld.z());
            normal =
                glm::vec3(ray.m_hitNormalWorld.x(), ray.m_hitNormalWorld.y(),
                          ray.m_hitNormalWorld.z());
            if (object) {
                *object = static_cast<GameObject*>(
                    ray.m_collisionObject->getUserPointer());
            }
            return true;
        }
        return false;
    }

    void startScript(const std::string& name);

    bool hasFocus() const {
        return inFocus;
    }

    void saveGame(const std::string& savename);
    void loadGame(const std::string& savename);

    /** shortcut for getWorld()->state.player->getCharacter() */
    PlayerController* getPlayer();

private:
    void tick(float dt);
    void render(float alpha, float dt);

    void renderDebugStats(float time);
    void renderDebugPaths(float time);
    void renderDebugObjects(float time, ViewCamera& camera);
    void renderProfile();

    void handleCheatInput(char symbol);

    void globalKeyEvent(const SDL_Event& event);
};

#endif
