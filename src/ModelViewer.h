#ifndef MODELVIEWER_H
#define MODELVIEWER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QMouseEvent>
#include <QTimer>
#include <QLabel>
#include <QComboBox>
#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QVector>
#include <QQuaternion>
#include <memory> // 智能指针
#include "modelparser.h"

// 几何体渲染资源封装（命名更清晰）
struct RenderableGeoset {
    GLuint vao = 0;          // 顶点数组对象
    GLuint vbo = 0;          // 顶点缓冲（位置）
    GLuint ebo = 0;          // 索引缓冲
    GLuint uvbo = 0;         // UV坐标缓冲
    GLuint boneIndicesBO = 0;// 骨骼索引缓冲
    GLuint boneWeightsBO = 0;// 骨骼权重缓冲
    int indexCount = 0;      // 索引数量
    int textureIndex = -1;   // 纹理索引（替代原textureId，语义更清晰）

    QVector<QVector3D> originalVertices; // 原始顶点数据
    QVector<QVector<int>> boneIndices;   // 骨骼索引
    QVector<QVector4D> boneWeights;      // 骨骼权重
};

class ModelViewer : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT
public:
    // 常量抽离（统一管理魔法数字）
    static constexpr int MAX_BONES = 128;                // 最大骨骼数
    static constexpr int ANIMATION_TIMER_INTERVAL = 16;  // 动画定时器默认间隔(ms)
    static constexpr float DEFAULT_FOV = 45.0f;          // 默认透视视角
    static constexpr float MIN_SCALE = 0.1f;             // 最小缩放因子
    static constexpr float MAX_SCALE = 10.0f;            // 最大缩放因子
    static constexpr float ROTATE_SENSITIVITY = 0.5f;    // 旋转灵敏度
    static constexpr float SCALE_SENSITIVITY = 0.1f;     // 缩放灵敏度
    static constexpr float DEFAULT_MODEL_SCALE = 120.0f; // 默认模型缩放基数

    explicit ModelViewer(QWidget *parent = nullptr);
    ~ModelViewer() override;

    // 公共接口
    void setModel(const ModelData& modelData, const QString& fileName);
    QString getFileName() const { return m_fileName; }
    void setError(const QString& error);
    void setAnimationEnabled(bool enabled);   // 切换动画/静态姿态

protected:
    // OpenGL生命周期
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

    // 鼠标事件
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private slots:
    void onAnimationTimer();
    void onAnimationChanged(int index);

private:
    // 类型别名（简化代码）
    using BoneTraversalFunc = std::function<void(int)>;

    // 工具函数（拆分长逻辑）
    void initUI();                          // 初始化UI组件
    bool initShaderProgram();               // 初始化Shader（带错误处理）
    void releaseGLResources();              // 释放OpenGL资源（统一管理）
    void releaseTextures();                 // 释放纹理资源
    void traverseBonesIterative(int rootBoneIdx, const BoneTraversalFunc& func); // 迭代遍历骨骼（替代递归）
    void setupMatrices();                   // 设置投影/视图/模型矩阵（抽离paintGL逻辑）
    void renderGeosets();                   // 渲染几何体（抽离paintGL逻辑）
    void initBoneMatrices(const ModelData& modelData); // 初始化骨骼矩阵（抽离setModel逻辑）
    void initAnimationUI(const ModelData& modelData);  // 初始化动画UI（抽离setModel逻辑）

    // 核心逻辑函数
    void updateGLResourcesIfNeeded();
    void loadTexture(const QString& imagePath, int textureIndex);
    void updateAnimation(float timeMs);
    QVector3D interpolateTranslation(int boneIdx, float timeMs) const;
    QQuaternion interpolateRotation(int boneIdx, float timeMs) const;
    QVector3D interpolateScale(int boneIdx, float timeMs) const;
    void resetCamera();

    // UI组件
    QLabel* m_fileNameLabel = nullptr;
    QComboBox* m_animationComboBox = nullptr;
    QLabel* m_errorLabel = nullptr;

    // OpenGL资源（智能指针管理，避免内存泄漏）
    std::unique_ptr<QOpenGLShaderProgram> m_shaderProgram;
    QVector<RenderableGeoset> m_renderableGeosets; // 渲染几何体（命名更清晰）
    QVector<GLuint> m_textures;

    // 模型数据
    ModelData m_modelData;
    QString m_fileName;
    bool m_needsGLUpdate = false;    // 是否需要更新GL资源（命名更清晰）
    bool m_isGlInitialized = false;
    bool m_hasError = false;

    // 矩阵相关
    QMatrix4x4 m_projection;
    QMatrix4x4 m_view;
    QMatrix4x4 m_model;
    QVector<QMatrix4x4> m_boneMatrices;
    QVector<QMatrix4x4> m_inverseBindMatrices;
    QMatrix4x4 m_boneMatricesArray[MAX_BONES]; // 传给Shader的骨骼矩阵数组

    // 相机交互
    QPoint m_lastMousePos;
    float m_rotateX = 0.0f;
    float m_rotateY = 0.0f;
    float m_scale = 1.0f;
    QVector3D m_modelCenter; // 缓存模型中心（避免重复计算）
    float m_modelRadius = 100.0f; // 缓存模型半径（避免重复计算）

    // 动画相关
    QTimer* m_animationTimer = nullptr;
    QElapsedTimer m_elapsedTimer;
    int m_currentAnimationIndex = 0;
    float m_animationTime = 0.0f;
    float m_animationSpeed = 1.0f;
    bool m_isPlaying = false;
    bool m_animationEnabled = true;

    // 样式表常量（抽离便于维护）
    const QString LABEL_STYLE = "background-color: rgba(0,0,0,150); color: white; padding: 2px;";
    const QString ERROR_LABEL_STYLE = "background-color: rgba(200,0,0,200); color: white; padding: 5px;";
    const QString COMBO_BOX_STYLE = "background-color: rgba(0,0,0,150); color: white;";
};

#endif // MODELVIEWER_H
