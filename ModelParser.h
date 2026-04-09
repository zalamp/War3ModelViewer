#ifndef MODELPARSER_H
#define MODELPARSER_H

#include <QString>
#include <QVector>
#include <QVector3D>
#include <QVector2D>
#include <QVector4D>
#include <QQuaternion>
#include <QDebug>

// 常量定义：替代魔法值，提升可维护性
namespace ModelParserConst {
const int DEFAULT_ANIM_DURATION = 1000;       // 默认动画时长(ms)
const float DEFAULT_MOVE_SPEED = 0.0f;        // 默认移动速度
const int MAX_BONE_WEIGHT_COUNT = 4;          // 最大骨骼权重数
const int PARSE_TIMEOUT_MS = 30000;           // MDX解析超时时间(ms)
const QString NODE_SERVICE_URL = "http://localhost:3737/parse"; // Node服务地址
}

/**
 * @brief 骨骼数据结构
 * 存储骨骼名称、父子关系、初始变换（旋转/平移/缩放）和枢轴点
 */
struct Bone {
    QString name;                // 骨骼名称
    int parent = -1;             // 父骨骼索引（-1表示根骨骼）
    QVector3D pivot;             // 枢轴点
    QQuaternion initRotation;    // 初始旋转（四元数）
    QVector3D initTranslation;   // 初始平移
    QVector3D initScale = QVector3D(1, 1, 1); // 初始缩放（默认1倍）
};

/**
 * @brief 动画关键帧
 * 存储某一时刻骨骼的变换数据
 */
struct KeyFrame {
    float time = 0.0f;           // 关键帧时间(ms)
    QVector3D translation;       // 平移数据
    QQuaternion rotation = QQuaternion(1, 0, 0, 0); // 旋转数据（默认无旋转）
    QVector3D scale = QVector3D(1, 1, 1); // 缩放数据（默认1倍）
};

/**
 * @brief 动画片段
 * 存储单个动画的名称、时间范围和移动速度
 */
struct Animation {
    QString name;                // 动画名称
    int startMs = 0;             // 动画起始时间(ms)
    int endMs = ModelParserConst::DEFAULT_ANIM_DURATION; // 动画结束时间
    float moveSpeed = ModelParserConst::DEFAULT_MOVE_SPEED; // 移动速度
};

/**
 * @brief 材质层
 * 支持多层材质叠加，存储纹理ID、过滤模式、透明度
 */
struct MaterialLayer {
    int textureId = -1;          // 纹理ID（-1表示无纹理）
    int filterMode = 0;          // 过滤模式（0=默认）
    float alpha = 1.0f;          // 透明度（1=不透明）
};

/**
 * @brief 材质数据
 * 包含多个材质层的集合
 */
struct MaterialData {
    QVector<MaterialLayer> layers; // 材质层列表
};

/**
 * @brief 几何体集
 * 存储模型的顶点、UV、索引、蒙皮等核心几何数据
 */
struct GeosetData {
    QVector<QVector3D> vertices;    // 顶点坐标
    QVector<QVector2D> uvs;         // UV坐标
    QVector<unsigned int> indices;  // 三角面索引（仅保留3的倍数）
    int textureId = -1;             // 纹理ID
    int materialId = -1;            // 材质ID
    QVector2D uvOffset = QVector2D(0.0f, 0.0f); // UV偏移
    QVector2D uvScale = QVector2D(1.0f, 1.0f);  // UV缩放

    QVector<QVector<int>> boneIndices;   // 骨骼索引（每个顶点对应4个骨骼）
    QVector<QVector4D> boneWeights;      // 骨骼权重（每个顶点对应4个权重）
};

/**
 * @brief 模型完整数据
 * 整合骨骼、动画、几何体、材质、纹理等所有模型数据
 */
struct ModelData {
    QVector<Bone> bones;                // 骨骼列表
    QVector<Animation> animations;      // 动画列表
    QVector<GeosetData> geosets;        // 几何体集列表
    QVector<MaterialData> materials;    // 材质列表
    QStringList allTexturePaths;        // 所有纹理路径

    // 模型包围盒
    struct Bounds {
        float radius = 100.0f;          // 包围球半径
        QVector3D min = QVector3D(0, 0, 0); // 包围盒最小值
        QVector3D max = QVector3D(0, 0, 0); // 包围盒最大值
    } bounds;

    // 动画关键帧（按骨骼索引分组）
    QVector<QVector<KeyFrame>> translationKeys; // 平移关键帧
    QVector<QVector<KeyFrame>> rotationKeys;    // 旋转关键帧
    QVector<QVector<KeyFrame>> scaleKeys;       // 缩放关键帧

    bool isValid = false;               // 模型是否解析成功
};

/**
 * @brief MDX/MDL模型解析器
 * 核心功能：调用本地Node服务解析MDX文件，暂不支持MDL解析
 */
class ModelParser
{
public:
    /**
     * @brief 解析MDX模型文件
     * @param filePath MDX文件路径
     * @param war3Path 魔兽3根路径（用于补全纹理路径）
     * @return 解析后的模型数据（isValid=true表示成功）
     */
    static ModelData parseMDX(const QString& filePath, const QString& war3Path = "");

    /**
     * @brief 解析MDL模型文件（暂未实现）
     * @param filePath MDL文件路径
     * @return 空的模型数据（isValid=false）
     */
    static ModelData parseMDL(const QString& filePath);

private:
    /**
     * @brief 获取备用立方体模型（解析失败时返回）
     * @return 基础立方体模型数据
     */
    static ModelData getFallbackCube();

    // 以下方法为预留的本地二进制解析接口（暂未实现）
    static float readFloatLE(QDataStream& stream) { Q_UNUSED(stream); return 0.0f; }
    static qint32 readIntLE(QDataStream& stream) { Q_UNUSED(stream); return 0; }
};

#endif // MODELPARSER_H
