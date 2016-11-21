#include <data/Skeleton.hpp>
#include <engine/GameWorld.hpp>
#include <items/Weapon.hpp>
#include <objects/ProjectileObject.hpp>

void Weapon::fireHitscan(WeaponData* weapon, CharacterObject* owner) {
    auto handFrame = owner->getModel()->findFrame("srhand");
    glm::mat4 handMatrix;
    if (handFrame) {
        while (handFrame->getParent()) {
            handMatrix =
                owner->skeleton->getMatrix(handFrame->getIndex()) * handMatrix;
            handFrame = handFrame->getParent();
        }
    }

    auto farTarget =
        owner->getPosition() +
        owner->getRotation() * glm::vec3(0.f, weapon->hitRange, 0.f);
    auto handPos = glm::vec3(handMatrix * glm::vec4(0.f, 0.f, 0.f, 1.f));
    auto fireOrigin = owner->getPosition() + owner->getRotation() * handPos;

    owner->engine->doWeaponScan(
        WeaponScan(weapon->damage, fireOrigin, farTarget, weapon));
}

void Weapon::fireProjectile(WeaponData* weapon, CharacterObject* owner,
                            float force) {
    auto handPos = glm::vec3(0.f, 1.5f, 1.f);
    auto fireOrigin = owner->getPosition() + owner->getRotation() * handPos;
    auto direction =
        owner->getRotation() * glm::normalize(glm::vec3{0.f, 1.f, 1.f});

    auto pt = weapon->name == "grenade" ? ProjectileObject::Grenade
                                        : ProjectileObject::Molotov;

    force = std::max(0.1f, force);

    auto projectile = new ProjectileObject(
        owner->engine, fireOrigin,
        {pt, direction,
         17.f * force,  /// @todo pull a better velocity from somewhere
         3.5f, weapon});

    auto& pool = owner->engine->getTypeObjectPool(projectile);
    pool.insert(projectile);
    owner->engine->allObjects.push_back(projectile);
}
