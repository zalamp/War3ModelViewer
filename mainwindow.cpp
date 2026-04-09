#include "mainwindow.h"
#include "settingsdialog.h"
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileInfoList>
#include <QtConcurrent/QtConcurrent>
#include <QStatusBar>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScrollBar>
#include <QTimer>
#include <QSettings>
#include <QDir>
#include <QResizeEvent>
#include <QApplication>
#include <QScreen>
#include <QProcess>
#include <QElapsedTimer>
#include <exception>

// ==================== IOSSwitch 实现（优化DPI适配） ====================
IOSSwitch::IOSSwitch(QWidget *parent) : QPushButton(parent) {
    setCheckable(false);
    // 适配高DPI
    qreal dpr = qApp->primaryScreen()->devicePixelRatio();
    int width = Constants::IOS_SWITCH_WIDTH * dpr;
    int height = Constants::IOS_SWITCH_HEIGHT * dpr;
    setFixedSize(width / dpr, height / dpr);

    m_animation = new QPropertyAnimation(this, "animationOffset", this);
    m_animation->setDuration(Constants::IOS_SWITCH_ANIM_DURATION);
    m_animation->setEasingCurve(QEasingCurve::InOutQuad);
    connect(m_animation, &QPropertyAnimation::valueChanged, this, [this](){ update(); });
    setChecked(false);
}

void IOSSwitch::setChecked(bool checked) {
    if (m_checked == checked) return;
    m_checked = checked;
    updateAnimation();
    emit toggled(m_checked);
}

void IOSSwitch::setAnimationOffset(int offset) {
    m_animationOffset = offset;
    update();
}

void IOSSwitch::updateAnimation() {
    if (!m_animation) return;
    m_animation->stop();
    m_animation->setEndValue(m_checked ? width() - height() : 0);
    m_animation->start();
}

void IOSSwitch::paintEvent(QPaintEvent *event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // 适配高DPI的绘图
    QPaintDevice* device = painter.device();
    qreal dpr = device->devicePixelRatio();

    // 绘制背景
    QRectF bgRect(0, 0, width() * dpr, height() * dpr);
    QColor bgColor = m_checked ? QColor(52, 120, 246) : QColor(200, 200, 200);
    painter.setBrush(bgColor);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(bgRect, bgRect.height()/2, bgRect.height()/2);

    // 绘制滑块
    int sliderSize = height() * dpr - 4;
    int x = m_animationOffset * dpr + 2;
    painter.setBrush(QColor(255, 255, 255));
    painter.drawEllipse(x, 2, sliderSize, sliderSize);
}

void IOSSwitch::mouseReleaseEvent(QMouseEvent *event) {
    QPushButton::mouseReleaseEvent(event);
    setChecked(!m_checked);
}

// ==================== MainWindow 实现 ====================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    m_networkManager = new QNetworkAccessManager(this);
    m_isLoading = false;

    initUI();
    initConnections();
    checkServiceHealthAsync(); // 异步检查服务健康状态
    startNodeServiceSilently();
}

MainWindow::~MainWindow()
{
    clearAllModels();
    // 兜底清理所有Watcher
    for (auto watcher : qAsConst(m_loadWatchers)) {
        if (watcher->future().isRunning()) {
            watcher->cancel();
        }
        watcher->deleteLater();
    }
    m_loadWatchers.clear();
}

// ==================== 初始化相关 ====================
void MainWindow::initUI() {
    setWindowTitle(QString("%1 v1.0").arg(Constants::APP_NAME));
    setMinimumSize(Constants::MIN_WINDOW_WIDTH, Constants::MIN_WINDOW_HEIGHT);

    // 中心控件
    m_centralWidget = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(m_centralWidget);
    mainLayout->setSpacing(Constants::GRID_SPACING);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // 创建按钮栏、菜单栏、网格布局
    createMenuBar();
    createButtonBar();
    createGridLayout();

    // 滚动区域
    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setWidget(m_scrollContent);
    mainLayout->addWidget(m_scrollArea);

    // 状态栏
    m_statusLabel = new QLabel("就绪", this);
    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumWidth(Constants::PROGRESS_BAR_WIDTH);
    statusBar()->addWidget(m_statusLabel, 1);
    statusBar()->addPermanentWidget(m_progressBar);

    setCentralWidget(m_centralWidget);
}

void MainWindow::createMenuBar() {
    QMenuBar *menuBar = this->menuBar();
    QMenu *toolsMenu = menuBar->addMenu(tr("工具(&T)"));
    QAction *settingsAction = toolsMenu->addAction(tr("设置(&S)..."));
    connect(settingsAction, &QAction::triggered, this, &MainWindow::onOpenSettings);
}

void MainWindow::createButtonBar() {
    QWidget* buttonBar = new QWidget(this);
    QHBoxLayout* buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setSpacing(10);
    buttonLayout->setContentsMargins(5, 5, 5, 5);

    // 功能按钮
    m_openFolderBtn = new QPushButton("📂 选择模型文件夹", this);
    m_clearBtn = new QPushButton("🗑️ 清空所有", this);
    m_openFolderBtn->setMinimumHeight(35);
    m_clearBtn->setMinimumHeight(35);

    // 动画开关
    m_animLabel = new QLabel("动画", this);
    m_animLabel->setStyleSheet("color: #333;");
    m_animationSwitch = new IOSSwitch(this);

    // 分页按钮
    m_prevBtn = new QPushButton("◀ 上一页", this);
    m_nextBtn = new QPushButton("下一页 ▶", this);
    m_pageLabel = new QLabel("0 / 0", this);
    m_prevBtn->setMinimumHeight(35);
    m_nextBtn->setMinimumHeight(35);
    m_prevBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);

    // 布局组装
    buttonLayout->addWidget(m_openFolderBtn);
    buttonLayout->addWidget(m_clearBtn);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_animLabel);
    buttonLayout->addWidget(m_animationSwitch);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_prevBtn);
    buttonLayout->addWidget(m_pageLabel);
    buttonLayout->addWidget(m_nextBtn);

    // 添加到主布局
    QVBoxLayout* mainLayout = qobject_cast<QVBoxLayout*>(m_centralWidget->layout());
    if (mainLayout) {
        mainLayout->addWidget(buttonBar);
    }
}

void MainWindow::initConnections() {
    // 使用UniqueConnection防止重复连接
    connect(m_openFolderBtn, &QPushButton::clicked, this, &MainWindow::onOpenFolderClicked, Qt::UniqueConnection);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClearClicked, Qt::UniqueConnection);
    connect(m_prevBtn, &QPushButton::clicked, this, &MainWindow::onPrevPage, Qt::UniqueConnection);
    connect(m_nextBtn, &QPushButton::clicked, this, &MainWindow::onNextPage, Qt::UniqueConnection);
    connect(m_animationSwitch, &IOSSwitch::toggled, this, &MainWindow::onAnimationSwitchToggled, Qt::UniqueConnection);
}

void MainWindow::createGridLayout() {
    m_scrollContent = new QWidget(this);
    m_gridLayout = new QGridLayout(m_scrollContent);
    m_gridLayout->setSpacing(Constants::GRID_SPACING);
    m_gridLayout->setContentsMargins(Constants::GRID_SPACING, Constants::GRID_SPACING,
                                     Constants::GRID_SPACING, Constants::GRID_SPACING);
    m_gridLayout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
}

// ==================== 业务逻辑相关 ====================
void MainWindow::updateStatusBar(const QString& message, int progress) {
    m_statusLabel->setText(message);
    if (progress >= 0 && progress <= 100) {
        m_progressBar->setValue(progress);
        m_progressBar->setVisible(true);
    } else {
        m_progressBar->setVisible(false);
    }
}

void MainWindow::checkServiceHealthAsync() {
    QUrl serviceUrl(QString("http://127.0.0.1:%1/%2").arg(Constants::SERVICE_PORT)
                        .arg(Constants::SERVICE_HEALTH_PATH));
    QNetworkRequest request(serviceUrl);
    QNetworkReply* reply = m_networkManager->get(request);
    QTimer::singleShot(Constants::SERVICE_HEALTH_TIMEOUT, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort(); // 超时终止请求
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onServiceHealthChecked(reply);
    });
}

void MainWindow::onServiceHealthChecked(QNetworkReply* reply) {
    if (reply->error() != QNetworkReply::NoError) {
        updateStatusBar(QString("Node.js服务未连接：%1").arg(reply->errorString()));
        QMessageBox::warning(this, "警告",
                             "未检测到Node.js解析服务，请确保service/start.bat已运行！\n"
                             "否则模型将无法正常解析。");
    } else {
        QByteArray responseData = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(responseData);
        if (doc.isObject() && doc.object()["status"].toString() == "ok") {
            updateStatusBar("Node.js服务连接正常");
        } else {
            updateStatusBar("Node.js服务响应异常");
        }
    }
    reply->deleteLater();
}

QString MainWindow::getDefaultWar3Path() {
#ifdef Q_OS_WIN
    QStringList possiblePaths = {
        "C:/Program Files/Warcraft III",
        "C:/Program Files (x86)/Warcraft III",
        "C:/Warcraft III",
        QDir::homePath() + "/Warcraft III",
        "D:/Warcraft III",
        "E:/Warcraft III"
    };

    for (const QString& path : possiblePaths) {
        if (validateWar3Path(path)) {
            return path;
        }
    }
#endif
    return "";
}

bool MainWindow::validateWar3Path(const QString& path) {
    if (path.isEmpty()) return false;
    QDir war3Dir(path);
    // 验证关键文件存在，避免误判空文件夹
    QString war3Exe = war3Dir.filePath("War3.exe");
    QString dataDir = war3Dir.filePath("Data");
    return QFile::exists(war3Exe) && QDir(dataDir).exists();
}

void MainWindow::startNodeServiceSilently() {
#ifdef Q_OS_WIN
    QTcpSocket testSocket;
    testSocket.connectToHost("127.0.0.1", 3737);
    if (testSocket.waitForConnected(500)) {
        // 端口已被占用，说明服务已在运行
        testSocket.disconnectFromHost();
        return;  // 不重复启动
    }
    QDir appDir(QCoreApplication::applicationDirPath());
    // 最多向上查找5层目录
    for (int i = 0; i < 5; ++i) {
        QString serviceDir = appDir.filePath("service");
        QString batPath = QDir(serviceDir).filePath("start.bat");
        if (QFile::exists(batPath)) {
            QStringList args;
            args << "/c" << "start" << "" << batPath;
            bool started = QProcess::startDetached("cmd", args, serviceDir);
            // 静默启动bat（隐藏控制台窗口）
            // QProcess::startDetached(batPath, QStringList(), serviceDir);
            if (started) {
                // 延迟2秒后异步检查服务健康状态（可选）
                QTimer::singleShot(2000, this, &MainWindow::checkServiceHealthAsync);
            }
            // QTimer::singleShot(2000, this, &MainWindow::checkServiceHealthAsync); // 延迟检查
            return;
        }
        if (!appDir.cdUp()) break;
    }
    updateStatusBar("未找到Node.js服务启动脚本(service/start.bat)");
#endif
}

// ==================== 布局与分页相关 ====================
void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    // 加载中禁止调整布局，避免异常
    if (!m_isLoading && !m_allViewers.isEmpty()) {
        updateColumnCount();
        updatePage();
    }
}

void MainWindow::updateColumnCount() {
    // 计算可用宽度（扣除滚动条宽度）
    int scrollBarWidth = m_scrollArea->verticalScrollBar()->width();
    int availableWidth = m_scrollArea->viewport()->width() - 20 - scrollBarWidth;
    int itemTotalWidth = Constants::MODEL_ITEM_SIZE + m_gridLayout->spacing();
    int newColumns = availableWidth / itemTotalWidth;
    newColumns = qMax(1, newColumns);

    if (newColumns != m_columns) {
        m_columns = newColumns;
        m_pageSize = m_columns * Constants::ROWS_PER_PAGE;
    }
}

int MainWindow::calculateTotalPages() const {
    if (m_pageSize <= 0 || m_allViewers.isEmpty()) return 0;
    return (m_allViewers.size() + m_pageSize - 1) / m_pageSize;
}

void MainWindow::updatePaginationButtons() {
    int totalPages = calculateTotalPages();
    m_prevBtn->setEnabled(m_currentPage > 0 && totalPages > 0);
    m_nextBtn->setEnabled((m_currentPage + 1) < totalPages && totalPages > 0);
    m_pageLabel->setText(QString("%1 / %2").arg(m_currentPage + 1).arg(totalPages));
}

void MainWindow::updatePage() {
    clearLayout();
    int totalPages = calculateTotalPages();
    if (totalPages == 0) {
        updatePaginationButtons();
        return;
    }

    // 计算当前页的模型范围
    int start = m_currentPage * m_pageSize;
    int end = qMin(start + m_pageSize, m_allViewers.size());

    // 填充当前页模型
    for (int i = start; i < end; ++i) {
        ModelViewer* viewer = m_allViewers[i];
        int row = (i - start) / m_columns;
        int col = (i - start) % m_columns;
        m_gridLayout->addWidget(viewer, row, col, 1, 1, Qt::AlignLeft | Qt::AlignTop);
        viewer->show();
    }

    updatePaginationButtons();
}

// ==================== 模型加载相关 ====================
void MainWindow::onOpenFolderClicked() {
    if (m_isLoading) {
        QMessageBox::information(this, "提示", "正在加载模型，请稍候...");
        return;
    }

    QString folderPath = QFileDialog::getExistingDirectory(this,
                                                           "选择包含MDX/MDL文件的文件夹",
                                                           QDir::homePath());
    if (folderPath.isEmpty()) return;

    loadModelsFromFolder(folderPath);
}

void MainWindow::loadModelsFromFolder(const QString& folderPath) {
    // 清空旧数据
    clearAllModels();

    // 筛选模型文件
    QDir dir(folderPath);
    QStringList filters = {"*.mdx", "*.MDX", "*.mdl", "*.MDL"};
    dir.setNameFilters(filters);
    QFileInfoList fileList = dir.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);

    if (fileList.isEmpty()) {
        QMessageBox::information(this, "提示", "该文件夹中没有找到MDX/MDL文件");
        return;
    }

    // 获取War3路径
    QSettings settings(Constants::ORG_NAME, Constants::APP_NAME);
    QString war3Path = settings.value(Constants::NATIVE_TEXTURE_DIR_KEY, "").toString();
    if (war3Path.isEmpty()) {
        war3Path = getDefaultWar3Path();
        if (!war3Path.isEmpty()) {
            QMessageBox::warning(this, "警告",
                                 "未检测到原生贴图文件夹目录，模型解析可能异常！\n"
                                 "请在“工具”->“设置”中手动配置。");
        }
    }

    // 初始化加载状态
    m_totalCount = fileList.size();
    m_loadedCount = 0;
    m_isLoading = true;
    updateStatusBar(QString("正在加载 %1 个模型...").arg(m_totalCount), 0);

    // 异步加载每个模型
    for (int i = 0; i < fileList.size(); ++i) {
        const QFileInfo& fileInfo = fileList.at(i);
        QFutureWatcher<LoadResult>* watcher = new QFutureWatcher<LoadResult>(this);
        QFuture<LoadResult> future = QtConcurrent::run(
            [this, filePath = fileInfo.absoluteFilePath(), war3Path]() {
                return this->loadModelAsync(filePath, war3Path);
            }
        );
        connect(watcher, &QFutureWatcher<LoadResult>::finished, this, [this, watcher, i]() {
            onModelLoaded(i);
        });
        watcher->setFuture(future);
        m_loadWatchers.append(watcher);
    }
}

LoadResult MainWindow::loadModelAsync(const QString& filePath, const QString& war3Path) {
    LoadResult result;
    result.fileName = QFileInfo(filePath).fileName();
    result.filePath = filePath;
    result.success = false;

    try {
        ModelData modelData = ModelParser::parseMDX(filePath, war3Path);
        if (modelData.isValid) {
            result.modelData = modelData;
            result.success = true;
        } else {
            result.error = "模型数据无效（可能文件损坏或版本不兼容）";
        }
    } catch (const std::exception& e) {
        result.error = QString("解析异常：%1").arg(e.what());
    } catch (...) {
        result.error = "解析未知异常";
    }

    return result;
}

void MainWindow::onModelLoaded(int index) {
    if (index < 0 || index >= m_loadWatchers.size() || !m_isLoading) return;

    // 更新加载进度
    m_loadedCount++;
    int progress = (m_loadedCount * 100) / m_totalCount;
    updateStatusBar(QString("已加载 %1/%2").arg(m_loadedCount).arg(m_totalCount), progress);

    // 获取加载结果
    QFutureWatcher<LoadResult>* watcher = m_loadWatchers.at(index);
    LoadResult result = watcher->result();
    watcher->deleteLater(); // 立即释放当前watcher

    // 创建模型预览控件
    ModelViewer* viewer = new ModelViewer(nullptr);
    viewer->setFixedSize(Constants::MODEL_ITEM_SIZE, Constants::MODEL_ITEM_SIZE);
    if (result.success && result.modelData.isValid) {
        viewer->setModel(result.modelData, result.fileName);
    } else {
        QString errorMsg = QString("【%1】%2").arg(result.fileName).arg(result.error);
        viewer->setError(errorMsg);
        updateStatusBar(errorMsg, progress); // 显示具体错误
    }

    // 同步当前动画开关状态
    viewer->setAnimationEnabled(m_animationSwitch->isChecked());
    m_allViewers.append(viewer);

    // 所有模型加载完成
    if (m_loadedCount == m_totalCount) {
        onAllModelsLoaded();
    }
}

void MainWindow::onAllModelsLoaded() {
    m_isLoading = false;
    updateStatusBar(QString("加载完成！共 %1 个模型（成功：%2，失败：%3）")
                        .arg(m_totalCount)
                        .arg(m_allViewers.size())
                        .arg(m_totalCount - m_allViewers.size()));

    // 清理剩余watcher
    m_loadWatchers.clear();

    // 更新布局和分页
    updateColumnCount();
    m_currentPage = 0;
    updatePage();
}

// ==================== 事件处理 ====================
void MainWindow::onClearClicked() {
    if (m_isLoading) {
        // 取消所有正在加载的任务
        for (auto watcher : qAsConst(m_loadWatchers)) {
            if (watcher->future().isRunning()) {
                watcher->cancel();
            }
            watcher->deleteLater();
        }
        m_loadWatchers.clear();
        m_isLoading = false;
    }

    clearAllModels();
    updateStatusBar("已清空所有模型");
}

void MainWindow::clearLayout() {
    if (!m_gridLayout) return;
    while (QLayoutItem* item = m_gridLayout->takeAt(0)) {
        if (item->widget()) {
            item->widget()->hide();
            item->widget()->setParent(nullptr);
        }
        delete item;
    }
}

void MainWindow::clearAllModels() {
    clearLayout();
    qDeleteAll(m_allViewers);
    m_allViewers.clear();

    m_currentPage = 0;
    m_loadedCount = 0;
    m_totalCount = 0;

    updatePaginationButtons();
    updateStatusBar("已清空所有模型");
}

void MainWindow::onPrevPage() {
    if (m_currentPage > 0) {
        m_currentPage--;
        updatePage();
    }
}

void MainWindow::onNextPage() {
    int totalPages = calculateTotalPages();
    if ((m_currentPage + 1) < totalPages) {
        m_currentPage++;
        updatePage();
    }
}

void MainWindow::onAnimationSwitchToggled(bool checked) {
    // 同步所有模型的动画状态
    for (ModelViewer* viewer : m_allViewers) {
        viewer->setAnimationEnabled(checked);
    }
    updateStatusBar(QString("全局动画：%1").arg(checked ? "开启" : "关闭"));
}

void MainWindow::onOpenSettings() {
    SettingsDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        // 设置确认后重新检查War3路径
        QSettings settings(Constants::ORG_NAME, Constants::APP_NAME);
        QString war3Path = settings.value(Constants::NATIVE_TEXTURE_DIR_KEY, "").toString();
        if (!war3Path.isEmpty() && !validateWar3Path(war3Path)) {
            QMessageBox::warning(this, "警告", "配置的War3路径无效，请检查！");
        }
    }
}
