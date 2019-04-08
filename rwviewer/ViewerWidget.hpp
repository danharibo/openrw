#ifndef _RWVIEWER_VIEWERWIDGET_HPP_
#define _RWVIEWER_VIEWERWIDGET_HPP_
#include <QTimer>
#include <data/Clump.hpp>
#include <engine/GameData.hpp>
#include <engine/GameWorld.hpp>
#include <gl/DrawBuffer.hpp>
#include <gl/GeometryBuffer.hpp>
#include <loaders/LoaderIFP.hpp>
#include <render/TextRenderer.hpp>
#include <glm/glm.hpp>

#include <QWindow>

class Clump;
class LegacyGameRenderer;
class QOpenGLContextWrapper;

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
        //! View text strings, \see showText
        Text,
    };

    ViewerWidget(QOpenGLContextWrapper* context, QWindow* parent);

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
    void clearText();
    void showText(const TextRenderer::TextInfo &ti);
    void selectFrame(ModelFrame* frame);
    void exportModel();

    void gameLoaded(GameWorld* world, LegacyGameRenderer* renderer);

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

    QOpenGLContextWrapper* m_context;
    GameWorld* _world = nullptr;
    LegacyGameRenderer* _renderer = nullptr;

    std::vector<TextRenderer::TextInfo> textInfos;
    ClumpPtr _model;
    ModelFrame* selectedFrame = nullptr;
    GameObject* _object = nullptr;
    quint16 _objectID = 0;


    float viewDistance = 1.f;
    glm::vec2 viewAngles{};
    glm::vec3 viewPosition{};

    bool dragging = false;
    QPointF dstart;
    glm::vec2 dastart{};
    bool moveFast = false;

    DrawBuffer* _frameWidgetDraw = nullptr;
    GeometryBuffer* _frameWidgetGeom = nullptr;
    GLuint whiteTex;

    void drawFrameWidget(ModelFrame* f, const glm::mat4& = glm::mat4(1.f));
    bool initialised = false;

    void drawModel(LegacyGameRenderer& r, ClumpPtr& model);
    void drawObject(LegacyGameRenderer& r, GameObject* object);
    void drawWorld(LegacyGameRenderer& r);
    void drawText(LegacyGameRenderer& r);

};

#endif
