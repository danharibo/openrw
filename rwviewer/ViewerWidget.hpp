#ifndef _RWVIEWER_VIEWERWIDGET_HPP_
#define _RWVIEWER_VIEWERWIDGET_HPP_
#include <QTimer>
#include <data/Clump.hpp>
#include <engine/GameData.hpp>
#include <engine/GameWorld.hpp>
#include <gl/DrawBuffer.hpp>
#include <gl/GeometryBuffer.hpp>
#include <glm/glm.hpp>
#include <loaders/LoaderIFP.hpp>

// Prevent Qt from conflicting with glLoadGen
#define GL_ARB_debug_output
#define GL_KHR_debug
#include <QOpenGLWindow>

class GameRenderer;
class Clump;
class ViewerWidget : public QWindow {
    Q_OBJECT
public:
    enum class Mode {
        //! View an Object, \see showObject
        Object,
        //! View a DFF model, \see showModel
        Model,
        //! View loaded instances, \see showWorld();
        World,
    };

    ViewerWidget(QOpenGLContext* context, QWindow* parent);

    void initGL();
    void paintGL();

    void renderNow();
    bool event(QEvent*) override;

    void exposeEvent(QExposeEvent*) override;

    ClumpPtr currentModel() const;
    GameObject* currentObject() const;

    GameWorld* world();

    void setMode(Mode m) {
        _viewMode = m;
    }

    Mode currentMode() const {
        return _viewMode;
    }

public slots:
    void showObject(quint16 item);
    void showModel(ClumpPtr model);
    void selectFrame(ModelFrame* frame);
    void exportModel();

    void gameLoaded(GameWorld* world, GameRenderer* renderer);

signals:
    void fileOpened(const QString& file);

    void modelChanged(ClumpPtr model);

protected:
    void keyPressEvent(QKeyEvent*) override;
    void keyReleaseEvent(QKeyEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

    Mode _viewMode = Mode::World;

    QOpenGLContext* context;
    GameWorld* _world = nullptr;
    GameRenderer* _renderer = nullptr;

    ClumpPtr _model;
    ModelFrame* selectedFrame = nullptr;
    GameObject* _object = nullptr;
    quint16 _objectID = 0;


    float viewDistance;
    glm::vec2 viewAngles{};
    glm::vec3 viewPosition{};

    bool dragging;
    QPointF dstart;
    glm::vec2 dastart{};
    bool moveFast;

    DrawBuffer* _frameWidgetDraw;
    GeometryBuffer* _frameWidgetGeom;
    GLuint whiteTex;

    void drawFrameWidget(ModelFrame* f, const glm::mat4& = glm::mat4(1.f));
    bool initialised = false;

    void drawModel(GameRenderer& r, ClumpPtr& model);
    void drawObject(GameRenderer& r, GameObject* object);
    void drawWorld(GameRenderer& r);

};

#endif
