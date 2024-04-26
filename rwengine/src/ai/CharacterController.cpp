#include "ai/CharacterController.hpp"

#include <cmath>
#include <limits>
#include <utility>

#ifdef _MSC_VER
#pragma warning(disable : 4305)
#endif
#include <BulletDynamics/Character/btKinematicCharacterController.h>
#include <btBulletDynamicsCommon.h>
#ifdef _MSC_VER
#pragma warning(default : 4305)
#endif

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <LinearMath/btScalar.h>

#include <rw/debug.hpp>
#include <dynamics/HitTest.hpp>

#include "ai/CharacterController.hpp"
#include "ai/AIGraphNode.hpp"
#include "data/WeaponData.hpp"
#include "engine/Animator.hpp"
#include "engine/GameData.hpp"
#include "engine/GameWorld.hpp"
#include "items/Weapon.hpp"
#include "objects/CharacterObject.hpp"
#include "objects/VehicleObject.hpp"

constexpr float kCloseDoorIdleTime = 2.f;

namespace ai {

bool CharacterController::updateActivity() {
    if (_currentActivity && character->isAlive()) {
        return _currentActivity->update(character, this);
    }

    return false;
}

void CharacterController::setActivity(std::unique_ptr<Activity> activity) {
    _currentActivity = std::move(activity);
}

void CharacterController::skipActivity() {
    // Some activities can't be cancelled, such as the final phase of entering a
    // vehicle
    // or jumping.
    if (getCurrentActivity() != nullptr &&
        getCurrentActivity()->canSkip(character, this))
        setActivity(nullptr);
}

void CharacterController::setNextActivity(std::unique_ptr<Activity> activity) {
    if (_currentActivity == nullptr) {
        setActivity(std::move(activity));
        _nextActivity = nullptr;
    } else {
        _nextActivity.swap(activity);
    }
}

bool CharacterController::isCurrentActivity(const std::string &activity) const {
    if (getCurrentActivity() == nullptr) return false;
    return getCurrentActivity()->name() == activity;
}

void CharacterController::update(float dt) {
    if (character->getCurrentVehicle()) {
        // Nevermind, the player is in a vehicle.

        auto &d = character->getMovement();

        if (character->getCurrentSeat() == 0) {
            character->getCurrentVehicle()->setSteeringAngle(d.y);

            if (std::abs(d.x) > 0.01f) {
                character->getCurrentVehicle()->setHandbraking(false);
            }
            character->getCurrentVehicle()->setThrottle(d.x);
        }

        if (_currentActivity == nullptr) {
            // If character is idle in vehicle, try to close the door.
            auto v = character->getCurrentVehicle();
            auto entryDoor = v->getSeatEntryDoor(character->getCurrentSeat());

            if (entryDoor && entryDoor->constraint) {
                if (glm::length(d) <= 0.1f) {
                    if (m_closeDoorTimer >= kCloseDoorIdleTime) {
                        character->getCurrentVehicle()->setPartTarget(
                            entryDoor, true, entryDoor->closedAngle);
                    }
                    m_closeDoorTimer += dt;
                } else {
                    m_closeDoorTimer = 0.f;
                }
            }
        }
    } else {
        m_closeDoorTimer = 0.f;
    }

    if (updateActivity()) {
        character->activityFinished();
        _currentActivity = nullptr;
        if (_nextActivity) {
            setActivity(std::move(_nextActivity));
            _nextActivity = nullptr;
        }
    }
}

CharacterObject *CharacterController::getCharacter() const {
    return character;
}

void CharacterController::setMoveDirection(const glm::vec3 &movement) {
    character->setMovement(movement);
}

void CharacterController::setLookDirection(const glm::vec2 &look) {
    character->setLook(look);
}

void CharacterController::setRunning(bool run) {
    character->setRunning(run);
}

bool Activities::GoTo::update(CharacterObject *character,
                              CharacterController *controller) {
    /* TODO: Use the ai nodes to navigate to the position */
    auto cpos = character->getPosition();
    glm::vec3 targetDirection = target - cpos;

    // Ignore vertical axis for the sake of simplicity.
    if (glm::length(glm::vec2(targetDirection)) < 0.1f) {
        character->setPosition(glm::vec3(glm::vec2(target), cpos.z));
        controller->setMoveDirection({0.f, 0.f, 0.f});
        character->controller->setRunning(false);
        return true;
    }

    float hdg =
        std::atan2(targetDirection.y, targetDirection.x) - glm::half_pi<float>();
    character->setHeading(glm::degrees(hdg));

    controller->setMoveDirection({1.f, 0.f, 0.f});
    controller->setRunning(sprint);

    return false;
}

glm::vec3 CharacterController::calculateRoadTarget(const glm::vec3 &target,
                                                   const glm::vec3 &start,
                                                   const glm::vec3 &end) {
    // @todo set the real value
    static constexpr float roadWidth = 5.f;

    static const glm::vec3 up = glm::vec3(0.f, 0.f, 1.f);
    const glm::vec3 dir = glm::normalize(start - end);

    // Calculate the strafe vector
    glm::vec3 strafe = glm::cross(up, dir);

    const glm::vec3 laneOffset =
        strafe *
        (roadWidth / 2 + roadWidth * static_cast<float>(getLane() - 1));

    return target + laneOffset;
}

void CharacterController::steerTo(const glm::vec3 &target) {
    // We can't drive without a vehicle
    VehicleObject* vehicle = character->getCurrentVehicle();
    if (vehicle == nullptr) {
        return;
    }

    // Calculate the steeringAngle
    float steeringAngle = 0.0f;
    float deviation = glm::abs(vehicle->isOnSide(target)) / 5.f;

    // If we are almost at the right angle, decrease the deviation to reduce wiggling
    if (deviation < 1.f) deviation = deviation / 5.f;

    // Make sure to normalize the value
    deviation = glm::clamp(deviation, 0.f, 1.f);

    // Set the right sign
    steeringAngle = std::copysign(deviation, -vehicle->isOnSide(target));

    vehicle->setSteeringAngle(steeringAngle, true);  
}

bool CharacterController::checkForObstacles() {
    // We can't drive without a vehicle
    VehicleObject *vehicle = character->getCurrentVehicle();
    if (vehicle == nullptr) {
        return false;
    }

    HitTest test{*vehicle->engine->dynamicsWorld};

    auto[center, halfSize] = vehicle->obstacleCheckVolume();
    const auto rotation = vehicle->getRotation();
    center = vehicle->getPosition() + rotation * center;
    auto results = test.boxTest(center, halfSize, vehicle->getRotation());

    return any_of(results.begin(), results.end(),
                  [&](const auto &hit) {
                      return !(hit.object == vehicle ||
                               hit.body->isStaticObject());
                  });
}

bool Activities::DriveTo::update(CharacterObject *character,
                              CharacterController *controller) {

    // We can't drive without a vehicle
    VehicleObject* vehicle = character->getCurrentVehicle();
    if (vehicle == nullptr) {
        return true;
    }

    // Get the nodes from the controller
    AIGraphNode* lastTargetNode = controller->lastTargetNode;
    AIGraphNode* nextTargetNode = controller->nextTargetNode;


    // That's the position the vehicle is actually targeting
    // depending on the lane, we have to shift its position
    glm::vec3 roadTarget;

    // A list of nodes we can choose from
    std::vector<AIGraphNode*> potentialNodes = targetNode->connections;

    // Make sure that we have a lastTargetNode
    if (lastTargetNode == nullptr) {
        for (const auto &node : potentialNodes) {
            if (vehicle->isInFront(node->position) < 0.f) {
                lastTargetNode = node;
                break;
            }
        }
    }
    if (lastTargetNode == nullptr) {
        return false;
    }

    // Remove unwanted nodes
    for( auto i = potentialNodes.begin(); i != potentialNodes.end(); ) {
        // @todo we don't know the direction of the road, so for now, choose the bigger value
        int maxLanes = (*i)->rightLanes > (*i)->leftLanes ? (*i)->rightLanes : (*i)->leftLanes;

        // We don't want roads with lanes <= 0, also ignore the lastTargetNode
        if ( (*i) == lastTargetNode || maxLanes <= 0) {
            i = potentialNodes.erase(i);
        }
        else {
            ++i;
        }
    }

    // That's a dead end, try to turn around
    if (potentialNodes.empty()) {
        //@todo try to turn around
    }
    // Just a normal road
    if (potentialNodes.size() == 1) {
        roadTarget = controller->calculateRoadTarget(
            targetNode->position, lastTargetNode->position,
            potentialNodes.at(0)->position);
    }
    // Intersection, choose a direction
    else if (potentialNodes.size() > 1) {
        // Choose the next node randomly
        if(nextTargetNode == nullptr) {
            auto i = character->engine->getRandomNumber(
                0u, potentialNodes.size() - 1);
            nextTargetNode = potentialNodes.at(i);
        }

        // Set the nextTargetNode to make sure we go this direction
        controller->nextTargetNode = nextTargetNode;

        roadTarget = controller->calculateRoadTarget(targetNode->position,
                                                     lastTargetNode->position,
                                                     nextTargetNode->position);
    }
    // Otherwise set the target to the current node
    else {
        roadTarget = targetNode->position;
    }

    // Check whether we reached the node
    const auto targetDistance = glm::vec2(vehicle->getPosition() - roadTarget);

    static constexpr float reachDistance = 5.0f;

    if (glm::length(targetDistance) <= reachDistance) {
        // Finish the activity
        return true;
    } 

    // @todo set real values
    static constexpr float maximumSpeed = 10.f;
    static constexpr float intersectionSpeed = 3.5f;

    float currentSpeed = 0.f;

    // Set the speed depending on where we are driving
    if ( potentialNodes.size() == 1 ) {
        currentSpeed = maximumSpeed;
    }
    else {
        currentSpeed = intersectionSpeed;
    }

    // Check whether a pedestrian or vehicle is in our way
    if (controller->checkForObstacles()) {
        currentSpeed = 0.f;
    }

    if (std::fabs(currentSpeed) < 0.1f) {
        vehicle->setHandbraking(true);
        vehicle->setThrottle(0.f);
    }
    // Is the vehicle slower than it should be
    else if (vehicle->getVelocity() < currentSpeed) {
        vehicle->setHandbraking(false);

        // The vehicle is driving backwards, accelerate
        if (vehicle->getVelocity() < 0) {
            vehicle->setThrottle(1.f);
        }
        // Slowly accelerate until we reach the designated speed
        else {
            vehicle->setThrottle(1.f - (vehicle->getVelocity() / currentSpeed));
        }
    }
    // We are to fast, activate the handbrake - works better
    else {
        vehicle->setHandbraking(true);
    }

    // Steer to the target
    controller->steerTo(roadTarget);

    return false;
}

bool Activities::Jump::update(CharacterObject *character,
                              CharacterController *controller) {
    RW_UNUSED(controller);
    if (character->physCharacter == nullptr) return true;

    if (!jumped) {
        character->jump();
        jumped = true;
    } else if (character->physCharacter->canJump()) {
        return true;
    }

    return false;
}

bool Activities::EnterVehicle::canSkip(CharacterObject *character,
                                       CharacterController *) const {
    // If we're already inside the vehicle, it can't helped.
    if (character->getCurrentVehicle() != nullptr) {
        return false;
    }

    switch (character->getCurrentCycle())
    {
        case AnimCycle::CarOpenLHS:
        case AnimCycle::CarGetInLHS:
        case AnimCycle::CarPullOutLHS:
        case AnimCycle::CarOpenRHS:
        case AnimCycle::CarGetInRHS:
        case AnimCycle::CarPullOutRHS:
            return false;
        default:
            return true;
    }
}

bool Activities::EnterVehicle::update(CharacterObject *character,
                                      CharacterController *controller) {
    constexpr float kSprintToEnterDistance = 5.f;
    constexpr float kGiveUpDistance = 50.f;

    RW_UNUSED(controller);

    // Boats don't have any kind of entry animation unless you're onboard.
    if (vehicle->getVehicle()->vehicletype_ == VehicleModelInfo::BOAT) {
        character->enterVehicle(vehicle, seat);
        return true;
    }

    if (seat == ANY_SEAT) {
        // Determine which seat to take.
        float nearest = std::numeric_limits<float>::max();
        for (unsigned int s = 1; s < vehicle->info->seats.size(); ++s) {
            auto entry = vehicle->getSeatEntryPositionWorld(s);
            float dist = glm::distance(entry, character->getPosition());
            if (dist < nearest) {
                seat = s;
                nearest = dist;
            }
        }
    }

    auto entryDoor = vehicle->getSeatEntryDoor(seat);
    auto entryPos = vehicle->getSeatEntryPositionWorld(seat);
    auto entryPosLocal = vehicle->getSeatEntryPosition(seat);

    auto cycle_open = AnimCycle::CarOpenLHS;
    auto cycle_enter = AnimCycle::CarGetInLHS;
    auto cycle_pullout = AnimCycle::CarPullOutLHS;

    if (entryPosLocal.x > 0.f) {
        cycle_open = AnimCycle::CarOpenRHS;
        cycle_enter = AnimCycle::CarGetInRHS;
        cycle_pullout = AnimCycle::CarPullOutRHS;
    }

    // If there's someone in this seat already, we may have to ask them to
    // leave.
    auto currentOccupant =
        static_cast<CharacterObject *>(vehicle->getOccupant(seat));

    bool tryToEnter = false;

    if (entering) {
        character->setPosition(entryPos);
        if (character->getCurrentCycle() == cycle_open) {
            if (character->animator->isCompleted(AnimIndexAction)) {
                tryToEnter = true;
            } else if (entryDoor &&
                       character->animator->getAnimationTime(AnimIndexAction) >=
                           0.5f) {
                vehicle->setPartTarget(entryDoor, true, entryDoor->openAngle);
            } else {
                character->rotation = vehicle->getRotation();
            }
        } else if (character->getCurrentCycle() == cycle_pullout) {
            if (character->animator->isCompleted(AnimIndexAction)) {
                tryToEnter = true;
            }
        } else if (character->getCurrentCycle() == cycle_enter) {
            if (character->animator->isCompleted(AnimIndexAction)) {
                /// @todo move to a more suitable place
                vehicle->grantOccupantRewards(character);
                vehicle->setImmobilised(false);

                // VehicleGetIn is over, finish activity
                return true;
            }
        }
    } else {
        glm::vec3 targetDirection = entryPos - character->getPosition();
        targetDirection.z = 0.f;

        float targetDistance = glm::length(targetDirection);

        if (targetDistance <= 0.4f) {
            character->setPosition(entryPos);
            entering = true;
            // Warp character to vehicle orientation
            character->controller->setMoveDirection({0.f, 0.f, 0.f});
            character->controller->setRunning(false);
            character->setHeading(
                glm::degrees(glm::roll(vehicle->getRotation())));

            // Determine if the door open animation should be skipped.
            if (entryDoor == nullptr ||
                (entryDoor->constraint != nullptr &&
                 glm::abs(entryDoor->constraint->getHingeAngle()) >= 0.6f)) {
                tryToEnter = true;
            } else {
                character->playCycle(cycle_open);
            }
        } else if (targetDistance > kGiveUpDistance) {
            return true;
        } else {
            if (targetDistance > kSprintToEnterDistance) {
                character->controller->setRunning(true);
            }
            character->setHeading(
                glm::degrees(std::atan2(targetDirection.y, targetDirection.x) -
                             glm::half_pi<float>()));
            character->controller->setMoveDirection({1.f, 0.f, 0.f});
        }
    }

    if (tryToEnter) {
        if (currentOccupant != nullptr && currentOccupant != character) {
            // Play the pullout animation and tell the other character to get
            // out.
            character->playCycle(cycle_pullout);

            if (currentOccupant->controller->getCurrentActivity() != nullptr) {
                currentOccupant->controller->skipActivity();
            }

            currentOccupant->controller->setNextActivity(
                std::make_unique<Activities::ExitVehicle>(true));

            vehicle->setImmobilised(true);
        } else {
            character->playCycle(cycle_enter);
            character->enterVehicle(vehicle, seat);
        }
    }
    return false;
}

bool Activities::ExitVehicle::update(CharacterObject *character,
                                     CharacterController *controller) {
    RW_UNUSED(controller);

    if (jacked) {
        const auto jacked_lhs = AnimCycle::CarJackedLHS;
        const auto jacked_rhs = AnimCycle::CarJackedRHS;
        const auto cycle_current = character->getCurrentCycle();

        if (cycle_current == jacked_lhs || cycle_current == jacked_rhs) {
            if (character->animator->isCompleted(AnimIndexAction)) {
                return true;
            }
        } else {
            if (character->getCurrentVehicle() == nullptr) return true;

            auto vehicle = character->getCurrentVehicle();
            auto seat = character->getCurrentSeat();
            auto door = vehicle->getSeatEntryDoor(seat);
            RW_UNUSED(door);
            auto exitPos = vehicle->getSeatEntryPositionWorld(seat);
            auto exitPosLocal = vehicle->getSeatEntryPosition(seat);

            character->rotation = vehicle->getRotation();

            // Exit the vehicle immediately
            character->enterVehicle(nullptr, seat);
            character->setPosition(exitPos);

            if (exitPosLocal.x > 0.f) {
                character->playCycle(jacked_rhs);
            } else {
                character->playCycle(jacked_lhs);
            }
            // No need to open the door, it should already be open.
        }
        return false;
    }

    if (character->getCurrentVehicle() == nullptr) return true;

    auto vehicle = character->getCurrentVehicle();
    auto seat = character->getCurrentSeat();
    auto door = vehicle->getSeatEntryDoor(seat);
    auto exitPos = vehicle->getSeatEntryPositionWorld(seat);
    auto exitPosLocal = vehicle->getSeatEntryPosition(seat);

    auto cycle_exit = AnimCycle::CarGetOutLHS;

    if (exitPosLocal.x > 0.f) {
        cycle_exit = AnimCycle::CarGetOutRHS;
    }

    if (vehicle->getVehicle()->vehicletype_ == VehicleModelInfo::BOAT) {
        auto ppos = character->getPosition();
        character->enterVehicle(nullptr, seat);
        character->setPosition(ppos);
        return true;
    }

    bool isDriver = vehicle->isOccupantDriver(character->getCurrentSeat());

    // If the vehicle is going too fast, slow down
    if (isDriver) {
        if (!vehicle->canOccupantExit()) {
            vehicle->setBraking(1.f);
            return false;
        }
    }

    if (character->getCurrentCycle() == cycle_exit) {
        if (character->animator->isCompleted(AnimIndexAction)) {
            character->enterVehicle(nullptr, seat);
            character->setPosition(exitPos);

            if (isDriver) {
                // Apply the handbrake
                vehicle->setHandbraking(true);
                vehicle->setThrottle(0.f);
            }

            return true;
        }
    } else {
        character->playCycle(cycle_exit);
        if (door) {
            vehicle->setPartTarget(door, true, door->openAngle);
        }
    }

    return false;
}

bool Activities::UseItem::update(CharacterObject *character,
                                 CharacterController *controller) {
    RW_UNUSED(controller);

    if (itemslot >= kMaxInventorySlots) {
        return true;
    }

    // Finds the cycle associated with an anim from the AnimGroup
    /// @todo doesn't need to happen every update..
    auto find_cycle = [&](const std::string &name) {
        if (name == "null") {
            return AnimCycle::Idle;
        }
        for (auto &i : character->animations->animations_) {
            if (i.name == name) return i.id;
        }
        return AnimCycle::Idle;
    };

    auto world = character->engine;
    auto weapon = &world->data->weaponData.at(itemslot);
    auto &state = character->getCurrentState().weapons[itemslot];
    auto &animator = character->animator;
    auto shootcycle = find_cycle(weapon->animation1);
    auto throwcycle = find_cycle(weapon->animation2);

    // No weapon except for Uzi can be used while in a vehicle
    if (character->getCurrentVehicle() && weapon->name != "uzi") {
        return false;
    }

    // Instant hit weapons loop their anim
    // Thrown projectiles have lob / throw.

    // Update player direction
    character->setRotation(
        glm::angleAxis(character->getLook().x, glm::vec3{0.f, 0.f, 1.f}));

    if (state.bulletsClip == 0 && state.bulletsTotal > 0) {
        state.bulletsClip +=
            std::min(static_cast<int>(state.bulletsTotal), weapon->clipSize);
        state.bulletsTotal -= state.bulletsClip;
    }
    bool hasammo = state.bulletsClip > 0;

    if (weapon->fireType == WeaponData::INSTANT_HIT) {
        if (!character->getCurrentState().primaryActive) {
            // Character is no longer firing
            return true;
        }
        if (hasammo) {
            if (character->getCurrentCycle() != shootcycle) {
                character->playCycle(shootcycle);
            }

            auto loopstart = weapon->animLoopStart / 100.f;
            auto loopend = weapon->animLoopEnd / 100.f;
            auto firetime = weapon->animFirePoint / 100.f;

            auto currenttime = animator->getAnimationTime(AnimIndexAction);

            if (currenttime >= firetime && !fired) {
                state.bulletsClip--;
                Weapon::fireHitscan(weapon, character);
                fired = true;
            }
            if (currenttime > loopend) {
                animator->setAnimationTime(AnimIndexAction, loopstart);
                fired = false;
            }
        } else if (animator->isCompleted(AnimIndexAction)) {
            // Should we exit this state when out of ammo?
            return true;
        }
    }
    /// @todo Use Thrown flag instead of project (RPG isn't thrown eg.)
    else if (weapon->fireType == WeaponData::PROJECTILE && hasammo) {
        if (character->getCurrentCycle() == shootcycle) {
            if (character->getCurrentState().primaryActive) {
                power = animator->getAnimationTime(AnimIndexAction) / 0.5f;
            }
            if (animator->isCompleted(AnimIndexAction)) {
                character->playCycle(throwcycle);
            }
        } else if (character->getCurrentCycle() == throwcycle) {
            auto firetime = weapon->animCrouchFirePoint / 100.f;
            auto currID = animator->getAnimationTime(AnimIndexAction);

            if (currID >= firetime && !fired) {
                state.bulletsClip--;
                Weapon::fireProjectile(weapon, character, power);
                fired = true;
            }
            if (animator->isCompleted(AnimIndexAction)) {
                return true;
            }
        } else {
            character->playCycle(shootcycle);
        }
    } else if (weapon->fireType == WeaponData::MELEE) {
        auto currentAnim = character->getCurrentCycle();
        if (currentAnim == shootcycle || currentAnim == throwcycle) {
            auto fireTime = weapon->animFirePoint / 100.f;
            auto loopStart = weapon->animLoopStart / 100.f;
            auto currentTime = animator->getAnimationTime(AnimIndexAction);

            if (currentTime >= fireTime && !fired) {
                Weapon::meleeHit(weapon, character);
                fired = true;
            }

            if (animator->isCompleted(AnimIndexAction)) {
                if (character->getCurrentState().primaryActive) {
                    animator->setAnimationTime(AnimIndexAction, loopStart);
                    fired = false;
                }
                else {
                    return true;
                }
            }
        }
        else {
            const auto onGround = Weapon::targetOnGround(weapon, character);
            if (onGround) {
                character->playCycle(throwcycle);
            }
            else {
                character->playCycle(shootcycle);
            }
        }

    } else {
        RW_ERROR("Unrecognized fireType: " << weapon->fireType);
        return true;
    }

    return false;
}

}  // namespace ai
