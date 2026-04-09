#include "ModelParser.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>

/**
 * @brief 解析MDX文件核心逻辑
 * 调用本地Node.js服务，解析JSON响应并填充ModelData
 */
ModelData ModelParser::parseMDX(const QString& filePath, const QString& war3Path)
{
    // 1. 初始化网络请求（修复：确保QNetworkRequest正确构造）
    QNetworkAccessManager networkManager;
    QUrl serviceUrl(ModelParserConst::NODE_SERVICE_URL); // 单独构造QUrl，避免语法歧义
    QNetworkRequest request(serviceUrl); // 修复：简化构造，避免参数解析错误
    // 修复：Content-Type头用正确的QByteArray类型，兼容Qt版本
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray("application/json"));

    // 构造请求JSON
    QJsonObject requestJson;
    requestJson["filePath"] = filePath;
    requestJson["war3Path"] = war3Path;
    QByteArray requestData = QJsonDocument(requestJson).toJson(QJsonDocument::Compact);

    // 2. 发送POST请求并等待响应（修复：确保post参数类型匹配）
    QNetworkReply* reply = networkManager.post(request, requestData);
    QEventLoop eventLoop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(ModelParserConst::PARSE_TIMEOUT_MS);

    // 连接信号：超时/响应完成时退出事件循环
    QObject::connect(&timeoutTimer, &QTimer::timeout, &eventLoop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &eventLoop, &QEventLoop::quit);

    timeoutTimer.start();
    eventLoop.exec();

    // 3. 初始化返回结果
    ModelData result;

    // 4. 处理响应错误
    if (reply->error() != QNetworkReply::NoError) {
        qWarning() << QString("MDX解析网络错误：%1，文件：%2").arg(reply->errorString()).arg(filePath);
        reply->deleteLater();
        return getFallbackCube(); // 返回备用模型
    }

    // 5. 解析JSON响应
    QByteArray responseData = reply->readAll();
    QJsonParseError jsonError;
    QJsonDocument responseDoc = QJsonDocument::fromJson(responseData, &jsonError);
    if (jsonError.error != QJsonParseError::NoError) {
        qWarning() << QString("MDX解析JSON错误：%1，文件：%2").arg(jsonError.errorString()).arg(filePath);
        reply->deleteLater();
        return getFallbackCube();
    }

    QJsonObject responseObj = responseDoc.object();
    if (!responseObj["success"].toBool()) {
        qWarning() << QString("MDX解析失败（Node服务返回失败），文件：%1").arg(filePath);
        reply->deleteLater();
        return getFallbackCube();
    }

    // 6. 解析骨骼数据
    if (responseObj.contains("bones")) {
        QJsonArray bonesArray = responseObj["bones"].toArray();
        for (int i = 0; i < bonesArray.size(); ++i) {
            QJsonObject boneObj = bonesArray[i].toObject();
            Bone bone;
            bone.name = boneObj["name"].toString();
            bone.parent = boneObj["parent"].toInt(-1);

            // 解析枢轴点
            QJsonArray pivotArr = boneObj["pivot"].toArray();
            if (pivotArr.size() >= 3) {
                bone.pivot = QVector3D(pivotArr[0].toDouble(), pivotArr[1].toDouble(), pivotArr[2].toDouble());
            }

            // 解析初始旋转
            QJsonArray initRotArr = boneObj["initRotation"].toArray();
            if (initRotArr.size() >= 4) {
                bone.initRotation = QQuaternion(initRotArr[3].toDouble(),
                                                initRotArr[0].toDouble(),
                                                initRotArr[1].toDouble(),
                                                initRotArr[2].toDouble());
            }

            // 解析初始平移
            QJsonArray initTransArr = boneObj["initTranslation"].toArray();
            if (initTransArr.size() >= 3) {
                bone.initTranslation = QVector3D(initTransArr[0].toDouble(),
                                                 initTransArr[1].toDouble(),
                                                 initTransArr[2].toDouble());
            }

            // 解析初始缩放
            QJsonArray initScaleArr = boneObj["initScale"].toArray();
            if (initScaleArr.size() >= 3) {
                bone.initScale = QVector3D(initScaleArr[0].toDouble(),
                                           initScaleArr[1].toDouble(),
                                           initScaleArr[2].toDouble());
            }

            result.bones.append(bone);
        }
    }

    // 7. 解析动画数据
    if (responseObj.contains("animations")) {
        QJsonArray animArray = responseObj["animations"].toArray();
        for (int i = 0; i < animArray.size(); ++i) {
            QJsonObject animObj = animArray[i].toObject();
            Animation anim;
            anim.name = animObj["name"].toString();
            anim.startMs = animObj["start"].toInt(0);
            anim.endMs = animObj["end"].toInt(ModelParserConst::DEFAULT_ANIM_DURATION);
            // 修复：toFloat → toDouble转float
            anim.moveSpeed = (float)animObj["moveSpeed"].toDouble(ModelParserConst::DEFAULT_MOVE_SPEED);
            result.animations.append(anim);
        }
    }

    // 8. 初始化关键帧容器（按骨骼数分配）
    result.translationKeys.resize(result.bones.size());
    result.rotationKeys.resize(result.bones.size());
    result.scaleKeys.resize(result.bones.size());

    // 9. 解析关键帧数据
    if (responseObj.contains("keyframes")) {
        QJsonObject keyframesObj = responseObj["keyframes"].toObject();
        for (auto it = keyframesObj.begin(); it != keyframesObj.end(); ++it) {
            bool ok = false;
            int boneIndex = it.key().toInt(&ok);
            if (!ok || boneIndex < 0 || boneIndex >= result.bones.size()) {
                qWarning() << QString("无效的骨骼索引：%1，跳过关键帧解析").arg(it.key());
                continue;
            }

            QJsonObject boneKeyFrames = it.value().toObject();
            // 解析平移关键帧
            if (boneKeyFrames.contains("translation")) {
                QJsonArray transArr = boneKeyFrames["translation"].toArray();
                for (const auto& kfVal : transArr) {
                    QJsonObject kfObj = kfVal.toObject();
                    KeyFrame keyFrame;
                    // 修复：toFloat → toDouble转float
                    keyFrame.time = (float)kfObj["time"].toDouble(0.0f);
                    QJsonArray valArr = kfObj["value"].toArray();
                    if (valArr.size() >= 3) {
                        keyFrame.translation = QVector3D(valArr[0].toDouble(), valArr[1].toDouble(), valArr[2].toDouble());
                    }
                    result.translationKeys[boneIndex].append(keyFrame);
                }
            }

            // 解析旋转关键帧
            if (boneKeyFrames.contains("rotation")) {
                QJsonArray rotArr = boneKeyFrames["rotation"].toArray();
                for (const auto& kfVal : rotArr) {
                    QJsonObject kfObj = kfVal.toObject();
                    KeyFrame keyFrame;
                    // 修复：toFloat → toDouble转float
                    keyFrame.time = (float)kfObj["time"].toDouble(0.0f);
                    QJsonArray valArr = kfObj["value"].toArray();
                    if (valArr.size() >= 4) {
                        keyFrame.rotation = QQuaternion(valArr[3].toDouble(),
                                                        valArr[0].toDouble(),
                                                        valArr[1].toDouble(),
                                                        valArr[2].toDouble());
                    }
                    result.rotationKeys[boneIndex].append(keyFrame);
                }
            }

            // 解析缩放关键帧
            if (boneKeyFrames.contains("scale")) {
                QJsonArray scaleArr = boneKeyFrames["scale"].toArray();
                for (const auto& kfVal : scaleArr) {
                    QJsonObject kfObj = kfVal.toObject();
                    KeyFrame keyFrame;
                    // 修复：toFloat → toDouble转float
                    keyFrame.time = (float)kfObj["time"].toDouble(0.0f);
                    QJsonArray valArr = kfObj["value"].toArray();
                    if (valArr.size() >= 3) {
                        keyFrame.scale = QVector3D(valArr[0].toDouble(), valArr[1].toDouble(), valArr[2].toDouble());
                    }
                    result.scaleKeys[boneIndex].append(keyFrame);
                }
            }
        }
    }

    // 10. 解析材质→纹理映射
    QVector<int> materialToTexture;
    if (responseObj.contains("materialToTexture")) {
        QJsonArray matToTexArr = responseObj["materialToTexture"].toArray();
        for (const auto& val : matToTexArr) {
            materialToTexture.append(val.toInt(-1));
        }
    }

    // 11. 解析材质数据
    if (responseObj.contains("materials")) {
        QJsonArray materialsArray = responseObj["materials"].toArray();
        for (const auto& matVal : materialsArray) {
            QJsonObject matObj = matVal.toObject();
            MaterialData matData;
            if (matObj.contains("layers")) {
                QJsonArray layersArray = matObj["layers"].toArray();
                for (const auto& layerVal : layersArray) {
                    QJsonObject layerObj = layerVal.toObject();
                    MaterialLayer layerData;
                    layerData.textureId = layerObj["textureId"].toInt(-1);
                    layerData.filterMode = layerObj["filterMode"].toInt(0);
                    // 修复：toFloat → toDouble转float
                    layerData.alpha = (float)layerObj["alpha"].toDouble(1.0f);
                    matData.layers.append(layerData);
                }
            }
            result.materials.append(matData);
        }
    }

    // 12. 解析纹理路径
    if (responseObj.contains("textures")) {
        QJsonArray texArray = responseObj["textures"].toArray();
        for (const auto& texVal : texArray) {
            QJsonObject texObj = texVal.toObject();
            result.allTexturePaths.append(texObj["pngPath"].toString());
        }
    }

    // 13. 解析几何体集
    if (responseObj.contains("geosets")) {
        QJsonArray geosetsArray = responseObj["geosets"].toArray();
        for (const auto& geosetVal : geosetsArray) {
            QJsonObject geosetObj = geosetVal.toObject();
            GeosetData geosetData;

            // 材质/纹理ID
            geosetData.materialId = geosetObj["materialId"].toInt(-1);
            if (geosetData.materialId >= 0 && geosetData.materialId < materialToTexture.size()) {
                geosetData.textureId = materialToTexture[geosetData.materialId];
            }

            // 顶点数据
            if (geosetObj.contains("vertices")) {
                QJsonArray vertsArray = geosetObj["vertices"].toArray();
                for (const auto& vertVal : vertsArray) {
                    QJsonArray coord = vertVal.toArray();
                    if (coord.size() >= 3) {
                        geosetData.vertices.append(QVector3D(coord[0].toDouble(), coord[1].toDouble(), coord[2].toDouble()));
                    }
                }
            }

            // UV数据
            if (geosetObj.contains("uvs")) {
                QJsonArray uvsArray = geosetObj["uvs"].toArray();
                for (const auto& uvVal : uvsArray) {
                    QJsonArray uv = uvVal.toArray();
                    if (uv.size() >= 2) {
                        geosetData.uvs.append(QVector2D(uv[0].toDouble(), uv[1].toDouble()));
                    }
                }
            }

            // 索引数据（仅保留3的倍数，确保三角面）
            if (geosetObj.contains("indices")) {
                QJsonArray indicesArray = geosetObj["indices"].toArray();
                int vertexCount = geosetData.vertices.size();
                for (const auto& idxVal : indicesArray) {
                    int idx = idxVal.toInt(-1);
                    if (idx >= 0 && idx < vertexCount) {
                        geosetData.indices.append(static_cast<unsigned int>(idx));
                    }
                }

                // 清理非3倍数的索引
                int remainder = geosetData.indices.size() % 3;
                if (remainder != 0) {
                    int removeCount = geosetData.indices.size() - (geosetData.indices.size() - remainder);
                    qWarning() << QString("几何体索引数非3的倍数，截断%1个索引").arg(removeCount);
                    geosetData.indices.resize(geosetData.indices.size() - remainder);
                }
            }

            // 蒙皮数据（骨骼索引+权重）
            if (geosetObj.contains("boneIndices") && geosetObj.contains("boneWeights")) {
                QJsonArray boneIndicesArr = geosetObj["boneIndices"].toArray();
                QJsonArray boneWeightsArr = geosetObj["boneWeights"].toArray();
                int dataCount = qMin(boneIndicesArr.size(), boneWeightsArr.size());
                int vertexCount = geosetData.vertices.size();

                // 确保蒙皮数据与顶点数匹配
                if (dataCount != vertexCount) {
                    qWarning() << QString("蒙皮数据数(%1)与顶点数(%2)不匹配，自动补全").arg(dataCount).arg(vertexCount);
                }

                for (int i = 0; i < vertexCount; ++i) {
                    QVector<int> indices(ModelParserConst::MAX_BONE_WEIGHT_COUNT, 0);
                    QVector4D weights(0.0f, 0.0f, 0.0f, 0.0f);

                    if (i < dataCount) {
                        QJsonArray idxArr = boneIndicesArr[i].toArray();
                        QJsonArray wgtArr = boneWeightsArr[i].toArray();

                        // 解析骨骼索引
                        for (int j = 0; j < qMin(ModelParserConst::MAX_BONE_WEIGHT_COUNT, idxArr.size()); ++j) {
                            indices[j] = idxArr[j].toInt(0);
                        }

                        // 解析骨骼权重
                        for (int j = 0; j < qMin(ModelParserConst::MAX_BONE_WEIGHT_COUNT, wgtArr.size()); ++j) {
                            weights[j] = wgtArr[j].toDouble(0.0f);
                        }
                    } else {
                        // 补全缺失的蒙皮数据（默认绑定到第0个骨骼）
                        weights[0] = 1.0f;
                    }

                    geosetData.boneIndices.append(indices);
                    geosetData.boneWeights.append(weights);
                }
            } else {
                // 无蒙皮数据时，默认绑定到第0个骨骼
                for (int i = 0; i < geosetData.vertices.size(); ++i) {
                    geosetData.boneIndices.append(QVector<int>(ModelParserConst::MAX_BONE_WEIGHT_COUNT, 0));
                    geosetData.boneWeights.append(QVector4D(1.0f, 0.0f, 0.0f, 0.0f));
                }
            }

            result.geosets.append(geosetData);
        }
    }

    // 14. 解析包围盒
    if (responseObj.contains("bounds")) {
        QJsonObject boundsObj = responseObj["bounds"].toObject();
        result.bounds.radius = boundsObj["radius"].toDouble(100.0f);

        QJsonArray minArr = boundsObj["min"].toArray();
        if (minArr.size() >= 3) {
            result.bounds.min = QVector3D(minArr[0].toDouble(), minArr[1].toDouble(), minArr[2].toDouble());
        }

        QJsonArray maxArr = boundsObj["max"].toArray();
        if (maxArr.size() >= 3) {
            result.bounds.max = QVector3D(maxArr[0].toDouble(), maxArr[1].toDouble(), maxArr[2].toDouble());
        }
    }

    // 15. 标记解析成功
    result.isValid = true;
    reply->deleteLater();

    qInfo() << QString("MDX解析成功，文件：%1，骨骼数：%2，动画数：%3，几何体数：%4")
                   .arg(filePath)
                   .arg(result.bones.size())
                   .arg(result.animations.size())
                   .arg(result.geosets.size());

    return result;
}

/**
 * @brief 解析MDL文件（暂未实现）
 */
ModelData ModelParser::parseMDL(const QString& filePath)
{
    Q_UNUSED(filePath);
    qWarning() << "MDL文件解析暂未实现，文件：" << filePath;
    return ModelData();
}

/**
 * @brief 获取备用立方体模型（解析失败时返回基础模型）
 */
ModelData ModelParser::getFallbackCube()
{
    ModelData fallback;
    fallback.isValid = true; // 标记为有效（基础模型）

    // 1. 基础立方体骨骼（根骨骼）
    Bone rootBone;
    rootBone.name = "Root";
    rootBone.parent = -1;
    rootBone.initScale = QVector3D(1, 1, 1);
    fallback.bones.append(rootBone);

    // 2. 基础立方体几何体
    GeosetData cubeGeoset;
    // 立方体顶点（8个顶点）
    cubeGeoset.vertices = {
        QVector3D(-1, -1, -1), QVector3D(1, -1, -1), QVector3D(1, 1, -1), QVector3D(-1, 1, -1),
        QVector3D(-1, -1, 1),  QVector3D(1, -1, 1),  QVector3D(1, 1, 1),  QVector3D(-1, 1, 1)
    };
    // 立方体UV（简化）
    cubeGeoset.uvs = {
        QVector2D(0, 0), QVector2D(1, 0), QVector2D(1, 1), QVector2D(0, 1),
        QVector2D(0, 0), QVector2D(1, 0), QVector2D(1, 1), QVector2D(0, 1)
    };
    // 立方体三角面索引（12个三角面，共36个索引）
    cubeGeoset.indices = {
        0,1,2, 0,2,3, // 后
        4,5,6, 4,6,7, // 前
        0,4,7, 0,7,3, // 左
        1,5,6, 1,6,2, // 右
        3,2,6, 3,6,7, // 上
        0,1,5, 0,5,4  // 下
    };
    // 蒙皮数据（所有顶点绑定到根骨骼）
    for (int i = 0; i < cubeGeoset.vertices.size(); ++i) {
        cubeGeoset.boneIndices.append(QVector<int>(ModelParserConst::MAX_BONE_WEIGHT_COUNT, 0));
        cubeGeoset.boneWeights.append(QVector4D(1.0f, 0.0f, 0.0f, 0.0f));
    }
    fallback.geosets.append(cubeGeoset);

    // 3. 包围盒
    fallback.bounds.radius = 1.732f; // 立方体对角线一半（√3≈1.732）
    fallback.bounds.min = QVector3D(-1, -1, -1);
    fallback.bounds.max = QVector3D(1, 1, 1);

    qInfo() << "使用备用立方体模型（解析失败）";
    return fallback;
}
