#include "objects/VehicleObject.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#ifdef _MSC_VER
#pragma warning(disable : 4305)
#endif
#include <BulletDynamics/Vehicle/btRaycastVehicle.h>
#include <btBulletDynamicsCommon.h>
#ifdef _MSC_VER
#pragma warning(default : 4305)
#endif

#include <glm/gtx/quaternion.hpp>

#include <data/Clump.hpp>
#include <rw/types.hpp>

#include "dynamics/CollisionInstance.hpp"
#include "dynamics/RaycastCallbacks.hpp"
#include "engine/GameData.hpp"
#include "engine/GameState.hpp"
#include "engine/GameWorld.hpp"
#include "objects/CharacterObject.hpp"

#define PART_CLOSE_VELOCITY 0.25f
constexpr float kVehicleMaxExitVelocity = 0.15f;

/**
 * A raycaster that will ignore the body of the vehicle when casting rays
 */
class VehicleRaycaster final : public btVehicleRaycaster {
    btDynamicsWorld* _world;
    VehicleObject* _vehicle;

public:
    VehicleRaycaster(VehicleObject* vehicle, btDynamicsWorld* world)
        : _world(world), _vehicle(vehicle) {
    }

    void* castRay(const btVector3& from, const btVector3& to,
                  btVehicleRaycasterResult& result) override {
        ClosestNotMeRayResultCallback rayCallback(
            _vehicle->collision->getBulletBody(), from, to);
        void* res = nullptr;

        _world->rayTest(from, to, rayCallback);

        if (rayCallback.hasHit()) {
            btRigidBody* body = const_cast<btRigidBody*>(
                btRigidBody::upcast(rayCallback.m_collisionObject));

            if (body && body->hasContactResponse()) {
                result.m_hitPointInWorld = rayCallback.m_hitPointWorld;
                result.m_hitNormalInWorld = rayCallback.m_hitNormalWorld;
                result.m_hitNormalInWorld.normalize();
                result.m_distFraction = rayCallback.m_closestHitFraction;
                res = body;
            }
        }

        return reinterpret_cast<void*>(res);
    }
};

class VehiclePartMotionState final : public btMotionState {
public:
    VehiclePartMotionState(VehicleObject* object, VehicleObject::Part* part)
        : m_object(object), m_part(part) {
    }

    void getWorldTransform(btTransform& tform) const override {
        const auto& p = m_part->dummy->getDefaultTranslation();
        const auto& o = glm::toQuat(m_part->dummy->getDefaultRotation());
        tform.setOrigin(btVector3(p.x, p.y, p.z));
        tform.setRotation(btQuaternion(o.x, o.y, o.z, o.w));
        tform =
            m_object->collision->getBulletBody()->getWorldTransform() * tform;
    }

    void setWorldTransform(const btTransform& tform) override {
        auto inv = glm::inverse(m_object->getRotation());
        const auto& rot = tform.getRotation();
        auto r2 = inv * glm::quat(rot.w(), rot.x(), rot.y(), rot.z());

        m_part->dummy->setRotation(glm::mat3_cast(r2));
    }

private:
    VehicleObject* m_object;
    VehicleObject::Part* m_part;
};

VehicleObject::VehicleObject(GameWorld* engine, const glm::vec3& pos,
                             const glm::quat& rot, BaseModelInfo* modelinfo,
                             VehicleInfo* info, const glm::u8vec3& prim,
                             const glm::u8vec3& sec)
    : GameObject(engine, pos, rot, modelinfo)
    , info(info)
    , colourPrimary(prim)
    , colourSecondary(sec)
    , collision(new CollisionInstance) {
    collision->createPhysicsBody(this, modelinfo->getCollision(), nullptr,
                                 &info->handling);
    collision->getBulletBody()->forceActivationState(DISABLE_DEACTIVATION);
    physRaycaster =
        std::make_unique<VehicleRaycaster>(this, engine->dynamicsWorld.get());
    btRaycastVehicle::btVehicleTuning tuning;

    float travel = fabs(info->handling.suspensionUpperLimit -
                        info->handling.suspensionLowerLimit);
    float maxVelocity = info->handling.maxVelocity;
    float accelerationFloor = std::max(info->handling.acceleration, 30.f);
    float massFloor = std::min(info->handling.mass, 3000.f);
    // The friction slip represents the friction coefficient between each wheel
    // and the ground. The higher the coefficient, the more slippery the contact
    // gets between the wheel and the ground. Fast vehicles (with a significant
    // acceleration and max speed for a low mass) tend to need a high
    // coefficient (> 5.f) to allow properly turning when steering. On the other
    // hand, slow vehicles tend to need a lower coefficient or they will be too
    // reactive when turning. This (purely empirical) formula is an attempt to
    // apply this idea, with floors to avoid too much divergence.
    // For some reason, the calculation with handling info gives us a value in
    // the right range, with an offset to set a sane base.
    tuning.m_frictionSlip = 1.8f + maxVelocity * accelerationFloor / massFloor;
    tuning.m_maxSuspensionTravelCm = travel * 100.f;

    physVehicle = std::make_unique<btRaycastVehicle>(
        tuning, collision->getBulletBody(), physRaycaster.get());
    physVehicle->setCoordinateSystem(0, 2, 1);
    engine->dynamicsWorld->addAction(physVehicle.get());

    float kC = 0.5f;
    float kR = 0.6f;

    for (size_t w = 0; w < info->wheels.size(); ++w) {
        auto restLength = travel;
        auto heightOffset = info->handling.suspensionUpperLimit;
        btVector3 connection(info->wheels[w].position.x,
                             info->wheels[w].position.y,
                             info->wheels[w].position.z + heightOffset);
        bool front = connection.y() > 0;
        btWheelInfo& wi = physVehicle->addWheel(
            connection, btVector3(0.f, 0.f, -1.f), btVector3(1.f, 0.f, 0.f),
            restLength, getVehicle()->wheelscale_ / 2.f, tuning, front);
        wi.m_suspensionRestLength1 = restLength;
        wi.m_raycastInfo.m_suspensionLength = 0.f;

        wi.m_maxSuspensionForce = info->handling.mass * 9.f;
        wi.m_suspensionStiffness = (info->handling.suspensionForce * 50.f);

        // float dampEffect = (info->handling.suspensionDamping) / travel;
        // wi.m_wheelsDampingCompression = wi.m_wheelsDampingRelaxation =
        // dampEffect;

        wi.m_wheelsDampingCompression =
            kC * 2.f * btSqrt(wi.m_suspensionStiffness);
        wi.m_wheelsDampingRelaxation =
            kR * 2.f * btSqrt(wi.m_suspensionStiffness);
        // Roll influence prevents cars from easily flipping wheels.
        // Tune with care!
        wi.m_rollInfluence = 0.02f;

        float halfFriction = tuning.m_frictionSlip * 0.5f;
        wi.m_frictionSlip =
            halfFriction +
            halfFriction * (front ? info->handling.tractionBias
                                  : 1.f - info->handling.tractionBias);

        wheelsRotation.push_back(0.f);
    }

    setModel(getVehicle()->getModel()->clone());
    setupModel();
}

VehicleObject::~VehicleObject() {
    ejectAll();

    engine->dynamicsWorld->removeAction(physVehicle.get());

    for (auto& p : dynamicParts) {
        destroyObjectHinge(&p.second);
    }
}

void VehicleObject::setupModel() {
    const auto vehicleInfo = getModelInfo<VehicleModelInfo>();
    const auto isBoat = (vehicleInfo->vehicletype_ == VehicleModelInfo::BOAT);
    const std::string baseName = isBoat ? "boat" : "chassis";
    const auto dummy = getClump()->findFrame("chassis_dummy");

    for (const auto& atomic : getClump()->getAtomics()) {
        auto frame = atomic->getFrame().get();
        const auto& name = frame->getName();
        if (name == baseName + "_vlo") {
            chassislow_ = atomic.get();
        }
        if (name == baseName + "_hi") {
            chassishigh_ = atomic.get();
        }
        if (name.find("extra") == 0) {
            if (name.size() == 5) {
                continue;
            }

            auto partNumber = (std::stoul(name.c_str() + 5) - 1);
            extras_.at(partNumber) = atomic.get();
            setExtraEnabled(partNumber, false);
        }
    }

    if (!dummy) {
        return;
    }

    for (const auto& frame : dummy->getChildren()) {
        const auto& name = frame->getName();
        if (name.find("_dummy") != std::string::npos) {
            registerPart(frame.get());
        }
    }

    auto compRules = vehicleInfo->componentrules_;
    auto numComponents = [](int rule) {
        if ((rule & 0xFFF) == 0xFFF) return 0;
        if ((rule & 0xFF0) == 0xFF0) return 1;
        if ((rule & 0xF00) == 0xF00) return 2;
        return 3;
    };
    while (compRules != 0) {
        auto rule = (compRules & 0xFFFF);
        auto type = static_cast<ComponentRuleType>(rule >> 12);
        compRules >>= 16;

        long int result = -1;
        switch (type) {
            case ComponentRuleType::Any:
            case ComponentRuleType::RainOnly: {
                auto max = numComponents(rule) - 1;
                auto i = engine->getRandomNumber(0, max);
                result = (rule >> (4 * i)) & 0xF;
            } break;
            case ComponentRuleType::Optional: {
                auto max = numComponents(rule) - 1;
                auto i = engine->getRandomNumber(-1, max);
                if (i == -1) {
                    break;
                }
                result = (rule >> (4 * i)) & 0xF;
            } break;
            case ComponentRuleType::Random:
                /// @todo this should fail to enable the 6th component
                result = engine->getRandomNumber(0, 5);
                break;
        }

        if (type == ComponentRuleType::RainOnly && !engine->isRaining()) {
            continue;
        }

        if (result >= 0) {
            setExtraEnabled(size_t(result), true);
        }
    }
}

void VehicleObject::setPosition(const glm::vec3& pos) {
    GameObject::setPosition(pos);
    getClump()->getFrame()->setTranslation(pos);
    if (collision->getBulletBody()) {
        auto bodyOrigin = btVector3(position.x, position.y, position.z);
        for (auto& part : dynamicParts) {
            if (part.second.body == nullptr) continue;
            auto body = part.second.body.get();
            auto rel = body->getWorldTransform().getOrigin() - bodyOrigin;
            body->getWorldTransform().setOrigin(
                btVector3(pos.x + rel.x(), pos.y + rel.y(), pos.z + rel.z()));
        }

        auto t = collision->getBulletBody()->getWorldTransform();
        t.setOrigin(btVector3(pos.x, pos.y, pos.z));
        collision->getBulletBody()->setWorldTransform(t);
    }
}

glm::vec3 VehicleObject::getCenterOffset() {
    // Calculate the offset from the center to the base of the vehicle
    btVector3 aabbMin;
    btVector3 aabbMax;

    collision->getBulletBody()->getAabb(aabbMin, aabbMax);
    float z_offset = (aabbMax.z() - aabbMin.z()) / 2;

    return glm::vec3(0.f, 0.f, z_offset);
}

void VehicleObject::setRotation(const glm::quat& orientation) {
    getClump()->getFrame()->setRotation(glm::mat3_cast(orientation));
    if (collision->getBulletBody()) {
        auto t = collision->getBulletBody()->getWorldTransform();
        t.setRotation(btQuaternion(orientation.x, orientation.y, orientation.z,
                                   orientation.w));
        collision->getBulletBody()->setWorldTransform(t);
    }
    GameObject::setRotation(orientation);
}

void VehicleObject::setExtraEnabled(size_t extra, bool enabled) {
    auto atomic = extras_.at(extra);
    if (!atomic) {
        return;
    }

    atomic->setFlag(Atomic::ATOMIC_RENDER, enabled);
}

void VehicleObject::tick(float dt) {
    RW_UNUSED(dt);
    // Moved to tickPhysics
}

void VehicleObject::tickPhysics(float dt) {
    RW_UNUSED(dt);

    static constexpr float steeringWeight = 1.f/0.35f;

    if (physVehicle) {
        // todo: a real engine function
        float velFac = info->handling.maxVelocity;
        float velocity = collision->getBulletBody()->getLinearVelocity().length();
        float velocityForward = physVehicle->getCurrentSpeedKmHour() / 3.6f;
        float velocityMax = velFac / 9.f;
        float steerValue = 0.f;
        float steerLimit = glm::radians(info->handling.steeringLock);
        // The engine force is calculated from the acceleration and max velocity
        // of the vehicle, with a specific coefficient to make it adapted to
        // Bullet physics and avoid reaching top speed too fast.
        float engineForce = info->handling.acceleration * throttle * velFac / 1.2f;
        float brakeF = getBraking();
        // Mass coefficient, that quantifies how heavy a vehicle is and excludes
        // light vehicles.
        float kM = (std::max(1500.f, info->handling.mass) - 1500.f) / 1500.f;
        unsigned int count = 0;

        // Get the global vehicle steering value (for front wheels only).
        for (int w = 0; w < physVehicle->getNumWheels(); ++w) {
            btWheelInfo& wi = physVehicle->getWheelInfo(w);

            if (wi.m_bIsFrontWheel) {
                steerValue += physVehicle->getSteeringValue(w);
                count++;
            }
        }

        steerValue /= count;

        // Increase the engine force based on the mass by up to 4 times.
        // Heavy vehicles need extra engine force to be reactive enough.
        engineForce *= std::min(1.f + kM, 4.f);

        // Give vehicles a boost when starting (forward or backward) to reduce
        // general sluggishness. The engine force is multiplied by 1.5 before
        // reaching velocityMax / 4 and then reduces down to nominal force as
        // velocity reaches velocityMax / 2.
        if (velocity < velocityMax / 4.f)
            engineForce *= 1.5f;
        else if (velocity < velocityMax / 2.f)
            engineForce *= 1.f + 0.5f * (2.f - velocity / (velocityMax / 4.f));

        // Reduce the engine force when steering, by a factor of up to 1.25 for
        // the maximum steering angle, linearly back to nominal value when no
        // steering is applied.
        if (std::abs(steerValue) > steerLimit / 8.f && velocity > velocityMax / 3.f)
            engineForce /= 1.f + std::abs(steerValue) / (4.f * steerLimit);

        if (velocity > velocityMax) {
            btVector3 v = collision->getBulletBody()->getLinearVelocity().normalized();

            velocity = velocityMax;
            v *= velocity;

            collision->getBulletBody()->setLinearVelocity(v);
        }

        if (handbrake) {
            brakeF = 5.f;
        } else if (throttle < 0.f && velocityForward > velocityMax / 8.f) {
            engineForce = 0.f;
            brakeF = 2.f * std::min(1.f + kM, 4.f);
        }

        if (isStopped() && std::abs(throttle) < 0.1f) {
            btVector3 v = collision->getBulletBody()->getLinearVelocity();
            v.setX(0.f);
            v.setY(0.f);

            collision->getBulletBody()->setLinearVelocity(v);

            for (int w = 0; w < physVehicle->getNumWheels(); ++w) {
                btWheelInfo& wi = physVehicle->getWheelInfo(w);
                wi.m_rotation = wheelsRotation[w];
            }
        }

        if (immobilised) {
            // An immobilised car should not be able to move on it's own.
            engineForce = 0.f;
            brakeF = 5.f;
        }

        for (int w = 0; w < physVehicle->getNumWheels(); ++w) {
            btWheelInfo& wi = physVehicle->getWheelInfo(w);

            wheelsRotation[w] = wi.m_rotation;

            if (info->handling.driveType == VehicleHandlingInfo::All ||
                (info->handling.driveType == VehicleHandlingInfo::Forward &&
                 wi.m_bIsFrontWheel) ||
                (info->handling.driveType == VehicleHandlingInfo::Rear &&
                 !wi.m_bIsFrontWheel)) {
                physVehicle->applyEngineForce(engineForce, w);
            }

            float brakeReal =
                8.f * info->handling.brakeDeceleration *
                (wi.m_bIsFrontWheel ? info->handling.brakeBias
                                    : 1.f - info->handling.brakeBias);
            physVehicle->setBrake(brakeReal * brakeF, w);

            if (wi.m_bIsFrontWheel) {
                float currentVal = physVehicle->getSteeringValue(w);
                float currentSign = std::copysign(1.0f, currentVal);
                float newVal;

                if (std::abs(steerAngle) < 0.001f) {   // no steering?
                    newVal = std::max(0.0f,std::abs(currentVal) -
                                      steeringWeight * dt) * currentSign;
                } else {
                    newVal = currentVal + steerAngle * dt * steeringWeight;

                    float limit = glm::radians(info->handling.steeringLock);

                    if (std::abs(newVal) > limit) {
                        newVal = limit * currentSign;
                    }
                }

                physVehicle->setSteeringValue(newVal,w);
            }
        }

        // Update passenger positions
        for (auto& [seatId, objectPtr] : seatOccupants) {
            auto character = static_cast<CharacterObject*>(objectPtr);

            glm::vec3 passPosition{};
            if (character->isEnteringOrExitingVehicle()) {
                passPosition = getSeatEntryPositionWorld(seatId);
            } else {
                passPosition = getPosition();
                if (seatId < info->seats.size()) {
                    passPosition +=
                        getRotation() * (info->seats[seatId].offset);
                }
            }
            objectPtr->updateTransform(passPosition, getRotation());
        }

        if (getVehicle()->vehicletype_ == VehicleModelInfo::BOAT) {
            if (isInWater()) {
                float sign = std::copysign(1.0f, steerAngle);
                float steer =
                    std::min(glm::radians(info->handling.steeringLock),
                             std::abs(steerAngle)) *
                    sign;
                auto orient = collision->getBulletBody()->getOrientation();

                // Find the local-space velocity
                auto velocity = collision->getBulletBody()->getLinearVelocity();
                velocity =
                    velocity.rotate(-orient.getAxis(), orient.getAngle());

                // Rudder force is proportional to velocity.
                float rAngle = steer * (velFac * 0.5f + 0.5f);
                btVector3 rForce =
                    btVector3(1000.f * velocity.y() * rAngle, 0.f, 0.f)
                        .rotate(orient.getAxis(), orient.getAngle());
                btVector3 rudderPoint =
                    btVector3(0.f, -info->handling.dimensions.y / 2.f, 0.f)
                        .rotate(orient.getAxis(), orient.getAngle());
                collision->getBulletBody()->applyForce(rForce, rudderPoint);

                btVector3 rudderVector =
                    btVector3(0.f, 1.f, 0.f)
                        .rotate(orient.getAxis(), orient.getAngle());
                collision->getBulletBody()->applyForce(
                    rudderVector * engineForce * 100.f, rudderPoint);

                btVector3 dampforce(10000.f * velocity.x(),
                                    velocity.y() * 100.f, 0.f);
                collision->getBulletBody()->applyCentralForce(
                    -dampforce.rotate(orient.getAxis(), orient.getAngle()));
            }
        }

        const auto& ws = getPosition();
        auto wX = static_cast<int>((ws.x + WATER_WORLD_SIZE / 2.f) /
                        (WATER_WORLD_SIZE / WATER_HQ_DATA_SIZE));
        auto wY = static_cast<int>((ws.y + WATER_WORLD_SIZE / 2.f) /
                        (WATER_WORLD_SIZE / WATER_HQ_DATA_SIZE));
        btVector3 bbmin, bbmax;
        // This is in world space.
        collision->getBulletBody()->getAabb(bbmin, bbmax);
        float vH = bbmin.z();
        float wH = 0.f;

        if (wX >= 0 && wX < WATER_HQ_DATA_SIZE && wY >= 0 &&
            wY < WATER_HQ_DATA_SIZE) {
            int i = (wX * WATER_HQ_DATA_SIZE) + wY;
            int hI = engine->data->realWater[i];
            if (hI < NO_WATER_INDEX) {
                wH = engine->data->waterHeights[hI];
                wH += engine->data->getWaveHeightAt(ws);
                // If the vehicle is currently underwater
                if (vH <= wH) {
                    // and was not underwater here in the last tick
                    if (_lastHeight >= wH) {
                        // we are for real, underwater
                        inWater = true;
                    }
                } else {
                    // The water is beneath us
                    inWater = false;
                }
            } else {
                inWater = false;
            }
        }

        auto isBoat = getVehicle()->vehicletype_ == VehicleModelInfo::BOAT;
        if (inWater) {
            // Ensure that vehicles don't fall asleep at the top of a wave.
            if (!collision->getBulletBody()->isActive()) {
                collision->getBulletBody()->activate(true);
            }

            float bbZ = info->handling.dimensions.z / 2.f;

            float oZ =
                -bbZ / 2.f + (bbZ * (info->handling.percentSubmerged / 120.f));

            if (isBoat) {
                oZ = 0.f;
            } else {
                // Damper motion
                collision->getBulletBody()->setDamping(0.95f, 0.9f);
            }

            // Boats, Buoyancy offset is affected by the orientation of the
            // chassis.
            // Vehicles, it isn't.
            glm::vec3 vFwd =
                          glm::vec3(0.f, info->handling.dimensions.y / 2.f, oZ),
                      vBack = glm::vec3(0.f, -info->handling.dimensions.y / 2.f,
                                        oZ);
            glm::vec3 vRt =
                          glm::vec3(info->handling.dimensions.x / 2.f, 0.f, oZ),
                      vLeft = glm::vec3(-info->handling.dimensions.x / 2.f, 0.f,
                                        oZ);

            vFwd = getRotation() * vFwd;
            vBack = getRotation() * vBack;
            vRt = getRotation() * vRt;
            vLeft = getRotation() * vLeft;

            // This function will try to keep v* at the water level.
            applyWaterFloat(vFwd);
            applyWaterFloat(vBack);
            applyWaterFloat(vRt);
            applyWaterFloat(vLeft);
        } else {
            if (isBoat) {
                collision->getBulletBody()->setDamping(0.1f, 0.8f);
            } else {
                collision->getBulletBody()->setDamping(0.05f, 0.0f);
            }
        }

        _lastHeight = vH;

        // Update hinge object rotations
        for (auto& it : dynamicParts) {
            if (it.second.body == nullptr) continue;
            if (it.second.moveToAngle) {
                auto angledelta = it.second.targetAngle -
                                  it.second.constraint->getHingeAngle();
                if (glm::abs(angledelta) <= 0.01f) {
                    it.second.constraint->enableAngularMotor(false, 1.f, 1.f);
                    it.second.moveToAngle = false;
                } else {
                    it.second.constraint->enableAngularMotor(
                        true, glm::sign(angledelta) * 5.f, 1.f);
                }
            }

            // If the part is moving quite fast and near the limit, lock it.
            /// @TODO not all parts rotate in the z axis.
            float zspeed = it.second.body->getAngularVelocity().getZ();
            if (it.second.openAngle < 0.f) zspeed = -zspeed;
            if (zspeed >= PART_CLOSE_VELOCITY) {
                auto d = it.second.constraint->getHingeAngle() -
                         it.second.closedAngle;
                if (glm::abs(d) < 0.05f) {
                    it.second.moveToAngle = false;
                    setPartLocked(&(it.second), true);
                }
            }
        }
    }
}

bool VehicleObject::isFlipped() const {
    auto forward = getRotation() * glm::vec3(0.f, 0.f, 1.f);
    return forward.z <= -0.97f;
}

bool VehicleObject::isUpright() const {
    auto forward = getRotation() * glm::vec3(0.f, 0.f, 1.f);
    return forward.z >= 0.f;
}

float VehicleObject::getVelocity() const {
    if (physVehicle) {
        return (physVehicle->getCurrentSpeedKmHour() * 1000.f) / (60.f * 60.f);
    }
    return 0.f;
}

bool VehicleObject::canBeRemoved() const {
    return GameObject::canBeRemoved() &&
           all_of(seatOccupants.begin(), seatOccupants.end(),
                  [](const auto& p) { return p.second->canBeRemoved(); });
}

bool VehicleObject::isWrecked() const {
    return health < 250.f;
}

void VehicleObject::setHealth(float h) {
    health = h;
}

float VehicleObject::getHealth() const {
    return health;
}

void VehicleObject::setSteeringAngle(float a, bool force) {
    steerAngle = a;

    if (force && physVehicle) {
        for (int w = 0; w < physVehicle->getNumWheels(); ++w) {
            btWheelInfo& wi = physVehicle->getWheelInfo(w);
            
            if (wi.m_bIsFrontWheel) {
                physVehicle->setSteeringValue(a, w);
            }
        }
    }
}

float VehicleObject::getSteeringAngle() const {
    return steerAngle;
}

void VehicleObject::setThrottle(float t) {
    throttle = t;
}

float VehicleObject::getThrottle() const {
    return throttle;
}

void VehicleObject::setBraking(float b) {
    brake = b;
}

float VehicleObject::getBraking() const {
    return brake;
}

void VehicleObject::setHandbraking(bool hb) {
    handbrake = hb;
}

bool VehicleObject::getHandbraking() const {
    return handbrake;
}

void VehicleObject::setImmobilised(bool immobile) {
    immobilised = immobile;
}

bool VehicleObject::getImmobilised() const {
    return immobilised;
}

void VehicleObject::ejectAll() {
    for (std::map<size_t, GameObject*>::iterator it = seatOccupants.begin();
         it != seatOccupants.end();) {
        if (it->second->type() == GameObject::Character) {
            CharacterObject* c = static_cast<CharacterObject*>(it->second);
            c->setCurrentVehicle(nullptr, 0);
            c->setPosition(getPosition());
        }
        it = seatOccupants.erase(it);
    }
}

GameObject* VehicleObject::getOccupant(size_t seat) const {
    auto it = seatOccupants.find(seat);
    if (it != seatOccupants.end()) {
        return it->second;
    }
    return nullptr;
}

void VehicleObject::setOccupant(size_t seat, GameObject* occupant) {
    auto it = seatOccupants.find(seat);
    if (occupant == nullptr) {
        if (it != seatOccupants.end()) {
            seatOccupants.erase(it);
        }
    } else {
        seatOccupants[seat] = occupant;
    }
}

bool VehicleObject::canOccupantExit() const {
    return getVelocity() <= kVehicleMaxExitVelocity;
}

bool VehicleObject::isOccupantDriver(size_t seat) const {
    // This isn't true for all vehicles, but it'll do until we figure it out
    return seat == 0;
}

CharacterObject* VehicleObject::getDriver() const {
    return static_cast<CharacterObject*>(getOccupant(0));
}

VehicleObject::Part* VehicleObject::getSeatEntryDoor(size_t seat) {
    auto pos = info->seats[seat].offset + glm::vec3(0.f, 0.5f, 0.f);
    Part* nearestDoor = nullptr;
    float d = std::numeric_limits<float>::max();
    for (auto& p : dynamicParts) {
        float partDist =
            glm::distance(p.second.dummy->getDefaultTranslation(), pos);
        if (partDist < d && p.second.dummy->getName().substr(0, 5) == "door_") {
            d = partDist;
            nearestDoor = &p.second;
        }
    }
    return nearestDoor;
}

bool VehicleObject::takeDamage(const GameObject::DamageInfo& dmg) {
    RW_CHECK(dmg.hitpoints == 0, "Vehicle Damage not implemented yet");

    const float frameDamageThreshold = 1500.f;

    if (dmg.impulse >= frameDamageThreshold) {
        auto dpoint = dmg.damageLocation;
        dpoint -= getPosition();
        dpoint = glm::inverse(getRotation()) * dpoint;

        // Set any parts within range to damaged state.
        for (auto& d : dynamicParts) {
            auto p = &d.second;

            if (p->normal == nullptr) continue;

            /// @todo correct logic
            float damageradius = 0.1f;
            auto center = glm::vec3(p->dummy->getWorldTransform()[3]);
            float td = glm::distance(center, dpoint);
            if (td < damageradius * 1.2f) {
                setPartState(p, DAM);
            }
            /// @todo determine when doors etc. should un-latch
        }
    }

    health -= dmg.hitpoints;

    return true;
}

void VehicleObject::setPartState(VehicleObject::Part* part,
                                 VehicleObject::FrameState state) {
    if (state == VehicleObject::OK) {
        if (part->normal) part->normal->setFlag(Atomic::ATOMIC_RENDER, true);
        if (part->damaged) part->damaged->setFlag(Atomic::ATOMIC_RENDER, false);
    } else if (state == VehicleObject::DAM) {
        if (part->normal) part->normal->setFlag(Atomic::ATOMIC_RENDER, false);
        if (part->damaged) part->damaged->setFlag(Atomic::ATOMIC_RENDER, true);
    }
}

void VehicleObject::applyWaterFloat(const glm::vec3& relPt) {
    auto ws = getPosition() + relPt;
    auto wi = engine->data->getWaterIndexAt(ws);
    if (wi != NO_WATER_INDEX) {
        float h = engine->data->waterHeights[wi];

        // Calculate wave height
        h += engine->data->getWaveHeightAt(ws);

        if (ws.z <= h) {
            float x = (h - ws.z);
            float F = WATER_BUOYANCY_K * x +
                      -WATER_BUOYANCY_C *
                          collision->getBulletBody()->getLinearVelocity().z();
            collision->getBulletBody()->applyImpulse(
                btVector3(0.f, 0.f, F), btVector3(relPt.x, relPt.y, relPt.z));
        }
    }
}

void VehicleObject::setPartLocked(VehicleObject::Part* part, bool locked) {
    if (part->body == nullptr && !locked) {
        createObjectHinge(part);
    } else if (part->body != nullptr && locked) {
        destroyObjectHinge(part);

        // Restore default bone transform
        part->dummy->reset();
    }
}

void VehicleObject::setPartTarget(VehicleObject::Part* part, bool enable,
                                  float target) {
    if (enable) {
        if (part->body == nullptr) {
            setPartLocked(part, false);
        } else {
            part->body->activate(true);
        }
        part->targetAngle = target;
        part->moveToAngle = true;
    } else {
        part->targetAngle = target;
        part->moveToAngle = false;
    }
}

VehicleObject::Part* VehicleObject::getPart(const std::string& name) {
    auto f = dynamicParts.find(name);
    if (f != dynamicParts.end()) {
        return &f->second;
    }
    return nullptr;
}

void VehicleObject::registerPart(ModelFrame* mf) {
    auto dummynameend = mf->getName().find("_dummy");
    RW_CHECK(dummynameend != std::string::npos,
             "Can't create part from non-dummy");
    auto dummyname = mf->getName().substr(0, dummynameend);
    auto normalframe = mf->findDescendant(dummyname + "_hi_ok");
    auto damageframe = mf->findDescendant(dummyname + "_hi_dam");

    if (normalframe == nullptr && damageframe == nullptr) {
        // Not actually a useful part, just a dummy.
        return;
    }

    // Find the Atomics for the part
    Atomic *normal = nullptr, *damage = nullptr;
    for (const auto& atomic : getClump()->getAtomics()) {
        if (atomic->getFrame().get() == normalframe) {
            normal = atomic.get();
        }
        if (atomic->getFrame().get() == damageframe) {
            damage = atomic.get();
            damage->setFlag(Atomic::ATOMIC_RENDER, false);
        }
    }
    Part part{mf, normal, damage, nullptr, nullptr, nullptr, false,
     0.f, 0.f, 0.f};

    dynamicParts.emplace(mf->getName(), std::move(part));
}

void VehicleObject::createObjectHinge(Part* part) {
    float sign = glm::sign(part->dummy->getDefaultTranslation().x);
    btVector3 hingeAxis, hingePosition;
    btVector3 boxSize, boxOffset;
    float hingeMax = 1.f;
    float hingeMin = 0.f;

    auto& fn = part->dummy->getName();

    if (fn.find("door") != fn.npos) {
        hingeAxis = {0.f, 0.f, 1.f};
        // hingePosition = {0.f, 0.2f, 0.f};
        boxSize = {0.15f, 0.5f, 0.6f};
        // boxOffset = {0.f,-0.2f, gbounds.center.z/2.f};
        boxOffset = btVector3(0.f, -0.25f, 0.f);
        hingePosition = -boxOffset;

        if (sign < 0.f) {
            hingeMax = glm::quarter_pi<float>() * 1.5f;
            hingeMin = 0.f;
            part->openAngle = hingeMax;
            part->closedAngle = hingeMin;
        } else {
            hingeMin = glm::quarter_pi<float>() * -1.5f;
            hingeMax = 0.f;
            part->openAngle = hingeMin;
            part->closedAngle = hingeMax;
        }
    } else if (fn.find("bonnet") != fn.npos) {
        hingeAxis = {1.f, 0.f, 0.f};
        hingePosition = {0.f, -0.2f, 0.f};
        hingeMax = 0.f;
        hingeMin = -glm::quarter_pi<float>() * 1.5f;
        boxSize = {0.4f, 0.4f, 0.1f};
        boxOffset = {0.f, 0.2f, 0.f};
    } else {
        // TODO: boot, bumper
        return;
    }

    auto ms = std::make_unique<VehiclePartMotionState>(this, part);

    btTransform tr = btTransform::getIdentity();
    const auto& p = part->dummy->getDefaultTranslation();
    const auto& o = glm::toQuat(part->dummy->getDefaultRotation());
    tr.setOrigin(btVector3(p.x, p.y, p.z));
    tr.setRotation(btQuaternion(o.x, o.y, o.z, o.w));

    auto cs = std::make_unique<btBoxShape>(boxSize);
    btTransform t;
    t.setIdentity();
    t.setOrigin(boxOffset);

    btVector3 inertia;
    cs->calculateLocalInertia(10.f, inertia);

    btRigidBody::btRigidBodyConstructionInfo rginfo(10.f, ms.get(), cs.get(), inertia);
    auto subObject = std::make_unique<btRigidBody>(rginfo);
    subObject->setUserPointer(this);

    auto hinge = std::make_unique<btHingeConstraint>(*collision->getBulletBody(), *subObject,
                                       tr.getOrigin(), hingePosition, hingeAxis,
                                       hingeAxis);
    hinge->setLimit(hingeMin, hingeMax);
    hinge->setBreakingImpulseThreshold(250.f);

    part->cs = std::move(cs);
    part->body = std::move(subObject);
    part->motionState = std::move(ms);
    part->constraint = std::move(hinge);

    engine->dynamicsWorld->addRigidBody(part->body.get());
    engine->dynamicsWorld->addConstraint(part->constraint.get(), true);
}

void VehicleObject::destroyObjectHinge(Part* part) {
    if (part->constraint != nullptr) {
        engine->dynamicsWorld->removeConstraint(part->constraint.get());
        part->constraint = nullptr;
    }

    if (part->body != nullptr) {
        engine->dynamicsWorld->removeCollisionObject(part->body.get());
        part->body = nullptr;
        part->motionState = nullptr;
        part->cs = nullptr;
    }

    // Reset target.
    part->moveToAngle = false;
}

void VehicleObject::setPrimaryColour(uint8_t color) {
    colourPrimary = engine->data->vehicleColours[color];
}

void VehicleObject::setSecondaryColour(uint8_t color) {
    colourSecondary = engine->data->vehicleColours[color];
}

bool VehicleObject::isStopped() const {
    return fabsf(physVehicle->getCurrentSpeedKmHour()) < 0.75f;
}

bool VehicleObject::collectSpecial() {
    bool hadSpecial = mHasSpecial;

    mHasSpecial = false;

    return hadSpecial;
}

void VehicleObject::grantOccupantRewards(CharacterObject* character) {
    if (character->isPlayer() && collectSpecial()) {
        if (getVehicle()->vehiclename_ == "TAXI" ||
            getVehicle()->vehiclename_ == "CABBIE" ||
            getVehicle()->vehiclename_ == "BORGNINE") {
            // Earn $25 from taxi cabs
            engine->state->playerInfo.money += 25;
        } else if (getVehicle()->vehiclename_ == "POLICAR") {
            // Police cruisers give 5 shotgun cartridges
            character->addToInventory(4, 5);
        } else if (getVehicle()->vehiclename_ == "ENFORCR") {
            // Obtain 100 armour if it's an Enforcer
            character->getCurrentState().armour = 100.f;
        } else if (getVehicle()->vehiclename_ == "AMBULAN") {
            // Receive up to 20 HPs from ambulances
            float health = character->getCurrentState().health;
            float fullHealth = 100.f;
            if (health < fullHealth) {
                character->getCurrentState().health =
                    (health >= 80.f) ? fullHealth : health + 20.f;
            }
        }
    }
}

float VehicleObject::isInFront(const glm::vec3& point) {
    // The point we need to test
    glm::vec3 testPoint;

    testPoint = point - getPosition();

    // The two endpoints of the line
    glm::vec3 v1;
    glm::vec3 v2;

    static const glm::vec3 up = glm::vec3(0.f, 0.f, 1.f);
    const glm::vec3 dir =
        glm::normalize(getRotation() * glm::vec3(0.f, 1.f, 0.f));

    // Calculate the strafe vector
    glm::vec3 strafe = glm::cross(up, dir);

    v1 = strafe;
    v2 = -strafe;
	
    // Check if the point is behind or in front the car

    glm::vec3 normal(v1.y - v2.y, 0, v2.x - v1.x);
    normal = glm::normalize(normal);

    const glm::vec3 vecTemp(testPoint.x - v1.x, 0, testPoint.y - v1.y);
    float distance = glm::dot(vecTemp, normal);

    return distance;
}

float VehicleObject::isOnSide(const glm::vec3& point) {
    // The point we need to test
    glm::vec3 testPoint;

    testPoint = point - getPosition();

    // The two endpoints of the line
    glm::vec3 v1;
    glm::vec3 v2;

    static const glm::vec3 up = glm::vec3(0.f, 0.f, 1.f);
    const glm::vec3 dir =
        glm::normalize(getRotation() * glm::vec3(1.f, 0.f, 0.f));

    // Calculate the strafe vector
    glm::vec3 strafe = glm::cross(up, dir);

    v1 = strafe;
    v2 = -strafe;

    // Check if the point is behind or in front the car

    glm::vec3 normal(v1.y - v2.y, 0, v2.x - v1.x);
    normal = glm::normalize(normal);

    const glm::vec3 vecTemp(testPoint.x - v1.x, 0, testPoint.y - v1.y);
    float distance = glm::dot(vecTemp, normal);

    return distance;
}

std::tuple<glm::vec3, glm::vec3> VehicleObject::obstacleCheckVolume() const {
    const auto& dim = info->handling.dimensions;
    const auto kMaxDistance = 20.f;
    const auto velocity = getVelocity() / info->handling.maxVelocity;
    const auto lookDistance = glm::clamp(kMaxDistance * velocity, 0.f, kMaxDistance);
    const glm::vec3 areaSize{dim.x * 0.6f, 1.0f + lookDistance, 1.0f};
    return {
            {0.f, dim.y * 0.5f + areaSize.y, 0.f},
            areaSize,
    };
}

VehicleObject::Part::Part(ModelFrame* p_dummy, Atomic* p_normal,
                          Atomic* p_damaged,
                          std::unique_ptr<btCollisionShape> p_cs,
                          std::unique_ptr<btRigidBody> p_body,
                          std::unique_ptr<btHingeConstraint> p_constraint,
                          bool p_moveToAngle, float p_targetAngle,
                          float p_openAngle, float p_closedAngle)
    : dummy(p_dummy)
    , normal(p_normal)
    , damaged(p_damaged)
    , cs(std::move(p_cs))
    , body(std::move(p_body))
    , constraint(std::move(p_constraint))
    , moveToAngle(p_moveToAngle)
    , targetAngle(p_targetAngle)
    , openAngle(p_openAngle)
    , closedAngle(p_closedAngle) {
}