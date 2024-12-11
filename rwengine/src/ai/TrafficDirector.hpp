#ifndef _RWENGINE_TRAFFICDIRECTOR_HPP_
#define _RWENGINE_TRAFFICDIRECTOR_HPP_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class GameWorld;
class GameObject;
class ViewCamera;

namespace ai {

enum class NodeType;
class AIGraph;
struct AIGraphNode;

class TrafficDirector {
public:
    TrafficDirector(AIGraph* graph, GameWorld* world);

    std::vector<AIGraphNode*> findAvailableNodes(NodeType type,
                                                 const ViewCamera& camera,
                                                 float radius);

    void setDensity(NodeType type, float density);

    /**
     * Put the right pedestrian inside the car
     */
    uint16_t assignDriver(const std::string &vehiclename);

    /**
     * Creates new traffic at available locations.
     * @param camera The camera to spawn around
     * @param radius the maximum distance to spawn in
     * @param max The maximum number of traffic to create.
     */
    std::vector<GameObject*> populateNearby(const ViewCamera& camera,
                                            float radius, int maxSpawn = -1);

    /**
     * Sets the maximum number of pedestrians and cars in the traffic system
     */
    void setPopulationLimits(int maxPeds, int maxCars);

private:
    AIGraph* graph = nullptr;
    GameWorld* world = nullptr;
    float pedDensity = 1.f;
    float carDensity = 1.f;
    size_t maximumPedestrians = 20;
    size_t maximumCars = 10;
};

}  // namespace ai

#endif
