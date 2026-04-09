#include "settingsdialog.h"
#include <QSettings>
#include <QFileDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QLabel>
#include <QDir>

// 初始化静态常量（统一管理硬编码，便于修改）
const QString SettingsDialog::ORG_NAME = "MyCompany";
const QString SettingsDialog::APP_NAME = "War3ModelViewer";
const QString SettingsDialog::NATIVE_TEXTURE_DIR_KEY = "NativeTexture/Path";

// 布局/样式常量（统一管理UI尺寸）
const int MAIN_LAYOUT_MARGIN = 20;
const int LAYOUT_SPACING = 15;
const int PATH_LAYOUT_SPACING = 10;
const int CONTROL_MIN_HEIGHT = 30;
const int BUTTON_MIN_HEIGHT = 35;
const int BUTTON_MIN_WIDTH = 100;

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    // 窗口基础设置（支持国际化）
    setWindowTitle(tr("设置"));
    setMinimumSize(600, 200); // 合并minWidth/minHeight为minSize

    // 主布局（垂直）
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(LAYOUT_SPACING);
    mainLayout->setContentsMargins(MAIN_LAYOUT_MARGIN, MAIN_LAYOUT_MARGIN,
                                   MAIN_LAYOUT_MARGIN, MAIN_LAYOUT_MARGIN);

    // 提示标签
    QLabel* infoLabel = new QLabel(tr("请设置《魔兽争霸3》的安装目录（或解压 MPQ 的文件夹），用于查找模型纹理文件。"), this);
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #666; padding: 5px;");
    mainLayout->addWidget(infoLabel);

    // 路径选择布局（水平）
    QHBoxLayout *pathLayout = new QHBoxLayout();
    pathLayout->setSpacing(PATH_LAYOUT_SPACING);

    pathLayout->addWidget(new QLabel(tr("魔兽争霸3 目录:"), this));
    m_war3DirEdit = new QLineEdit(this);
    m_war3DirEdit->setMinimumHeight(CONTROL_MIN_HEIGHT);
    m_browseBtn = new QPushButton(tr("浏览..."), this);
    m_browseBtn->setMinimumHeight(CONTROL_MIN_HEIGHT);

    pathLayout->addWidget(m_war3DirEdit);
    pathLayout->addWidget(m_browseBtn);

    mainLayout->addLayout(pathLayout);
    mainLayout->addStretch(1); // 拉伸因子，UI更均衡

    // 按钮布局（水平）
    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_saveBtn = new QPushButton(tr("保存"), this);
    m_saveBtn->setMinimumSize(BUTTON_MIN_WIDTH, BUTTON_MIN_HEIGHT);
    m_cancelBtn = new QPushButton(tr("取消"), this);
    m_cancelBtn->setMinimumSize(BUTTON_MIN_WIDTH, BUTTON_MIN_HEIGHT);

    btnLayout->addStretch(1);
    btnLayout->addWidget(m_saveBtn);
    btnLayout->addWidget(m_cancelBtn);

    mainLayout->addLayout(btnLayout);

    // 信号槽连接（Qt6风格，更安全）
    connect(m_browseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowseClicked);
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSaveClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    // 加载历史配置
    loadSettings();
}

SettingsDialog::~SettingsDialog()
{
    // Qt父子机制会自动析构子控件，无需手动释放
}

// 浏览按钮：弹出目录选择框，填充到输入框
void SettingsDialog::onBrowseClicked()
{
    QString selectedDir = QFileDialog::getExistingDirectory(
        this,
        tr("选择魔兽原生贴图文件夹"),
        m_war3DirEdit->text() // 初始路径为当前输入框内容
        );

    if (!selectedDir.isEmpty()) {
        m_war3DirEdit->setText(selectedDir);
    }
}

// 保存按钮：校验路径并写入配置
void SettingsDialog::onSaveClicked()
{
    QString war3Dir = m_war3DirEdit->text().trimmed(); // 去除首尾空格

    // 1. 空路径处理：提示用户确认是否清空配置
    if (war3Dir.isEmpty()) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr("确认清空"),
            tr("您输入的路径为空，这将清空魔兽原生贴图文件夹配置，可能导致纹理无法加载。\n\n是否确认保存？"),
            QMessageBox::Yes | QMessageBox::No
            );
        if (reply == QMessageBox::No) {
            return;
        }
    }
    // 2. 非空路径：校验目录存在性
    else if (!QDir(war3Dir).exists()) { // 替换QFile::exists，语义更准确
        QMessageBox::warning(this, tr("错误"), tr("所选路径不存在！"));
        return;
    }

    // 写入配置（跨平台持久化）
    QSettings settings(ORG_NAME, APP_NAME);
    settings.setValue(NATIVE_TEXTURE_DIR_KEY, war3Dir);

    // 提示保存成功并关闭对话框
    QMessageBox::information(this, tr("成功"), tr("设置已保存！"));
    accept();
}

// 加载历史配置到输入框
void SettingsDialog::loadSettings()
{
    QSettings settings(ORG_NAME, APP_NAME);
    QString savedDir = settings.value(NATIVE_TEXTURE_DIR_KEY, "").toString();
    m_war3DirEdit->setText(savedDir);
}
