#include <btBulletDynamicsCommon.h>
#include <ai/CharacterController.hpp>
#include <data/Clump.hpp>
#include <engine/Animator.hpp>
#include <engine/GameData.hpp>
#include <engine/GameWorld.hpp>
#include <items/Weapon.hpp>
#include <objects/CharacterObject.hpp>
#include <objects/VehicleObject.hpp>
#include <rw/defines.hpp>

constexpr float kCloseDoorIdleTime = 2.f;

CharacterController::CharacterController()
    : character(nullptr)
    , _currentActivity(nullptr)
    , _nextActivity(nullptr)
    , m_closeDoorTimer(0.f)
    , currentGoal(None)
    , leader(nullptr)
    , targetNode(nullptr) {
}

bool CharacterController::updateActivity() {
    if (_currentActivity && character->isAlive()) {
        return _currentActivity->update(character, this);
    }

    return false;
}

void CharacterController::setActivity(CharacterController::Activity *activity) {
    if (_currentActivity) delete _currentActivity;
    _currentActivity = activity;
}

void CharacterController::skipActivity() {
    // Some activities can't be cancelled, such as the final phase of entering a
    // vehicle
    // or jumping.
    if (getCurrentActivity() != nullptr &&
        getCurrentActivity()->canSkip(character, this))
        setActivity(nullptr);
}

void CharacterController::setNextActivity(
    CharacterController::Activity *activity) {
    if (_currentActivity == nullptr) {
        setActivity(activity);
        _nextActivity = nullptr;
    } else {
        if (_nextActivity) delete _nextActivity;
        _nextActivity = activity;
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
        if (_currentActivity) {
            delete _currentActivity;
            _currentActivity = nullptr;
        }
        if (_nextActivity) {
            setActivity(_nextActivity);
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
        atan2(targetDirection.y, targetDirection.x) - glm::half_pi<float>();
    character->setHeading(glm::degrees(hdg));

    controller->setMoveDirection({1.f, 0.f, 0.f});
    controller->setRunning(sprint);

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
    return character->getCurrentVehicle() == nullptr;
}

bool Activities::EnterVehicle::update(CharacterObject *character,
                                      CharacterController *controller) {
    constexpr float kSprintToEnterDistance = 5.f;
    constexpr float kGiveUpDistance = 100.f;

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
        if (character->getCurrentCycle() == cycle_open) {
            if (character->animator->isCompleted(AnimIndexAction)) {
                tryToEnter = true;
            } else if (entryDoor &&
                       character->animator->getAnimationTime(AnimIndexAction) >=
                           0.5f) {
                vehicle->setPartTarget(entryDoor, true, entryDoor->openAngle);
            } else {
                // character->setPosition(vehicle->getSeatEntryPosition(seat));
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

                // VehicleGetIn is over, finish activity
                return true;
            }
        }
    } else {
        glm::vec3 targetDirection = entryPos - character->getPosition();
        targetDirection.z = 0.f;

        float targetDistance = glm::length(targetDirection);

        if (targetDistance <= 0.4f) {
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
                glm::degrees(atan2(targetDirection.y, targetDirection.x) -
                             glm::half_pi<float>()));
            character->controller->setMoveDirection({1.f, 0.f, 0.f});
        }
    }

    if (tryToEnter) {
        if (currentOccupant != nullptr && currentOccupant != character) {
            // Play the pullout animation and tell the other character to get
            // out.
            character->playCycle(cycle_pullout);
            currentOccupant->controller->setNextActivity(
                new Activities::ExitVehicle(true));
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

            // Exit the vehicle immediatley
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
    const auto &weapon = world->data->weaponData.at(itemslot);
    auto &state = character->getCurrentState().weapons[itemslot];
    auto animator = character->animator;
    auto shootcycle = find_cycle(weapon->animation1);
    auto throwcycle = find_cycle(weapon->animation2);

    // Instant hit weapons loop their anim
    // Thrown projectiles have lob / throw.

    // Update player direction
    character->setRotation(
        glm::angleAxis(character->getLook().x, glm::vec3{0.f, 0.f, 1.f}));

    if (state.bulletsClip == 0 && state.bulletsTotal > 0) {
        state.bulletsClip +=
            std::min(int(state.bulletsTotal), weapon->clipSize);
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
                Weapon::fireHitscan(weapon.get(), character);
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
                Weapon::fireProjectile(weapon.get(), character, power);
                fired = true;
            }
            if (animator->isCompleted(AnimIndexAction)) {
                return true;
            }
        } else {
            character->playCycle(shootcycle);
        }
    } else if (weapon->fireType == WeaponData::MELEE) {
        RW_CHECK(weapon->fireType != WeaponData::MELEE,
                 "Melee attacks not implemented");
        return true;
    } else {
        RW_ERROR("Unrecognized fireType: " << weapon->fireType);
        return true;
    }

    return false;
}
