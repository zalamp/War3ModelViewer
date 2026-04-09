#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QGridLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QDir>
#include <QVector>
#include <QFuture>
#include <QFutureWatcher>
#include "modelviewer.h"
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPropertyAnimation>
#include <QPainter>
#include <QProcess>

// ==================== 全局常量定义 ====================
namespace Constants {
// 服务相关
const int SERVICE_PORT = 3737;                // Node服务端口
const QString SERVICE_HEALTH_PATH = "health"; // 健康检查路径
const int SERVICE_HEALTH_TIMEOUT = 3000;      // 健康检查超时(ms)

// UI相关
const int IOS_SWITCH_WIDTH = 50;              // iOS开关宽度
const int IOS_SWITCH_HEIGHT = 28;             // iOS开关高度
const int IOS_SWITCH_ANIM_DURATION = 150;     // 开关动画时长(ms)
const int MODEL_ITEM_SIZE = 200;              // 模型预览控件尺寸
const int GRID_SPACING = 5;                   // 网格布局间距
const int MIN_WINDOW_WIDTH = 1200;            // 窗口最小宽度
const int MIN_WINDOW_HEIGHT = 800;            // 窗口最小高度
const int PROGRESS_BAR_WIDTH = 200;           // 进度条宽度

// 分页相关
const int DEFAULT_COLUMNS = 5;                // 默认列数
const int ROWS_PER_PAGE = 4;                  // 每页行数

// 配置相关
const QString ORG_NAME = "War3ModelTools";    // 配置组织名
const QString APP_NAME = "War3ModelBatchViewer"; // 配置应用名
const QString NATIVE_TEXTURE_DIR_KEY = "NativeTexture/Path";    // 原生贴图文件夹路径配置键
}

// iOS风格开关按钮（无额外尺寸改动，优化DPI适配）
class IOSSwitch : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(int animationOffset READ animationOffset WRITE setAnimationOffset)
public:
    explicit IOSSwitch(QWidget *parent = nullptr);
    void setChecked(bool checked);
    bool isChecked() const { return m_checked; }
    int animationOffset() const { return m_animationOffset; }
    void setAnimationOffset(int offset);

signals:
    void toggled(bool checked);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateAnimation();
    bool m_checked = false;
    int m_animationOffset = 0;
    QPropertyAnimation *m_animation = nullptr;
};

struct LoadResult {
    QString fileName;
    QString filePath;          // 新增：完整路径，便于调试
    ModelData modelData;
    QString error;
    bool success;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onOpenFolderClicked();
    void onClearClicked();
    void onOpenSettings();
    void onModelLoaded(int index);
    void onAllModelsLoaded();
    void onPrevPage();
    void onNextPage();
    void clearLayout();
    void clearAllModels();
    void onAnimationSwitchToggled(bool checked);
    void onServiceHealthChecked(QNetworkReply* reply); // 新增：异步健康检查

private:
    // 初始化相关
    void initUI();
    void initConnections();    // 拆分：信号槽连接
    void createGridLayout();
    void createMenuBar();      // 拆分：菜单栏创建
    void createButtonBar();    // 拆分：按钮栏创建

    // 业务逻辑相关
    void updateStatusBar(const QString& message, int progress = -1);
    void checkServiceHealthAsync(); // 优化：异步健康检查
    QString getDefaultWar3Path();
    bool validateWar3Path(const QString& path); // 新增：验证War3路径有效性
    void startNodeServiceSilently();

    // 布局与分页相关
    void updateColumnCount();
    void updatePage();
    int calculateTotalPages() const; // 封装：总页数计算
    void updatePaginationButtons();  // 封装：分页按钮状态更新

    // 模型加载相关
    void loadModelsFromFolder(const QString& folderPath); // 拆分：模型加载逻辑
    LoadResult loadModelAsync(const QString& filePath, const QString& war3Path); // 移为成员函数

    // 成员变量
    QWidget* m_centralWidget = nullptr;
    QGridLayout* m_gridLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_scrollContent = nullptr;

    // 按钮栏控件
    QPushButton* m_openFolderBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QPushButton* m_prevBtn = nullptr;
    QPushButton* m_nextBtn = nullptr;
    QLabel* m_pageLabel = nullptr;
    IOSSwitch* m_animationSwitch = nullptr;
    QLabel* m_animLabel = nullptr;

    // 状态栏控件
    QLabel* m_statusLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;

    // 状态变量
    int m_columns = Constants::DEFAULT_COLUMNS;
    int m_pageSize = Constants::DEFAULT_COLUMNS * Constants::ROWS_PER_PAGE;
    int m_currentPage = 0;
    int m_loadedCount = 0;
    int m_totalCount = 0;
    bool m_isLoading = false; // 新增：加载状态锁，防止重复加载

    // 数据容器
    QVector<QFutureWatcher<LoadResult>*> m_loadWatchers;
    QVector<ModelViewer*> m_allViewers;

    // 网络相关
    QNetworkAccessManager* m_networkManager = nullptr;
};

#endif // MAINWINDOW_H
