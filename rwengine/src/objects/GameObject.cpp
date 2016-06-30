#include <objects/GameObject.hpp>
#include <loaders/LoaderIFP.hpp>
#include <loaders/LoaderDFF.hpp>
#include <engine/Animator.hpp>
#include <data/Skeleton.hpp>
#include <glm/gtc/quaternion.hpp>

GameObject::~GameObject()
{
	if(animator)
	{
		delete animator;
	}
	if(skeleton)
	{
		delete skeleton;
	}
}


void GameObject::setPosition(const glm::vec3& pos)
{
	_lastPosition = position = pos;
}

void GameObject::setRotation(const glm::quat& orientation)
{
	rotation = orientation;
}

float GameObject::getHeading() const
{
  auto hdg = glm::roll(getRotation());
	return hdg / glm::pi<float>() * 180.f;
}

void GameObject::setHeading(float heading)
{
	auto hdg = (heading / 180.f) * glm::pi<float>();
	auto quat = glm::normalize(glm::quat(glm::vec3(0.f, 0.f, hdg)));
	setRotation(quat);
}
