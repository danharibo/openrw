#include "ViewerWindow.hpp"
#include <ViewerWidget.hpp>
#include "views/ModelViewer.hpp"
#include "views/ObjectViewer.hpp"
#include "views/WorldViewer.hpp"

#include <engine/GameState.hpp>
#include <engine/GameWorld.hpp>
#include <render/GameRenderer.hpp>

#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QMenuBar>
#include <QOffscreenSurface>
#include <QPushButton>
#include <QSettings>
#include <QSignalMapper>
#include <fstream>

static int MaxRecentGames = 5;

ViewerWindow::ViewerWindow(QWidget* parent, Qt::WindowFlags flags)
    : QMainWindow(parent, flags)
    , gameData(nullptr)
    , gameWorld(nullptr)
    , renderer(nullptr) {
    setMinimumSize(640, 480);

    QMenuBar* mb = this->menuBar();
    QMenu* file = mb->addMenu("&File");

    file->addAction("Open &Game", this, SLOT(loadGame()));

    file->addSeparator();
    for (int i = 0; i < MaxRecentGames; ++i) {
        QAction* r = file->addAction("");
        recentGames.append(r);
        connect(r, SIGNAL(triggered()), SLOT(openRecent()));
    }

    recentSep = file->addSeparator();
    auto ex = file->addAction("E&xit");
    ex->setShortcut(QKeySequence::Quit);
    connect(ex, SIGNAL(triggered()), QApplication::instance(),
            SLOT(closeAllWindows()));

    //----------------------- View Mode setup

    QGLFormat glFormat;
    glFormat.setVersion(3, 3);
    glFormat.setProfile(QGLFormat::CoreProfile);

    viewerWidget = new ViewerWidget(glFormat);
    viewerWidget->context()->makeCurrent();
    connect(this, SIGNAL(loadedData(GameWorld*)), viewerWidget,
            SLOT(dataLoaded(GameWorld*)));

    //------------- Object Viewer
    m_views[ViewMode::Object] = new ObjectViewer(viewerWidget);
    m_viewNames[ViewMode::Object] = "Objects";

    //------------- Model Viewer
    m_views[ViewMode::Model] = new ModelViewer(viewerWidget);
    m_viewNames[ViewMode::Model] = "Model";

    //------------- World Viewer
    m_views[ViewMode::World] = new WorldViewer(viewerWidget);
    m_viewNames[ViewMode::World] = "World";

    //------------- display mode switching
    viewSwitcher = new QStackedWidget;
    auto signalMapper = new QSignalMapper(this);
    auto switchPanel = new QVBoxLayout();
    int i = 0;
    for (auto viewer : m_views) {
        viewSwitcher->addWidget(viewer);
        connect(this, SIGNAL(loadedData(GameWorld*)), viewer,
                SLOT(showData(GameWorld*)));

        auto viewerButton = new QPushButton(m_viewNames[i].c_str());
        signalMapper->setMapping(m_views[i], i);
        signalMapper->setMapping(viewerButton, i);
        connect(viewerButton, SIGNAL(clicked()), signalMapper, SLOT(map()));
        switchPanel->addWidget(viewerButton);
        i++;
    }
    // Map world viewer loading placements to switch to the world viewer
    connect(m_views[ViewMode::World], SIGNAL(placementsLoaded(QString)),
            signalMapper, SLOT(map()));

    switchView(ViewMode::Object);

    connect(m_views[ViewMode::Object], SIGNAL(showObjectModel(uint16_t)), this,
            SLOT(showObjectModel(uint16_t)));
    connect(m_views[ViewMode::Object], SIGNAL(showObjectModel(uint16_t)),
            m_views[ViewMode::Model], SLOT(showObject(uint16_t)));
    connect(this, SIGNAL(loadAnimations(QString)), m_views[ViewMode::Model],
            SLOT(loadAnimations(QString)));

    connect(signalMapper, SIGNAL(mapped(int)), this, SLOT(switchView(int)));
    connect(signalMapper, SIGNAL(mapped(int)), viewSwitcher,
            SLOT(setCurrentIndex(int)));

    switchPanel->addStretch();
    auto mainlayout = new QHBoxLayout();
    mainlayout->addLayout(switchPanel);
    mainlayout->addWidget(viewSwitcher);
    auto mainwidget = new QWidget();
    mainwidget->setLayout(mainlayout);

    mb->addMenu("&Data");

    QMenu* anim = mb->addMenu("&Animation");
    anim->addAction("Load &Animations", this, SLOT(openAnimations()));

    QMenu* map = mb->addMenu("&Map");
    map->addAction("Load IPL", m_views[ViewMode::World],
                   SLOT(loadPlacements()));

    this->setCentralWidget(mainwidget);

    updateRecentGames();
}

void ViewerWindow::showEvent(QShowEvent*) {
    static bool first = true;
    if (first) {
        QSettings settings("OpenRW", "rwviewer");
        restoreGeometry(settings.value("window/geometry").toByteArray());
        restoreState(settings.value("window/windowState").toByteArray());
        first = false;
    }
}

void ViewerWindow::closeEvent(QCloseEvent* event) {
    QSettings settings("OpenRW", "rwviewer");
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/windowState", saveState());
    QMainWindow::closeEvent(event);
}

void ViewerWindow::openAnimations() {
    QFileDialog dialog(this, "Open Animations", QDir::homePath(),
                       "IFP Animations (*.ifp)");
    if (dialog.exec()) {
        loadAnimations(dialog.selectedFiles()[0]);
    }
}

void ViewerWindow::loadGame() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("Open Directory"), QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (dir.size() > 0) loadGame(dir);
}

void ViewerWindow::loadGame(const QString& path) {
    QDir gameDir(path);

    if (gameDir.exists() && path.size() > 0) {
        gameData =
            new GameData(&engineLog, gameDir.absolutePath().toStdString());
        gameWorld = new GameWorld(&engineLog, gameData);
        renderer = new GameRenderer(&engineLog, gameData);
        gameWorld->state = new GameState;
        viewerWidget->setRenderer(renderer);

        gameWorld->data->load();

        loadedData(gameWorld);
    }

    QSettings settings("OpenRW", "rwviewer");
    QStringList recent = settings.value("recentGames").toStringList();
    recent.removeAll(path);
    recent.prepend(path);
    while (recent.size() > MaxRecentGames) recent.removeLast();
    settings.setValue("recentGames", recent);

    updateRecentGames();
}

void ViewerWindow::openRecent() {
    QAction* r = qobject_cast<QAction*>(sender());
    if (r) {
        loadGame(r->data().toString());
    }
}

void ViewerWindow::switchView(int mode) {
    if (mode < int(m_views.size())) {
        m_views[mode]->setViewerWidget(viewerWidget);
    } else {
        RW_ERROR("Unhandled view mode" << mode);
    }
}

void ViewerWindow::showObjectModel(uint16_t) {
    // Switch to the model viewer
    switchView(ViewMode::Model);
    viewSwitcher->setCurrentIndex(
        viewSwitcher->indexOf(m_views[ViewMode::Model]));
}

void ViewerWindow::updateRecentGames() {
    QSettings settings("OpenRW", "rwviewer");
    QStringList recent = settings.value("recentGames").toStringList();

    for (int i = 0; i < MaxRecentGames; ++i) {
        if (i < recent.size()) {
            QString fnm(QFileInfo(recent[i]).fileName());
            recentGames[i]->setText(tr("&%1 - %2").arg(i).arg(fnm));
            recentGames[i]->setData(recent[i]);
            recentGames[i]->setVisible(true);
        } else {
            recentGames[i]->setVisible(false);
        }
    }

    recentSep->setVisible(recent.size() > 0);
}
