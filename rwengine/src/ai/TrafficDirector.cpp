#include "ai/TrafficDirector.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/transform.hpp>
#include <string>

#include "ai/AIGraph.hpp"
#include "ai/AIGraphNode.hpp"
#include "ai/CharacterController.hpp"
#include "engine/GameData.hpp"
#include "engine/GameState.hpp"
#include "engine/GameWorld.hpp"
#include "objects/CharacterObject.hpp"
#include "objects/GameObject.hpp"
#include "objects/VehicleObject.hpp"
#include "render/ViewCamera.hpp"

#ifdef RW_WINDOWS
#include <rw_mingw.hpp>
#endif

namespace ai {

TrafficDirector::TrafficDirector(AIGraph* g, GameWorld* w)
    : graph(g)
    , world(w) {
}

std::vector<ai::AIGraphNode*> TrafficDirector::findAvailableNodes(
    ai::NodeType type, const ViewCamera& camera, float radius) {
    std::vector<ai::AIGraphNode*> available;
    available.reserve(20);

    graph->gatherExternalNodesNear(camera.position, radius, available, type);

    float density = type == ai::NodeType::Vehicle ? carDensity : pedDensity;
    float minDist = (15.f / density) * (15.f / density);
    float halfRadius2 = std::pow(radius / 2.f, 2.f);

    // Check if any of the nearby nodes are blocked by a pedestrian or vehicle standing on
    // it
    // or because it's inside the view frustum
    for (auto it = available.begin(); it != available.end();) {
        bool blocked = false;
        float dist2 = glm::distance2(camera.position, (*it)->position);

        for (const auto& obj : world->pedestrianPool.objects) {
            if (glm::distance2((*it)->position, obj.second->getPosition()) <=
                minDist) {
                blocked = true;
                break;
            }
        }

        for (const auto& obj : world->vehiclePool.objects) {
            if (glm::distance2((*it)->position, obj.second->getPosition()) <=
                minDist) {
                blocked = true;
                break;
            }
        }

        // Check that we're not going to spawn something right where the player
        // is looking
        if (dist2 <= halfRadius2 &&
            camera.frustum.intersects((*it)->position, 1.f)) {
            blocked = true;
        }

        if (blocked) {
            it = available.erase(it);
        } else {
            it++;
        }
    }

    return available;
}

void TrafficDirector::setDensity(ai::NodeType type, float density) {
    switch (type) {
        case ai::NodeType::Vehicle:
            carDensity = density;
            break;
        case ai::NodeType::Pedestrian:
            pedDensity = density;
            break;
    }
}

uint16_t TrafficDirector::assignDriver(const std::string &vehiclename) {
    enum ped_types { COP = 1, MEDIC = 5, FIREMAN = 6 };
    if (vehiclename == "POLICAR") {
        return COP;
    } else if (vehiclename == "AMBULAN") {
        return MEDIC;
    } else if (vehiclename == "FIRETRUK") {
        return FIREMAN;
    } else {
        return 0;
    }
}

std::vector<GameObject*> TrafficDirector::populateNearby(
    const ViewCamera& camera, float radius, int maxSpawn) {

    std::vector<GameObject*> created;

    /// @todo Check how "in player view" should be determined.

    // Don't check the frustum for things more than 1/2 of the radius away
    // so that things will spawn as you drive towards them
    float halfRadius2 = std::pow(radius / 2.f, 2.f);

    // Spawn vehicles at vehicle generators
    auto camera2D = glm::vec2(camera.position);
    for (auto& gen : world->state->vehicleGenerators) {
        /// @todo verify how vehicle generator proximity is determined
        auto gen2D = glm::vec2(gen.position);
        float dist2 = glm::distance2(camera2D, gen2D);
        if (dist2 < radius * radius) {
            auto position = gen.position;
            // Check that the on-ground position is not in view
            if (gen.position.z < -90.f) {
                position = world->getGroundAtPosition(position);
            }

            if (dist2 <= halfRadius2 &&
                camera.frustum.intersects(position, 1.f)) {
                if (!gen.alwaysSpawn) {
                    // Don't spawn in the view frustum unless we're forced to
                    continue;
                }
            }
            auto spawned = world->tryToSpawnVehicle(gen);
            if (spawned) {
                created.push_back(spawned);
            }
        }
    }

    // Hardcoded cop Pedestrian
    std::vector<uint16_t> peds = {1};
    uint16_t pedId;

    // Determine which zone the viewpoint is in
    auto zone = world->data->findZoneAt(camera.position);
    bool day = (world->state->basic.gameHour >= 8 &&
                world->state->basic.gameHour <= 19);
    int groupid = zone ? (day ? zone->pedGroupDay : zone->pedGroupNight) : 0;
    const auto& group = world->data->pedgroups.at(groupid);
    peds.insert(peds.end(), group.cbegin(), group.cend());

    // Vehicles for normal traffic @todo create correct vehicle list
    static constexpr std::array<uint16_t, 32> cars = {{
        90, 91, 92, 94, 95, 97, 98, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110,
        111, 112, 116, 119, 128, 129, 130, 134, 135, 136, 138, 139, 144, 146
    }};

    auto availablePedsNodes = findAvailableNodes(ai::NodeType::Pedestrian, camera, radius);

    // We have not reached the limit of spawned pedestrians
    if (maximumPedestrians > world->pedestrianPool.objects.size()) {
        const auto availablePeds = maximumPedestrians - world->pedestrianPool.objects.size();

        size_t counter = availablePeds;
        // maxSpawn can be -1 for "as many as possible"
        if (maxSpawn > -1) {
            counter = std::min(availablePeds, static_cast<size_t>(maxSpawn));
        }

        for (AIGraphNode* spawn : availablePedsNodes) {
            if (spawn->type != ai::NodeType::Pedestrian) {
                continue;
            }
            if (counter == 0) {
                break;
            }
            counter--;

            // Spawn a pedestrian from the available pool
            pedId = peds.at(world->getRandomNumber(0u, peds.size() - 1));
            auto ped = world->createPedestrian(pedId, spawn->position);
            ped->applyOffset();
            ped->setLifetime(GameObject::TrafficLifetime);
            ped->controller->setGoal(CharacterController::TrafficWander);
            created.push_back(ped);
        }
    }

    auto availableVehicleNodes = findAvailableNodes(ai::NodeType::Vehicle, camera, radius);

    // We have not reached the limit of spawned vehicles
    if (maximumCars > world->vehiclePool.objects.size()) {
        const auto availableCars = maximumCars - world->vehiclePool.objects.size();

        size_t counter = availableCars;
        // maxSpawn can be -1 for "as many as possible"
        if (maxSpawn > -1) {
            counter = std::min(availableCars, static_cast<size_t>(maxSpawn));
        }

        for (AIGraphNode* spawn : availableVehicleNodes) {
            if (spawn->type != ai::NodeType::Vehicle) {
                continue;
            }
            if (counter == 0) {
                break;
            }
            counter--;

            // Get the next node, to spawn in between
            AIGraphNode* next = spawn->connections.at(0);

            // Set the spawn point to the middle of the two nodes
            const glm::vec3 diff = (spawn->position - next->position) / 2.f;

            // Calculate the orientation of the vehicle
            glm::mat4 rotMat = glm::lookAt(next->position, spawn->position, glm::vec3(0,0,1));
            const glm::mat4 rotate =
                glm::rotate(glm::radians(90.f), glm::vec3(1, 0, 0));
            rotMat = rotate * rotMat;

            const glm::quat orientation = glm::conjugate(glm::toQuat(rotMat));

            const glm::vec3 up = glm::vec3(0, 0, 1);
            const glm::vec3 dir =
                glm::normalize(next->position - spawn->position);

            // Calculate the strafe vector
            const glm::vec3 strafe = glm::cross(up, dir);

            // @todo we don't know the direction of the street, so for now, choose the smaller value
            int maxLanes = spawn->rightLanes < spawn->leftLanes ? spawn->rightLanes : spawn->leftLanes;
	
            // This street has no lanes, continue
            if( maxLanes <= 0 ) {
                continue;
            }

            // Choose a random lane
            const int lane = world->getRandomNumber(1, maxLanes);
            const glm::vec3 laneOffset =
                strafe * (2.5f + 5.f * static_cast<float>(lane - 1));

            // Spawn a vehicle from the available pool
            const auto carId =
                cars.at(world->getRandomNumber(0u, cars.size() - 1));
            auto vehicle = world->createVehicle(carId, next->position + diff + laneOffset, orientation);
            vehicle->applyOffset();
            vehicle->setLifetime(GameObject::TrafficLifetime);
            vehicle->setHandbraking(false);

            // Spawn a pedestrian and put it into the vehicle
            pedId = TrafficDirector::assignDriver(
                vehicle->getVehicle()->vehiclename_);
            if (pedId == 0) {
                // not an special car, random person in
                pedId = peds.at(world->getRandomNumber(0u, peds.size() - 1));
            }
            CharacterObject* character =
                world->createPedestrian(pedId, vehicle->getPosition());
            character->setLifetime(GameObject::TrafficLifetime);
            character->setCurrentVehicle(vehicle, 0);
            character->controller->setGoal(CharacterController::TrafficDriver);
            character->controller->setLane(lane);
            vehicle->setOccupant(0, character);

            created.push_back(character);
            created.push_back(vehicle);
        }
    }

    // Find places it's legal to spawn things

    return created;
}

void TrafficDirector::setPopulationLimits(int maxPeds, int maxCars) {
    maximumPedestrians = maxPeds;
    maximumCars = maxCars;
}

}  // namespace ai
