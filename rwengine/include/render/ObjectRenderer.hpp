#ifndef _RWENGINE_OBJECTRENDERER_HPP_
#define _RWENGINE_OBJECTRENDERER_HPP_

#include <glm/glm.hpp>
#include <rw/types.hpp>
#include <render/ViewCamera.hpp>
#include <render/OpenGLRenderer.hpp>
#include <objects/GameObject.hpp>
#include <engine/GameWorld.hpp>
#include <gl/DrawBuffer.hpp>

class ProjectileObject;
class PickupObject;

/**
 * @brief The ObjectRenderer class handles object -> renderer transformation
 *
 * Determines what parts of an object are within a camera frustum and exports
 * a list of things to render for the object.
 */
class ObjectRenderer
{
public:
	ObjectRenderer(GameWorld* world,
				   const ViewCamera& camera,
				   float renderAlpha,
				   GLuint errorTexture)
		: m_world (world)
		, m_camera(camera)
		, m_renderAlpha(renderAlpha)
		, m_errorTexture(errorTexture)
	{ }

	/**
	 * @brief buildRenderList
	 *
	 * Exports rendering instructions for an object
	 */
	void buildRenderList(GameObject* object, RenderList& outList);

private:
	GameWorld* m_world;
	const ViewCamera& m_camera;
	float m_renderAlpha;
	GLuint m_errorTexture;

	void renderInstance(InstanceObject *instance, RenderList& outList);
	void renderCharacter(CharacterObject *pedestrian, RenderList& outList);
	void renderVehicle(VehicleObject *vehicle, RenderList& outList);
	void renderPickup(PickupObject *pickup, RenderList& outList);
	void renderCutsceneObject(CutsceneObject *cutscene, RenderList& outList);
	void renderProjectile(ProjectileObject *projectile, RenderList& outList);

	void renderItem(InventoryItem *item,
					const glm::mat4 &modelMatrix,
					RenderList& outList);
	void renderWheel(VehicleObject* vehicle,
					 Model* model,
					 const glm::mat4 &matrix,
					 const std::string& name,
					 RenderList& outList);

	bool renderFrame(Model* m,
					 ModelFrame* f,
					 const glm::mat4& matrix,
					 GameObject* object,
					 float opacity,
					 RenderList& outList);

	void renderGeometry(Model* model,
						size_t g,
						const glm::mat4& modelMatrix,
						float opacity,
						GameObject* object,
						RenderList& outList);
};

#endif
