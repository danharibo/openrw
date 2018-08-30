#ifndef _RWENGINE_SCRIPTFUNCTIONS_HPP_
#define _RWENGINE_SCRIPTFUNCTIONS_HPP_

#include <rw/debug.hpp>

#include <ai/AIGraphNode.hpp>
#include <data/ModelData.hpp>
#include <engine/GameData.hpp>
#include <engine/GameState.hpp>
#include <engine/GameWorld.hpp>
#include <fonts/GameTexts.hpp>
#include <objects/GameObject.hpp>
#include <objects/CharacterObject.hpp>
#include <objects/VehicleObject.hpp>
#include <script/ScriptMachine.hpp>
#include <script/ScriptTypes.hpp>
#include <glm/gtx/norm.hpp>

/**
 * Implementations for common functions likely to be shared
 * among many script modules and opcodes.
 */
namespace script {

inline void getObjectPosition(GameObject* object, ScriptFloat& x,
                              ScriptFloat& y, ScriptFloat& z) {
    const auto& p = object->getPosition();
    x = p.x;
    y = p.y;
    z = p.z;
}
inline void setObjectPosition(GameObject* object, const ScriptVec3& coord) {
    object->setPosition(coord);
    object->applyOffset();
}

inline VehicleObject* getCharacterVehicle(CharacterObject* character) {
    return character->getCurrentVehicle();
}
inline bool isInModel(const ScriptArguments& args, CharacterObject* character,
                      int model) {
    auto data = args.getWorld()->data->findModelInfo<VehicleModelInfo>(model);
    if (data) {
        auto vehicle = getCharacterVehicle(character);
        if (vehicle) {
            return vehicle->getVehicle()->id() == model;
        }
    }
    return false;
}

template <class Tvec>
inline void drawAreaIndicator(const ScriptArguments& args, const Tvec& coord,
                              const Tvec& radius) {
    auto ground = args.getWorld()->getGroundAtPosition(
        ScriptVec3(coord.x, coord.y, 100.f));
    args.getWorld()->drawAreaIndicator(AreaIndicatorInfo::Cylinder, ground,
                                       ScriptVec3(radius.x, radius.y, 5.f));
}
inline bool objectInBounds(GameObject* object, const ScriptVec2& min,
                           const ScriptVec2& max) {
    const auto& p = object->getPosition();
    return (p.x >= min.x && p.y >= min.y && p.x <= max.x && p.y <= max.y);
}
inline bool objectInBounds(GameObject* object, const ScriptVec3& min,
                           const ScriptVec3& max) {
    const auto& p = object->getPosition();
    return (p.x >= min.x && p.y >= min.y && p.z >= min.z && p.x <= max.x &&
            p.y <= max.y && p.z <= max.z);
}
template <class Tobj, class Tvec>
bool objectInArea(const ScriptArguments& args, const Tobj& object,
                  const Tvec& pointA, const Tvec& pointB, bool marker,
                  bool preconditions = true) {
    auto min = glm::min(pointA, pointB);
    auto max = glm::max(pointA, pointB);
    if (marker) {
        auto center = (min + max) / 2.f;
        auto radius = max - min;
        drawAreaIndicator(args, center, radius);
    }
    return preconditions && objectInBounds(object, min, max);
}

inline bool objectInSphere(GameObject* object, const ScriptVec2& center,
                           const ScriptVec2& radius) {
    auto p = glm::vec2(object->getPosition());
    p = glm::abs(p - center);
    return p.x <= radius.x && p.y <= radius.y;
}
inline bool objectInSphere(GameObject* object, const ScriptVec3& center,
                           const ScriptVec3& radius) {
    auto p = object->getPosition();
    p = glm::abs(p - center);
    return p.x <= radius.x && p.y <= radius.y && p.z <= radius.z;
}
template <class Tobj, class Tvec>
inline bool objectInRadius(const ScriptArguments& args, const Tobj& object,
                           const Tvec& center, const Tvec& radius, bool marker,
                           bool preconditions = true) {
    if (marker) {
        drawAreaIndicator(args, center, radius);
    }
    return preconditions && objectInSphere(object, center, radius);
}
template <class Tvec>
inline bool objectInRadiusNear(const ScriptArguments& args, GameObject* object,
                               GameObject* obj_near, const Tvec& radius,
                               bool marker, bool preconditions = true) {
    Tvec center(obj_near->getPosition());
    if (marker) {
        drawAreaIndicator(args, center, radius);
    }
    return preconditions && objectInSphere(object, center, radius);
}

template <class Tobj>
inline bool objectInZone(const ScriptArguments& args, Tobj object,
                         const ScriptString name) {
    auto zone = args.getWorld()->data->findZone(name);
    if (zone) {
        auto& min = zone->min;
        auto& max = zone->max;
        return objectInBounds(object, min, max);
    }
    return false;
}

inline void destroyObject(const ScriptArguments& args, GameObject* object) {
    args.getWorld()->destroyObjectQueued(object);
}

inline ScriptVec3 getGround(const ScriptArguments& args, ScriptVec3 p) {
    if (p.z <= -100.f) {
        p = args.getWorld()->getGroundAtPosition(p);
    }
    return p;
}

inline GameString gxt(const ScriptArguments& args, const ScriptString id) {
    return args.getWorld()->data->texts.text(id);
}

inline BlipData& createBlip(const ScriptArguments& args, const ScriptVec3& coord,
                            BlipData::BlipType type) {
    BlipData data;
    data.coord = coord;
    data.type = type;
    data.display = BlipData::ShowBoth;
    switch (type) {
        case BlipData::Contact:
            data.colour = 2;
            break;
        case BlipData::Coord:
            data.colour = 5;
            data.display = BlipData::RadarOnly;
            break;
        default:
            RW_ERROR("Unhandled blip type");
            break;
    }
    data.target = 0;
    data.texture = "";
    data.size = 3;
    auto blip = args.getState()->addRadarBlip(data);
    return args.getState()->radarBlips[blip];
}

const char* getBlipSprite(ScriptRadarSprite sprite);

inline BlipData& createBlipSprite(const ScriptArguments& args, const ScriptVec3& coord,
                                  BlipData::BlipType type, int sprite) {
    auto& data = script::createBlip(args, coord, type);
    data.texture = getBlipSprite(sprite);
    return data;
}

inline BlipData& createObjectBlip(const ScriptArguments& args,
                                  GameObject* object) {
    BlipData data;
    switch (object->type()) {
        case GameObject::Vehicle:
            data.type = BlipData::Vehicle;
            data.colour = 0;  // @todo 4 in Vice City
            break;
        case GameObject::Character:
            data.type = BlipData::Character;
            data.colour = 1;  // @todo 4 in Vice City
            break;
        case GameObject::Pickup:
            data.type = BlipData::Pickup;
            data.colour = 6;  // @todo 4 in Vice City
            break;
        case GameObject::Instance:
            data.type = BlipData::Instance;
            data.colour = 6;  // @todo 4 in Vice City
            break;
        default:
            data.type = BlipData::None;
            RW_ERROR("Unhandled blip type");
            break;
    }
    data.target = object->getScriptObjectID();
    data.display = BlipData::ShowBoth;
    data.texture = "";
    data.size = 3;
    auto blip = args.getState()->addRadarBlip(data);
    return args.getState()->radarBlips[blip];
}

inline BlipData createObjectBlipSprite(const ScriptArguments& args,
                                       GameObject* object, int sprite) {
    auto& data = script::createObjectBlip(args, object);
    data.texture = getBlipSprite(sprite);
    return data;
}

ScriptModel getModel(const ScriptArguments& args, ScriptModel model);

inline void addObjectToMissionCleanup(const ScriptArguments& args,
                                      GameObject* object) {
    if (args.getThread()->isMission) {
        /// @todo verify if the mission object list should be kept on a
        /// per-thread basis?
        /// husho: mission object list is one for all threads
        args.getState()->missionObjects.push_back(object);
    }
}

inline void removeObjectFromMissionCleanup(const ScriptArguments& args,
                                      GameObject* object) {
    if (args.getThread()->isMission) {
        auto& mo = args.getState()->missionObjects;
        mo.erase(std::remove(mo.begin(), mo.end(), object), mo.end());
    }
}
        
inline void getClosestNode(const ScriptArguments& args, ScriptVec3& coord, AIGraphNode::NodeType type,
                           ScriptFloat& xCoord, ScriptFloat& yCoord, ScriptFloat& zCoord) {
    coord = script::getGround(args, coord);
    float closest = 10000.f;
    std::vector<AIGraphNode*> nodes;
    args.getWorld()->aigraph.gatherExternalNodesNear(coord, closest, nodes, type);

    for (const auto &node : nodes) {
        // This is how the original game calculates distance,
        // weighted manhattan-distance where the vertical distance
        // has to be 3x as close to be considered.
        const float dist = std::abs(coord.x - node->position.x) +
                           std::abs(coord.y - node->position.y) +
                           std::abs(coord.z - node->position.z) * 3.f;
        if (dist < closest) {
            closest = dist;
            xCoord = node->position.x;
            yCoord = node->position.y;
            zCoord = node->position.z;
        }
    }
}
}

#endif
