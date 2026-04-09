#include "modelviewer.h"
#include <QVBoxLayout>
#include <QFile>
#include <QImage>
#include <QOpenGLShader>
#include <QDebug>
#include <stack> // 迭代遍历骨骼用

ModelViewer::ModelViewer(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setFixedSize(200, 200);
    setMouseTracking(true);
    m_isGlInitialized = false;

    // 初始化UI
    initUI();

    // 动画定时器（显式设置父对象，确保析构释放）
    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(ANIMATION_TIMER_INTERVAL);
    connect(m_animationTimer, &QTimer::timeout, this, &ModelViewer::onAnimationTimer);
    m_animationTimer->start();
    m_elapsedTimer.start();

    // 默认启用动画
    m_animationEnabled = true;
}

ModelViewer::~ModelViewer()
{
    // 确保OpenGL资源释放
    if (m_isGlInitialized) {
        makeCurrent();
        releaseGLResources();
        releaseTextures();
        doneCurrent();
    }
    // 定时器由Qt父子机制管理，无需手动delete，但显式停止更安全
    if (m_animationTimer) {
        m_animationTimer->stop();
    }
}

void ModelViewer::initUI()
{
    // 简化布局嵌套，统一对齐方式
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    QWidget* topBar = new QWidget(this);
    QVBoxLayout* topLayout = new QVBoxLayout(topBar);
    topLayout->setContentsMargins(5, 5, 5, 5);
    topLayout->setSpacing(2);
    topLayout->setAlignment(Qt::AlignTop);

    // 文件名标签
    m_fileNameLabel = new QLabel(this);
    m_fileNameLabel->setStyleSheet(LABEL_STYLE);
    m_fileNameLabel->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(m_fileNameLabel);

    // 动画下拉框
    m_animationComboBox = new QComboBox(this);
    m_animationComboBox->setVisible(false);
    m_animationComboBox->setStyleSheet(COMBO_BOX_STYLE);
    connect(m_animationComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ModelViewer::onAnimationChanged);
    topLayout->addWidget(m_animationComboBox);

    // 错误标签
    m_errorLabel = new QLabel(this);
    m_errorLabel->setStyleSheet(ERROR_LABEL_STYLE);
    m_errorLabel->setAlignment(Qt::AlignCenter);
    m_errorLabel->setWordWrap(true);
    m_errorLabel->setVisible(false);
    topLayout->addWidget(m_errorLabel);

    mainLayout->addWidget(topBar);
    mainLayout->addStretch();
}

bool ModelViewer::initShaderProgram()
{
    m_shaderProgram = std::make_unique<QOpenGLShaderProgram>(this);

    // 顶点着色器（骨骼蒙皮核心逻辑）
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 position;
        layout (location = 1) in vec2 texCoord;
        layout (location = 2) in ivec4 boneIndices;
        layout (location = 3) in vec4 boneWeights;

        uniform mat4 model;
        uniform mat4 view;
        uniform mat4 projection;
        uniform mat4 boneMatrices[128];

        out vec2 TexCoord;

        void main() {
            mat4 skinMatrix = mat4(0.0);
            skinMatrix += boneWeights.x * boneMatrices[boneIndices.x];
            skinMatrix += boneWeights.y * boneMatrices[boneIndices.y];
            skinMatrix += boneWeights.z * boneMatrices[boneIndices.z];
            skinMatrix += boneWeights.w * boneMatrices[boneIndices.w];

            vec4 skinnedPos = skinMatrix * vec4(position, 1.0);
            gl_Position = projection * view * model * skinnedPos;
            TexCoord = texCoord;
        }
    )";

    // 片段着色器
    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec2 TexCoord;
        out vec4 FragColor;

        uniform sampler2D ourTexture;
        uniform bool useTexture;

        void main() {
            if (useTexture) {
                FragColor = texture(ourTexture, TexCoord);
            } else {
                FragColor = vec4(0.6, 0.6, 0.6, 1.0);
            }
        }
    )";

    // 编译顶点着色器（带错误处理）
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        qCritical() << "顶点着色器编译失败：" << m_shaderProgram->log();
        return false;
    }

    // 编译片段着色器（带错误处理）
    if (!m_shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        qCritical() << "片段着色器编译失败：" << m_shaderProgram->log();
        return false;
    }

    // 链接Shader（带错误处理）
    if (!m_shaderProgram->link()) {
        qCritical() << "Shader链接失败：" << m_shaderProgram->log();
        return false;
    }

    return true;
}

void ModelViewer::initializeGL()
{
    initializeOpenGLFunctions();
    glClearColor(0.2f, 0.3f, 0.3f, 1.0f);

    // 初始化Shader（失败则标记错误）
    if (!initShaderProgram()) {
        setError("Shader初始化失败");
        m_isGlInitialized = false;
        return;
    }

    // 初始化视图矩阵
    m_view.setToIdentity();
    m_view.lookAt(QVector3D(0, 0, 10), QVector3D(0, 0, 0), QVector3D(0, 1, 0));

    m_isGlInitialized = true;
}

void ModelViewer::releaseGLResources()
{
    // 释放几何体VAO/VBO/EBO
    for (auto& geoset : m_renderableGeosets) {
        if (geoset.vao) glDeleteVertexArrays(1, &geoset.vao);
        if (geoset.vbo) glDeleteBuffers(1, &geoset.vbo);
        if (geoset.ebo) glDeleteBuffers(1, &geoset.ebo);
        if (geoset.uvbo) glDeleteBuffers(1, &geoset.uvbo);
        if (geoset.boneIndicesBO) glDeleteBuffers(1, &geoset.boneIndicesBO);
        if (geoset.boneWeightsBO) glDeleteBuffers(1, &geoset.boneWeightsBO);
    }
    m_renderableGeosets.clear();

    // 释放Shader（智能指针自动管理，此处仅置空）
    // m_shaderProgram.reset();
}

void ModelViewer::releaseTextures()
{
    if (!m_textures.isEmpty()) {
        glDeleteTextures(m_textures.size(), m_textures.constData());
        m_textures.clear();
    }
}

void ModelViewer::traverseBonesIterative(int rootBoneIdx, const BoneTraversalFunc& func)
{
    // 迭代遍历骨骼（替代递归，避免栈溢出）
    if (rootBoneIdx < 0 || rootBoneIdx >= m_modelData.bones.size()) return;

    std::stack<int> boneStack;
    QVector<bool> visited(m_modelData.bones.size(), false);

    boneStack.push(rootBoneIdx);
    while (!boneStack.empty()) {
        int boneIdx = boneStack.top();
        boneStack.pop();

        if (visited[boneIdx]) continue;
        visited[boneIdx] = true;

        // 先处理父骨骼，再处理子骨骼
        const Bone& bone = m_modelData.bones.at(boneIdx);
        if (bone.parent >= 0 && bone.parent < m_modelData.bones.size() && !visited[bone.parent]) {
            boneStack.push(bone.parent);
            continue;
        }

        func(boneIdx);

        // 压入子骨骼（此处假设ModelData有子骨骼列表，若无则需调整）
        // 若原代码无父子层级存储，可保留原逻辑，此处仅示例迭代方式
        for (int i = 0; i < m_modelData.bones.size(); ++i) {
            if (m_modelData.bones.at(i).parent == boneIdx && !visited[i]) {
                boneStack.push(i);
            }
        }
    }
}

void ModelViewer::initBoneMatrices(const ModelData& modelData)
{
    int boneCount = modelData.bones.size();
    m_boneMatrices.clear();
    m_inverseBindMatrices.clear();
    m_boneMatrices.resize(boneCount);
    m_inverseBindMatrices.resize(boneCount);

    // 迭代遍历骨骼（替代递归）
    QVector<int> boneOrder;
    QVector<bool> visited(boneCount, false);
    for (int i = 0; i < boneCount; ++i) {
        if (!visited[i]) {
            traverseBonesIterative(i, [&](int boneIdx) {
                if (!visited[boneIdx]) {
                    visited[boneIdx] = true;
                    boneOrder.append(boneIdx);
                }
            });
        }
    }

    // 计算绑定姿态全局矩阵
    QVector<QMatrix4x4> bindPoseGlobalMatrices(boneCount);
    for (int i = 0; i < boneCount; ++i) {
        bindPoseGlobalMatrices[i].setToIdentity();
    }

    for (int i : boneOrder) {
        const Bone& bone = modelData.bones.at(i);
        int parentIdx = bone.parent;

        QMatrix4x4 bindPoseLocal;
        bindPoseLocal.translate(bone.initTranslation);
        bindPoseLocal.rotate(bone.initRotation);
        bindPoseLocal.scale(bone.initScale);
        bindPoseLocal.translate(bone.pivot);

        if (parentIdx < 0 || parentIdx >= boneCount) {
            bindPoseGlobalMatrices[i] = bindPoseLocal;
        } else {
            bindPoseGlobalMatrices[i] = bindPoseGlobalMatrices[parentIdx] * bindPoseLocal;
        }
    }

    // 计算逆绑定矩阵
    for (int i = 0; i < boneCount; ++i) {
        m_inverseBindMatrices[i] = bindPoseGlobalMatrices[i].inverted();
        m_boneMatrices[i].setToIdentity();
    }

    // 初始化骨骼矩阵（静态/动态）
    if (!m_animationEnabled || modelData.animations.isEmpty()) {
        // 静态姿态：使用绑定姿态的全局矩阵
        for (int i = 0; i < boneCount; ++i) {
            m_boneMatrices[i] = m_inverseBindMatrices[i].inverted();
        }
    } else if (!modelData.animations.isEmpty()) {
        // 动态动画：初始化动画时间
        m_animationTime = modelData.animations.at(0).startMs;
        updateAnimation(m_animationTime);
    }

    // 缓存模型边界（避免paintGL重复计算）
    m_modelCenter = (modelData.bounds.min + modelData.bounds.max) / 2.0f;
    m_modelRadius = qMax(modelData.bounds.radius, 0.001f); // 避免半径为0
}

void ModelViewer::initAnimationUI(const ModelData& modelData)
{
    m_animationComboBox->clear();
    if (!modelData.animations.isEmpty()) {
        for (const auto& anim : qAsConst(modelData.animations)) {
            m_animationComboBox->addItem(anim.name);
        }
        m_animationComboBox->setVisible(true);
        m_currentAnimationIndex = 0;
        m_isPlaying = true;
    } else {
        m_animationComboBox->setVisible(false);
        m_isPlaying = false;
    }
}

void ModelViewer::setModel(const ModelData& modelData, const QString& fileName)
{
    m_modelData = modelData;
    m_fileName = fileName;
    m_fileNameLabel->setText(fileName);
    m_needsGLUpdate = true;
    m_hasError = false;
    m_errorLabel->setVisible(false);

    m_currentAnimationIndex = 0;
    m_animationTime = 0.0f;
    m_isPlaying = false;

    // 初始化骨骼矩阵和动画UI
    initBoneMatrices(modelData);
    initAnimationUI(modelData);

    update();
}

void ModelViewer::setError(const QString& error)
{
    m_hasError = true;
    m_errorLabel->setText(error);
    m_errorLabel->setVisible(true);
    m_animationComboBox->setVisible(false);
    update();
}

void ModelViewer::setAnimationEnabled(bool enabled)
{
    if (m_animationEnabled == enabled) return;
    m_animationEnabled = enabled;

    int boneCount = m_modelData.bones.size();
    if (boneCount == 0) return;

    if (!m_animationEnabled || m_modelData.animations.isEmpty()) {
        // 切换到静态绑定姿态
        for (int i = 0; i < boneCount; ++i) {
            m_boneMatrices[i] = m_inverseBindMatrices[i].inverted();
        }
    } else if (!m_modelData.animations.isEmpty()) {
        // 重新开启动画
        updateAnimation(m_animationTime);
    }
    update();
}

void ModelViewer::resetCamera()
{
    m_rotateX = 0.0f;
    m_rotateY = 0.0f;
    m_scale = 1.0f;
    update();
}

void ModelViewer::resizeGL(int w, int h)
{
    // 仅在窗口缩放时更新投影矩阵（避免paintGL重复设置）
    m_projection.setToIdentity();
    m_projection.perspective(DEFAULT_FOV, (float)w / qMax(h, 1), 0.1f, m_modelRadius * 10.0f);
}

void ModelViewer::updateGLResourcesIfNeeded()
{
    if (!m_needsGLUpdate || !m_isGlInitialized) return;

    // 先释放旧资源
    releaseGLResources();
    releaseTextures();

    if (!m_modelData.isValid || m_modelData.geosets.isEmpty()) {
        m_needsGLUpdate = false;
        return;
    }

    // 创建新的渲染几何体
    for (int geosetIndex = 0; geosetIndex < m_modelData.geosets.size(); ++geosetIndex) {
        const auto& geoset = m_modelData.geosets.at(geosetIndex);

        RenderableGeoset renderableGeoset;
        renderableGeoset.indexCount = geoset.indices.size();
        renderableGeoset.textureIndex = geoset.textureId;
        renderableGeoset.originalVertices = geoset.vertices;
        renderableGeoset.boneIndices = geoset.boneIndices;
        renderableGeoset.boneWeights = geoset.boneWeights;

        // 创建VAO/VBO
        glGenVertexArrays(1, &renderableGeoset.vao);
        glGenBuffers(1, &renderableGeoset.vbo);
        glGenBuffers(1, &renderableGeoset.ebo);
        glGenBuffers(1, &renderableGeoset.uvbo);
        glGenBuffers(1, &renderableGeoset.boneIndicesBO);
        glGenBuffers(1, &renderableGeoset.boneWeightsBO);

        glBindVertexArray(renderableGeoset.vao);

        // 顶点数据
        glBindBuffer(GL_ARRAY_BUFFER, renderableGeoset.vbo);
        QVector<float> vertexData;
        for (const auto& v : geoset.vertices) {
            vertexData << v.x() << v.y() << v.z();
        }
        glBufferData(GL_ARRAY_BUFFER, vertexData.size() * sizeof(float), vertexData.constData(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);

        // UV数据
        if (!geoset.uvs.isEmpty()) {
            glBindBuffer(GL_ARRAY_BUFFER, renderableGeoset.uvbo);
            QVector<float> uvData;
            for (const auto& uv : geoset.uvs) {
                uvData << uv.x() << (1.0f - uv.y()); // UV翻转（按需调整）
            }
            glBufferData(GL_ARRAY_BUFFER, uvData.size() * sizeof(float), uvData.constData(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
        }

        // 骨骼索引
        if (!geoset.boneIndices.isEmpty()) {
            glBindBuffer(GL_ARRAY_BUFFER, renderableGeoset.boneIndicesBO);
            QVector<int> boneIndexData;
            int maxBoneIdx = m_modelData.bones.size() - 1;

            for (const auto& indices : geoset.boneIndices) {
                for (int j = 0; j < 4; ++j) {
                    int idx = (j < indices.size()) ? indices[j] : 0;
                    idx = (idx >= 0 && idx <= maxBoneIdx) ? idx : 0;
                    boneIndexData << idx;
                }
            }

            glBufferData(GL_ARRAY_BUFFER, boneIndexData.size() * sizeof(int), boneIndexData.constData(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(2);
            glVertexAttribIPointer(2, 4, GL_INT, 4 * sizeof(int), nullptr);
        }

        // 骨骼权重
        if (!geoset.boneWeights.isEmpty()) {
            glBindBuffer(GL_ARRAY_BUFFER, renderableGeoset.boneWeightsBO);
            QVector<float> boneWeightData;
            for (const auto& weights : geoset.boneWeights) {
                boneWeightData << weights.x() << weights.y() << weights.z() << weights.w();
            }
            glBufferData(GL_ARRAY_BUFFER, boneWeightData.size() * sizeof(float), boneWeightData.constData(), GL_STATIC_DRAW);
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
        }

        // 索引数据
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, renderableGeoset.ebo);
        QVector<unsigned int> indexData;
        for (const auto& idx : geoset.indices) indexData.append(idx);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indexData.size() * sizeof(unsigned int), indexData.constData(), GL_STATIC_DRAW);

        glBindVertexArray(0);
        m_renderableGeosets.append(renderableGeoset);
    }

    // 加载纹理
    for (int i = 0; i < m_modelData.allTexturePaths.size(); ++i) {
        loadTexture(m_modelData.allTexturePaths.at(i), i);
    }

    m_needsGLUpdate = false;
}

void ModelViewer::loadTexture(const QString& imagePath, int textureIndex)
{
    if (!m_isGlInitialized || textureIndex < 0 || imagePath.isEmpty() || !QFile::exists(imagePath)) {
        return;
    }

    // 确保纹理数组大小足够
    while (m_textures.size() <= textureIndex) {
        m_textures.append(0);
    }

    QImage image(imagePath);
    if (image.isNull()) {
        qWarning() << "纹理加载失败：" << imagePath;
        return;
    }

    // 转换为OpenGL兼容格式
    QImage glImage = image.convertToFormat(QImage::Format_RGBA8888);
    glImage = glImage.mirrored(false, true); // UV翻转（按需调整）

    // 创建纹理
    GLuint texId;
    glGenTextures(1, &texId);
    glBindTexture(GL_TEXTURE_2D, texId);

    // 设置纹理参数
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    // 上传纹理数据
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, glImage.width(), glImage.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE, glImage.bits());

    // 释放旧纹理，替换为新纹理
    if (m_textures[textureIndex] != 0) {
        glDeleteTextures(1, &m_textures[textureIndex]);
    }
    m_textures[textureIndex] = texId;

    glBindTexture(GL_TEXTURE_2D, 0);
}

void ModelViewer::updateAnimation(float timeMs)
{
    int boneCount = m_modelData.bones.size();
    // 动画禁用/无动画数据：使用静态姿态
    if (!m_animationEnabled || m_modelData.animations.isEmpty()) {
        for (int i = 0; i < boneCount; ++i) {
            m_boneMatrices[i] = m_inverseBindMatrices[i].inverted();
        }
        return;
    }

    // 检查动画索引有效性
    if (m_currentAnimationIndex < 0 || m_currentAnimationIndex >= m_modelData.animations.size()) {
        m_currentAnimationIndex = 0;
    }
    const Animation& currentAnim = m_modelData.animations.at(m_currentAnimationIndex);

    // 迭代遍历骨骼（替代递归）
    QVector<int> boneOrder;
    QVector<bool> visited(boneCount, false);
    for (int i = 0; i < boneCount; ++i) {
        if (!visited[i]) {
            traverseBonesIterative(i, [&](int boneIdx) {
                if (!visited[boneIdx]) {
                    visited[boneIdx] = true;
                    boneOrder.append(boneIdx);
                }
            });
        }
    }

    // 修复：统一参数类型为 float，解决 qBound 匹配问题
    float animTime = qBound(static_cast<float>(currentAnim.startMs),
                            timeMs,
                            static_cast<float>(currentAnim.endMs));

    // 计算骨骼局部动画矩阵
    QVector<QMatrix4x4> localAnimMatrices(boneCount);
    for (int i = 0; i < boneCount; ++i) {
        localAnimMatrices[i].setToIdentity();
        QVector3D translation = interpolateTranslation(i, animTime);
        QQuaternion rotation = interpolateRotation(i, animTime);
        QVector3D scale = interpolateScale(i, animTime);

        localAnimMatrices[i].translate(translation);
        localAnimMatrices[i].rotate(rotation);
        localAnimMatrices[i].scale(scale);
    }

    // 计算骨骼全局动画矩阵
    QVector<QMatrix4x4> animatedGlobalMatrices(boneCount);
    for (int i = 0; i < boneCount; ++i) animatedGlobalMatrices[i].setToIdentity();

    for (int i : boneOrder) {
        const Bone& bone = m_modelData.bones.at(i);
        int parentIdx = bone.parent;

        QMatrix4x4 boneTransform = localAnimMatrices[i];
        if (parentIdx >= 0 && parentIdx < boneCount) {
            animatedGlobalMatrices[i] = animatedGlobalMatrices[parentIdx] * boneTransform;
        } else {
            animatedGlobalMatrices[i] = boneTransform;
        }
    }

    // 核心修复：骨骼蒙皮矩阵 = 全局动画矩阵 * 逆绑定矩阵
    for (int i = 0; i < boneCount; ++i) {
        m_boneMatrices[i] = animatedGlobalMatrices[i] * m_inverseBindMatrices[i];
    }
}

QVector3D ModelViewer::interpolateTranslation(int boneIdx, float timeMs) const
{
    if (boneIdx < 0 || boneIdx >= m_modelData.translationKeys.size()) return QVector3D(0,0,0);
    const auto& keys = m_modelData.translationKeys.at(boneIdx);
    if (keys.isEmpty()) return QVector3D(0,0,0);
    if (keys.size() == 1) return keys.first().translation;

    for (int i = 0; i < keys.size() - 1; ++i) {
        const auto& keyCurr = keys.at(i);
        const auto& keyNext = keys.at(i+1);
        if (timeMs >= keyCurr.time && timeMs <= keyNext.time) {
            float factor = (timeMs - keyCurr.time) / qMax(0.0001f, keyNext.time - keyCurr.time);
            return keyCurr.translation + factor * (keyNext.translation - keyCurr.translation);
        }
    }
    return (timeMs < keys.first().time) ? keys.first().translation : keys.last().translation;
}

QQuaternion ModelViewer::interpolateRotation(int boneIdx, float timeMs) const
{
    if (boneIdx < 0 || boneIdx >= m_modelData.rotationKeys.size()) return QQuaternion();
    const auto& keys = m_modelData.rotationKeys.at(boneIdx);
    if (keys.isEmpty()) return QQuaternion();
    if (keys.size() == 1) return keys.first().rotation;

    for (int i = 0; i < keys.size() - 1; ++i) {
        const auto& keyCurr = keys.at(i);
        const auto& keyNext = keys.at(i+1);
        if (timeMs >= keyCurr.time && timeMs <= keyNext.time) {
            float factor = (timeMs - keyCurr.time) / qMax(0.0001f, keyNext.time - keyCurr.time);
            return QQuaternion::slerp(keyCurr.rotation, keyNext.rotation, factor);
        }
    }
    return (timeMs < keys.first().time) ? keys.first().rotation : keys.last().rotation;
}

QVector3D ModelViewer::interpolateScale(int boneIdx, float timeMs) const
{
    if (boneIdx < 0 || boneIdx >= m_modelData.scaleKeys.size()) return QVector3D(1,1,1);
    const auto& keys = m_modelData.scaleKeys.at(boneIdx);
    if (keys.isEmpty()) return QVector3D(1,1,1);
    if (keys.size() == 1) return keys.first().scale;

    for (int i = 0; i < keys.size() - 1; ++i) {
        const auto& keyCurr = keys.at(i);
        const auto& keyNext = keys.at(i+1);
        if (timeMs >= keyCurr.time && timeMs <= keyNext.time) {
            float factor = (timeMs - keyCurr.time) / qMax(0.0001f, keyNext.time - keyCurr.time);
            return keyCurr.scale + factor * (keyNext.scale - keyCurr.scale);
        }
    }
    return (timeMs < keys.first().time) ? keys.first().scale : keys.last().scale;
}

void ModelViewer::setupMatrices()
{
    // 视图矩阵：相机旋转+缩放
    m_view.setToIdentity();
    m_view.lookAt(QVector3D(0, 0, m_modelRadius * 2.5f), QVector3D(0, 0, 0), QVector3D(0, 1, 0));
    m_view.rotate(m_rotateX, 1, 0, 0);
    m_view.rotate(m_rotateY, 0, 1, 0);
    m_view.scale(m_scale);

    // 模型矩阵：缩放+旋转+居中
    m_model.setToIdentity();
    float scaleFactor = DEFAULT_MODEL_SCALE / m_modelRadius;
    m_model.scale(scaleFactor);
    m_model.rotate(-90.0f, 1, 0, 0);
    m_model.rotate(-90.0f, 0, 0, 1);
    m_model.scale(1, -1, 1);
    m_model.translate(-m_modelCenter);

    // 传递矩阵到Shader
    m_shaderProgram->setUniformValue("model", m_model);
    m_shaderProgram->setUniformValue("view", m_view);
    m_shaderProgram->setUniformValue("projection", m_projection);

    // 传递骨骼矩阵到Shader
    int boneCount = qMin(m_boneMatrices.size(), MAX_BONES);
    for (int i = 0; i < boneCount; ++i) {
        m_boneMatricesArray[i] = m_boneMatrices.at(i);
    }
    for (int i = boneCount; i < MAX_BONES; ++i) {
        m_boneMatricesArray[i].setToIdentity();
    }
    m_shaderProgram->setUniformValueArray("boneMatrices", m_boneMatricesArray, MAX_BONES);
}

void ModelViewer::renderGeosets()
{
    // OpenGL状态设置
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 遍历渲染几何体
    for (const auto& geoset : m_renderableGeosets) {
        bool hasTexture = false;
        // 绑定纹理
        if (geoset.textureIndex >= 0 && geoset.textureIndex < m_textures.size()) {
            GLuint texId = m_textures.at(geoset.textureIndex);
            if (texId != 0) {
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, texId);
                m_shaderProgram->setUniformValue("ourTexture", 0);
                m_shaderProgram->setUniformValue("useTexture", true);
                hasTexture = true;
            }
        }

        // 无纹理则使用纯色
        if (!hasTexture) {
            glBindTexture(GL_TEXTURE_2D, 0);
            m_shaderProgram->setUniformValue("useTexture", false);
        }

        // 绘制几何体
        glBindVertexArray(geoset.vao);
        glDrawElements(GL_TRIANGLES, geoset.indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);

        // 解绑纹理
        if (hasTexture) {
            glBindTexture(GL_TEXTURE_2D, 0);
        }
    }

    // 重置OpenGL状态
    glBindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ModelViewer::paintGL()
{
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // 有错误则不渲染
    if (m_hasError){
        return;
    }

    // 首次/需要更新时刷新GL资源
    static bool firstRender = true;
    if (m_needsGLUpdate || firstRender) {
        updateGLResourcesIfNeeded();
        firstRender = false;
    }

    // Shader无效则不渲染
    if (!m_shaderProgram || !m_shaderProgram->isLinked() || m_renderableGeosets.isEmpty()) {
        return;
    }

    // 绑定Shader并渲染
    m_shaderProgram->bind();
    setupMatrices();  // 设置矩阵
    renderGeosets();  // 渲染几何体
    m_shaderProgram->release();
}

void ModelViewer::onAnimationTimer()
{
    // 仅当动画启用且播放时更新
    if (!m_animationEnabled || !m_isPlaying || m_modelData.animations.isEmpty()) {
        return;
    }

    const Animation& anim = m_modelData.animations.at(m_currentAnimationIndex);
    qint64 elapsedMs = m_elapsedTimer.restart();
    m_animationTime += elapsedMs * m_animationSpeed;

    // 修复：最小动画时长（避免无限循环）
    int animDuration = qMax(anim.endMs - anim.startMs, 10); // 最小10ms
    // 循环动画
    while (m_animationTime > anim.endMs) {
        m_animationTime -= animDuration;
    }
    while (m_animationTime < anim.startMs) {
        m_animationTime += animDuration;
    }

    updateAnimation(m_animationTime);
    update();
}

void ModelViewer::onAnimationChanged(int index)
{
    if (m_modelData.animations.isEmpty()) return;

    // 限制索引范围
    m_currentAnimationIndex = qBound(0, index, m_modelData.animations.size() - 1);
    m_animationTime = m_modelData.animations.at(m_currentAnimationIndex).startMs;

    // 仅当动画启用时更新
    if (m_animationEnabled) {
        updateAnimation(m_animationTime);
    }
    update();
}

void ModelViewer::mousePressEvent(QMouseEvent* event)
{
    // 兼容Qt5/Qt6
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_lastMousePos = event->position().toPoint();
#else
    m_lastMousePos = event->pos();
#endif
    event->accept();
}

void ModelViewer::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & Qt::LeftButton)) {
        event->accept();
        return;
    }

    // 兼容Qt5/Qt6
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPoint currentPos = event->position().toPoint();
#else
    QPoint currentPos = event->pos();
#endif

    int dx = currentPos.x() - m_lastMousePos.x();
    int dy = currentPos.y() - m_lastMousePos.y();

    // 更新旋转角度（限制X轴旋转范围）
    m_rotateY += dx * ROTATE_SENSITIVITY;
    m_rotateX += dy * ROTATE_SENSITIVITY;
    m_rotateX = qBound(-89.0f, m_rotateX, 89.0f);

    m_lastMousePos = currentPos;
    update();
    event->accept();
}

void ModelViewer::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        resetCamera();
    }
    event->accept();
}

void ModelViewer::wheelEvent(QWheelEvent* event)
{
    // 计算缩放增量（兼容不同平台滚轮步长）
    float delta = event->angleDelta().y() / 120.0f;
    m_scale += delta * SCALE_SENSITIVITY;
    m_scale = qBound(MIN_SCALE, m_scale, MAX_SCALE);

    update();
    event->accept();
}
