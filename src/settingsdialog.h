#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>

// 魔兽3路径设置对话框，用于配置模型纹理文件的查找目录
class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    // 构造函数：parent为父窗口指针，默认空
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override; // 显式声明override，提升可读性

private slots:
    // 处理「浏览」按钮点击事件，弹出目录选择框
    void onBrowseClicked();
    // 处理「保存」按钮点击事件，校验并保存路径配置
    void onSaveClicked();

private:
    // 加载已保存的路径配置到输入框
    void loadSettings();

    // 常量定义（统一管理硬编码）
    static const QString ORG_NAME;       // 配置存储的组织名
    static const QString APP_NAME;       // 配置存储的应用名
    static const QString NATIVE_TEXTURE_DIR_KEY;;  // 配置原生贴图文件夹

    // 控件成员（优化命名：Path→Dir，语义更准确）
    QLineEdit *m_war3DirEdit = nullptr;  // 魔兽3目录输入框
    QPushButton *m_browseBtn = nullptr;  // 浏览按钮
    QPushButton *m_saveBtn = nullptr;    // 保存按钮
    QPushButton *m_cancelBtn = nullptr;  // 取消按钮
};

#endif // SETTINGSDIALOG_H
