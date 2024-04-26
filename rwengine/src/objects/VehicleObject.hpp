#ifndef _RWENGINE_VEHICLEOBJECT_HPP_
#define _RWENGINE_VEHICLEOBJECT_HPP_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#ifdef _MSC_VER
#pragma warning(disable : 4305)
#endif
#include <LinearMath/btScalar.h>
#ifdef _MSC_VER
#pragma warning(default : 4305)
#endif

#include <glm/gtc/type_precision.hpp>
#include <glm/vec3.hpp>

#include <data/ModelData.hpp>
#include <objects/GameObject.hpp>
#include <objects/VehicleInfo.hpp>


class Atomic;
class CharacterObject;
class CollisionInstance;
class GameWorld;
class ModelFrame;
class btRaycastVehicle;
class btCollisionShape;
class btRigidBody;
class btHingeConstraint;
class btMotionState;
struct btVehicleRaycaster;

/**
 * @class VehicleObject
 * Implements Vehicle behaviours.
 */
class VehicleObject final : public GameObject {
private:
    float steerAngle{0.f};
    float throttle{0.f};
    float brake{0.f};
    bool handbrake = true;
    bool immobilised = false;
    std::vector<btScalar> wheelsRotation;

    Atomic* chassishigh_ = nullptr;
    Atomic* chassislow_ = nullptr;

    std::array<Atomic*, 6> extras_{};

public:
    float health{1000.f};

    VehicleInfo* info = nullptr;
    glm::u8vec3 colourPrimary{};
    glm::u8vec3 colourSecondary{};
    bool mHasSpecial = true;

    std::map<size_t, GameObject*> seatOccupants;

    std::unique_ptr<CollisionInstance> collision;
    std::unique_ptr<btVehicleRaycaster> physRaycaster;
    std::unique_ptr<btRaycastVehicle> physVehicle;

    struct Part {
        Part(ModelFrame* p_dummy, Atomic* p_normal, Atomic* p_damaged,
             std::unique_ptr<btCollisionShape> p_cs,
             std::unique_ptr<btRigidBody> p_body,
             std::unique_ptr<btHingeConstraint> p_constraint,
             bool p_moveToAngle, float p_targetAngle, float p_openAngle,
             float p_closedAngle);

        Part(Part&& part) = default;
        Part& operator=(Part&& part) = default;

        ~Part() = default;

        ModelFrame* dummy;
        Atomic* normal;
        Atomic* damaged;
        std::unique_ptr<btCollisionShape> cs;
        std::unique_ptr<btRigidBody> body;
        std::unique_ptr<btMotionState> motionState;
        std::unique_ptr<btHingeConstraint> constraint;
        bool moveToAngle;
        float targetAngle;
        float openAngle;
        float closedAngle;
    };

    std::unordered_map<std::string, Part> dynamicParts;

    VehicleObject(GameWorld* engine, const glm::vec3& pos, const glm::quat& rot,
                  BaseModelInfo* modelinfo, VehicleInfo* info,
                  const glm::u8vec3& prim, const glm::u8vec3& sec);

    ~VehicleObject() override;

    void setPosition(const glm::vec3& pos) override;

    void setRotation(const glm::quat& orientation) override;

    glm::vec3 getCenterOffset() override;

    VehicleModelInfo* getVehicle() const {
        return getModelInfo<VehicleModelInfo>();
    }

    Atomic* getHighLOD() const {
        return chassishigh_;
    }
    Atomic* getLowLOD() const {
        return chassislow_;
    }

    Type type() const override {
        return Vehicle;
    }

    bool canBeRemoved() const override;

    bool isWrecked() const;

    void setHealth(float);

    float getHealth() const;

    void setExtraEnabled(size_t extra, bool enabled);

    void setSteeringAngle(float, bool=false);

    float getSteeringAngle() const;

    void setThrottle(float);

    float getThrottle() const;

    void setBraking(float);

    float getBraking() const;

    void setHandbraking(bool);

    bool getHandbraking() const;

    void setImmobilised(bool);

    bool getImmobilised() const;

    void tick(float dt) override;

    void tickPhysics(float dt);

    bool isFlipped() const;

    bool isUpright() const;

    float getVelocity() const;

    void ejectAll();

    GameObject* getOccupant(size_t seat) const;

    void setOccupant(size_t seat, GameObject* occupant);

    /**
     * @brief canOccupantExit
     * @return true if the vehicle is currently exitable
     */
    bool canOccupantExit() const;

    /**
     * @brief isOccupantDriver
     * @param seat
     * @return True if the given seat is the driver's seat.
     */
    bool isOccupantDriver(size_t seat) const;

    /**
     * @brief getDriver
     * @return CharacterObject* if there is a driver
     */
    CharacterObject* getDriver() const;

    glm::vec3 getSeatEntryPosition(size_t seat) const {
        auto pos = info->seats[seat].offset;
        pos -= glm::vec3(glm::sign(pos.x) * -0.81756252f, 0.34800607f,
                         -0.486281008f);
        return pos;
    }
    glm::vec3 getSeatEntryPositionWorld(size_t seat) const {
        return getPosition() + getRotation() * getSeatEntryPosition(seat);
    }

    Part* getSeatEntryDoor(size_t seat);

    bool takeDamage(const DamageInfo& damage) override;

    enum FrameState { OK, DAM, BROKEN };

    void setPartState(Part* part, FrameState state);

    void setPartLocked(Part* part, bool locked);

    void setPartTarget(Part* part, bool enable, float target);

    Part* getPart(const std::string& name);

    void applyWaterFloat(const glm::vec3& relPt);

    void setPrimaryColour(uint8_t color);
    void setSecondaryColour(uint8_t color);

    /**
     * @brief isStopped
     * @return True if the vehicle isn't moving
     */
    bool isStopped() const;

    /**
     * @brief collectSpecial
     * @return True if mHasSpecial was true opon calling
     */
    bool collectSpecial();

    /**
     * @brief isInFront
     * @return a positive distance when the point is in front of the car
     * and a negative distance when the point is behind the car
     */
    float isInFront(const glm::vec3& point);

    /**
     * @brief collectSpecial
     * @return a positive distance when the point is at the right side of the car
     * and a negative distance when the point is at the left side of the car
     */
    float isOnSide(const glm::vec3& point);


    void grantOccupantRewards(CharacterObject* character);

    /**
     * @return The position, and size of the area that must be free for the vehicle to continue.
     */
    std::tuple<glm::vec3, glm::vec3> obstacleCheckVolume() const;

private:
    void setupModel();
    void registerPart(ModelFrame* mf);
    void createObjectHinge(Part* part);
    void destroyObjectHinge(Part* part);
};

#endif
