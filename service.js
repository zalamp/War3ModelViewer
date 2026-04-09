const express = require('express');
const fs = require('fs');
const path = require('path');
const os = require('os');
const { Worker, isMainThread, parentPort, threadId } = require('worker_threads');
const { parseMDX, decodeBLP, getBLPImageData } = require('war3-model');
const { PNG } = require('pngjs');
const cors = require('cors');
const crypto = require('crypto');
const rateLimit = require('express-rate-limit'); // 新增：接口限流
const ms = require('ms'); // 新增：时间格式化

// ===================== 1. 集中式常量配置（优化：统一管理，便于维护） =====================
const CONFIG = {
  // 环境
  IS_PROD: process.env.NODE_ENV === 'production',
  PORT: parseInt(process.env.PORT) || 3737,
  // 线程池
  MAX_WORKERS: parseInt(process.env.MAX_WORKERS) || Math.max(2, os.cpus().length - 1),
  WORKER_TASK_TIMEOUT: ms('30s'), // Worker任务超时时间
  // 缓存
  CACHE_MAX_SIZE: parseInt(process.env.CACHE_MAX_SIZE) || 100,
  CACHE_EXPIRE_TIME: ms('1h'), // 缓存过期时间
  CLEANUP_INTERVAL: ms('30m'), // 定时清理缓存间隔
  // 跨域
  CORS_ORIGIN: process.env.CORS_ORIGIN || (process.env.NODE_ENV === 'production' ? false : '*'),
  // 转换限制
  MAX_CONCURRENT_CONVERSIONS: 10,
  CONVERSION_TIMEOUT: ms('20s'), // BLP转换超时
  MAX_BLP_FILE_SIZE: 50 * 1024 * 1024, // BLP最大50MB，防止内存溢出
  // 路径与安全
  TEMP_DIR: path.join(os.tmpdir(), `war3_model_viewer_cache_${process.pid}`),
  STATIC_TOKEN: process.env.STATIC_TOKEN || crypto.randomBytes(16).toString('hex'), // 静态资源访问token
  RATE_LIMIT_WINDOW: ms('15m'),
  RATE_LIMIT_MAX: 100, // 15分钟最多100次请求
  // 日志
  LOG_LEVEL: process.env.LOG_LEVEL || (process.env.NODE_ENV === 'production' ? 'warn' : 'info'),
};

// ===================== 2. 工具函数（优化：拆分+完善+JSDoc） =====================
/**
 * 日志工具（分级输出）
 * @param {string} level - 日志级别: debug/info/warn/error
 * @param {string} message - 日志信息
 * @param {any} [meta] - 附加信息
 */
const logger = (level, message, meta = {}) => {
  const levels = ['debug', 'info', 'warn', 'error'];
  const currentLevelIdx = levels.indexOf(CONFIG.LOG_LEVEL);
  const logLevelIdx = levels.indexOf(level);
  if (logLevelIdx < currentLevelIdx) return;

  const timestamp = new Date().toISOString();
  const logObj = { timestamp, level, message, ...meta };
  const logStr = JSON.stringify(logObj);

  switch (level) {
    case 'error': console.error(logStr); break;
    case 'warn': console.warn(logStr); break;
    case 'info': console.info(logStr); break;
    case 'debug': console.debug(logStr); break;
  }
};

/**
 * 初始化临时目录（优化：添加权限检查）
 */
const initTempDir = () => {
  try {
    if (fs.existsSync(CONFIG.TEMP_DIR)) {
      fs.rmSync(CONFIG.TEMP_DIR, { recursive: true, force: true, maxRetries: 3 });
      logger('info', '旧临时目录已清理', { dir: CONFIG.TEMP_DIR });
    }
    fs.mkdirSync(CONFIG.TEMP_DIR, { recursive: true, mode: 0o700 }); // 仅当前用户可访问
    logger('info', '临时目录初始化完成', { dir: CONFIG.TEMP_DIR });
  } catch (e) {
    logger('error', '创建临时目录失败', { error: e.message, dir: CONFIG.TEMP_DIR });
    process.exit(1);
  }
};

/**
 * 获取文件的哈希+修改时间（优化：缓存Key包含mtime，文件更新自动失效）
 * @param {string} filePath - 文件路径
 * @returns {Promise<string|null>} 哈希+修改时间的组合Key
 */
const getFileKey = async (filePath) => {
  try {
    if (!fs.existsSync(filePath)) return null;
    const stat = await fs.promises.stat(filePath);
    const hash = crypto.createHash('md5');
    const stream = fs.createReadStream(filePath, { highWaterMark: 1024 * 1024 });

    return new Promise((resolve) => {
      stream.on('data', (chunk) => hash.update(chunk));
      stream.on('end', () => {
        const md5 = hash.digest('hex');
        const mtime = stat.mtimeMs.toString(16); // 转16进制压缩长度
        resolve(`${md5}_${mtime}`);
      });
      stream.on('error', (e) => {
        logger('warn', '计算文件哈希失败', { filePath, error: e.message });
        resolve(null);
      });
    });
  } catch (e) {
    logger('warn', '获取文件信息失败', { filePath, error: e.message });
    return null;
  }
};

/**
 * 路径校验（优化：增强防护，统一大小写）
 * @param {string} inputPath - 输入路径
 * @param {string} [baseDir] - 基准目录
 * @returns {string|null} 校验后的路径
 */
const validatePath = (inputPath, baseDir = null) => {
  if (!inputPath || typeof inputPath !== 'string') return null;

  // 统一转为小写（Windows兼容）
  let normalizedPath = path.normalize(inputPath).toLowerCase();
  // 禁止路径遍历
  if (normalizedPath.includes('..') || normalizedPath.includes(path.sep + '..') || normalizedPath.startsWith('..' + path.sep)) {
    logger('warn', '路径包含非法遍历字符', { inputPath });
    return null;
  }

  if (baseDir) {
    const resolvedBaseDir = path.resolve(baseDir).toLowerCase();
    normalizedPath = path.resolve(baseDir, normalizedPath).toLowerCase();

    if (!normalizedPath.startsWith(resolvedBaseDir + path.sep) && normalizedPath !== resolvedBaseDir) {
      logger('warn', '路径超出基准目录范围', { inputPath, baseDir });
      return null;
    }
  }

  // 检查文件是否存在且为文件
  if (fs.existsSync(normalizedPath) && !fs.statSync(normalizedPath).isFile()) {
    logger('warn', '路径不是文件', { inputPath });
    return null;
  }

  return normalizedPath;
};

/**
 * 带超时的Promise包装
 * @param {Promise} promise - 原始Promise
 * @param {number} timeout - 超时时间(ms)
 * @param {string} message - 超时提示
 * @returns {Promise} 包装后的Promise
 */
const withTimeout = (promise, timeout, message) => {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error(message)), timeout);
    promise
      .then((res) => {
        clearTimeout(timer);
        resolve(res);
      })
      .catch((err) => {
        clearTimeout(timer);
        reject(err);
      });
  });
};

// ===================== 3. LRU缓存（修复：锁机制+过期清理） =====================
class LRUCache {
  /**
   * 初始化LRU缓存
   * @param {number} maxSize - 最大缓存数
   * @param {number} expireTime - 缓存过期时间(ms)
   */
  constructor(maxSize = CONFIG.CACHE_MAX_SIZE, expireTime = CONFIG.CACHE_EXPIRE_TIME) {
    this.cache = new Map(); // key: { value: string, timestamp: number }
    this.maxSize = maxSize;
    this.expireTime = expireTime;
    this.locks = new Map(); // 锁队列: key -> Promise.resolve[]
    // 定时清理过期缓存
    this.cleanupTimer = setInterval(() => this.cleanupExpired(), CONFIG.CLEANUP_INTERVAL);
    logger('info', 'LRU缓存初始化完成', { maxSize, expireTime: ms(this.expireTime) });
  }

  /**
   * 获取缓存
   * @param {string} key - 缓存Key
   * @returns {string|null} 缓存值
   */
  get(key) {
    if (!this.cache.has(key)) return null;

    const entry = this.cache.get(key);
    // 检查是否过期
    if (Date.now() - entry.timestamp > this.expireTime) {
      this.delete(key);
      return null;
    }

    // 更新访问顺序
    this.cache.delete(key);
    this.cache.set(key, { ...entry, timestamp: Date.now() });
    return entry.value;
  }

  /**
   * 设置缓存
   * @param {string} key - 缓存Key
   * @param {string} value - 缓存值
   * @returns {Promise<void>}
   */
  async set(key, value) {
    if (this.cache.has(key)) {
      this.cache.delete(key);
    }

    // 达到上限，删除最旧的
    if (this.cache.size >= this.maxSize) {
      const oldestKey = this.cache.keys().next().value;
      await this.delete(oldestKey);
    }

    this.cache.set(key, {
      value,
      timestamp: Date.now()
    });
  }

  /**
   * 删除缓存（含文件清理）
   * @param {string} key - 缓存Key
   * @returns {Promise<void>}
   */
  async delete(key) {
    if (!this.cache.has(key)) return;

    const entry = this.cache.get(key);
    this.cache.delete(key);

    // 删除物理文件
    if (entry.value && fs.existsSync(entry.value)) {
      try {
        await fs.promises.unlink(entry.value);
        logger('info', '缓存文件已删除', { key, file: entry.value });
      } catch (e) {
        logger('warn', '删除缓存文件失败', { key, file: entry.value, error: e.message });
      }
    }
  }

  /**
   * 清空缓存
   * @returns {Promise<void>}
   */
  async clear() {
    const deletePromises = Array.from(this.cache.keys()).map(key => this.delete(key));
    await Promise.allSettled(deletePromises);
    this.cache.clear();
    logger('info', '缓存已全部清空', { count: deletePromises.length });
  }

  /**
   * 清理过期缓存
   */
  cleanupExpired() {
    const now = Date.now();
    const expiredKeys = [];

    for (const [key, entry] of this.cache.entries()) {
      if (now - entry.timestamp > this.expireTime) {
        expiredKeys.push(key);
      }
    }

    if (expiredKeys.length > 0) {
      logger('info', '清理过期缓存', { count: expiredKeys.length });
      expiredKeys.forEach(key => this.delete(key));
    }
  }

  /**
   * 互斥锁执行（修复：防止内存泄漏，完善异常处理）
   * @param {string} key - 锁Key
   * @param {Function} fn - 执行函数
   * @returns {Promise<any>}
   */
  async withLock(key, fn) {
    // 执行函数（带释放锁逻辑）
    const execute = async () => {
      try {
        return await fn();
      } finally {
        // 释放锁，执行下一个任务
        const queue = this.locks.get(key);
        if (queue && queue.length > 0) {
          const nextResolve = queue.shift();
          nextResolve();
        } else {
          this.locks.delete(key); // 无等待任务，删除锁
        }
      }
    };

    // 无锁，直接执行
    if (!this.locks.has(key)) {
      this.locks.set(key, []);
      return execute();
    }

    // 有锁，加入队列等待
    return new Promise((resolve, reject) => {
      this.locks.get(key).push(async () => {
        try {
          resolve(await fn());
        } catch (e) {
          reject(e);
        }
      });
    });
  }

  /**
   * 销毁缓存（清理定时器）
   */
  destroy() {
    clearInterval(this.cleanupTimer);
    this.clear();
  }
}

// ===================== 4. Worker线程池（优化：任务超时+优雅重启） =====================
class WorkerPool {
  /**
   * 初始化Worker池
   * @param {string} workerScript - Worker脚本路径
   * @param {number} maxWorkers - 最大Worker数
   */
  constructor(workerScript, maxWorkers = CONFIG.MAX_WORKERS) {
    this.workerScript = workerScript;
    this.maxWorkers = maxWorkers;
    this.workers = [];
    this.idleWorkers = [];
    this.taskQueue = [];
    this.isShuttingDown = false;

    this._initWorkers();
    logger('info', 'Worker池初始化完成', { maxWorkers });

    // 进程退出时终止所有Worker
    process.on('exit', () => this.terminateAll());
  }

  /**
   * 初始化Worker
   */
  _initWorkers() {
    for (let i = 0; i < this.maxWorkers; i++) {
      this._createWorker();
    }
  }

  /**
   * 创建Worker（优化：添加超时监控）
   * @returns {Worker} 新Worker实例
   */
  _createWorker() {
    const worker = new Worker(this.workerScript);
    worker.customThreadId = threadId || Math.random().toString(36).substr(2, 9); // 兼容标识
    worker.currentTask = null;
    worker.taskTimer = null; // 任务超时定时器

    // 消息处理
    worker.on('message', (result) => {
      clearTimeout(worker.taskTimer); // 清除超时定时器
      const task = worker.currentTask;
      worker.currentTask = null;

      this.idleWorkers.push(worker);
      task?.resolve(result);
      this._processNextTask();
    });

    // 错误处理
    worker.on('error', (error) => {
      logger('error', 'Worker异常', { threadId: worker.threadId, error: error.message });
      clearTimeout(worker.taskTimer);

      const task = worker.currentTask;
      worker.currentTask = null;
      task?.reject(new Error(`Worker异常: ${error.message}`));

      this._restartWorker(worker); // 重启Worker
    });

    // 退出处理
    worker.on('exit', (code) => {
      if (code !== 0 && !this.isShuttingDown) {
        logger('warn', 'Worker意外退出', { threadId: worker.threadId, code });
        this._restartWorker(worker);
      }
    });

    this.workers.push(worker);
    this.idleWorkers.push(worker);
    return worker;
  }

  /**
   * 重启Worker
   * @param {Worker} worker - 要重启的Worker
   */
  _restartWorker(worker) {
    // 移除旧Worker
    this.workers = this.workers.filter(w => w !== worker);
    this.idleWorkers = this.idleWorkers.filter(w => w !== worker);

    // 终止旧Worker
    if (!worker.isDead) {
      worker.terminate().catch(e => logger('warn', '终止异常Worker失败', { error: e.message }));
    }

    // 启动新Worker（非关闭状态）
    if (!this.isShuttingDown) {
      this._createWorker();
    }
  }

  /**
   * 处理下一个任务
   */
  _processNextTask() {
    if (this.isShuttingDown || this.idleWorkers.length === 0 || this.taskQueue.length === 0) {
      return;
    }

    const task = this.taskQueue.shift();
    const worker = this.idleWorkers.shift();

    // 设置任务超时
    worker.taskTimer = setTimeout(() => {
      logger('warn', 'Worker任务超时', { threadId: worker.threadId, taskId: task.id });
      worker.terminate(); // 终止超时Worker
      task.reject(new Error(`Worker任务超时（${ms(CONFIG.WORKER_TASK_TIMEOUT)}）`));
    }, CONFIG.WORKER_TASK_TIMEOUT);

    // 执行任务
    worker.currentTask = task;
    worker.postMessage(task.workerData);
  }

  /**
   * 提交任务
   * @param {object} workerData - 任务数据
   * @returns {Promise<any>} 任务结果
   */
  run(workerData) {
    return new Promise((resolve, reject) => {
      if (this.isShuttingDown) {
        return reject(new Error('Worker池已关闭'));
      }

      // 生成唯一任务ID（便于日志追踪）
      const taskId = crypto.randomBytes(8).toString('hex');
      logger('debug', '提交Worker任务', { taskId, workerData: { filePath: workerData.filePath } });

      this.taskQueue.push({
        id: taskId,
        workerData,
        resolve,
        reject
      });

      this._processNextTask();
    });
  }

  /**
   * 终止所有Worker
   */
  terminateAll() {
    this.isShuttingDown = true;
    logger('info', '开始终止所有Worker', { count: this.workers.length });

    // 拒绝所有待处理任务
    this.taskQueue.forEach(task => {
      task.reject(new Error('Worker池已关闭，任务取消'));
    });
    this.taskQueue = [];

    // 终止所有Worker
    this.workers.forEach(worker => {
      clearTimeout(worker.taskTimer);
      worker.terminate().catch(e => logger('warn', '终止Worker失败', { error: e.message }));
    });

    this.workers = [];
    this.idleWorkers = [];
  }
}

// ===================== 5. BLP转PNG（优化：大小限制+超时+内存优化） =====================
/**
 * BLP转PNG
 * @param {string} blpFilePath - BLP文件路径
 * @param {LRUCache} cache - 缓存实例
 * @returns {Promise<string|null>} PNG文件路径
 */
const convertBlpToPng = async (blpFilePath, cache) => {
  // 基础校验
  if (!blpFilePath || !fs.existsSync(blpFilePath)) {
    logger('warn', 'BLP文件不存在', { filePath: blpFilePath });
    return null;
  }

  // 文件大小限制
  const stat = await fs.promises.stat(blpFilePath);
  if (stat.size > CONFIG.MAX_BLP_FILE_SIZE) {
    logger('warn', 'BLP文件超出大小限制', { filePath: blpFilePath, size: stat.size, maxSize: CONFIG.MAX_BLP_FILE_SIZE });
    return null;
  }

  // 获取缓存Key
  const cacheKey = await getFileKey(blpFilePath);
  if (!cacheKey) return null;

  // 检查缓存
  const cachedPath = cache.get(cacheKey);
  if (cachedPath && fs.existsSync(cachedPath)) {
    logger('debug', '使用BLP缓存', { filePath: blpFilePath, cacheKey });
    return cachedPath;
  }

  // 加锁转换（带超时）
  return cache.withLock(cacheKey, async () => {
    // 二次检查缓存（防止并发重复转换）
    const doubleCheck = cache.get(cacheKey);
    if (doubleCheck && fs.existsSync(doubleCheck)) return doubleCheck;

    try {
      // 读取BLP文件（带超时）
      const nodeBuffer = await withTimeout(
        fs.promises.readFile(blpFilePath),
        CONFIG.CONVERSION_TIMEOUT,
        `读取BLP文件超时: ${blpFilePath}`
      );

      // 解码BLP
      const arrayBuffer = nodeBuffer.buffer.slice(nodeBuffer.byteOffset, nodeBuffer.byteOffset + nodeBuffer.byteLength);
      const blp = decodeBLP(arrayBuffer);
      const imageData = getBLPImageData(blp, 0);

      // 生成PNG（优化：降低压缩级别，减少内存占用）
      const png = new PNG({
        width: blp.width,
        height: blp.height,
        inputHasAlpha: true,
        filterType: -1 // 自动选择最优过滤
      });
      png.data = Buffer.from(imageData.data);
      const pngBuffer = PNG.sync.write(png, { deflateLevel: 4 }); // 原6→4，平衡压缩/内存

      // 写入临时文件
      const pngFileName = `${cacheKey}_${Date.now()}.png`;
      const pngPath = path.join(CONFIG.TEMP_DIR, pngFileName);
      await fs.promises.writeFile(pngPath, pngBuffer, { mode: 0o600 });

      // 更新缓存
      await cache.set(cacheKey, pngPath);
      logger('info', 'BLP转换完成', { original: path.basename(blpFilePath), pngPath });

      return pngPath;
    } catch (err) {
      logger('error', 'BLP转换失败', { filePath: blpFilePath, error: err.message });
      return null;
    }
  });
};

// ===================== 6. MDX解析子函数（Worker内使用，优化：拆分长函数） =====================
/**
 * 解析骨骼数据
 * @param {object} model - MDX模型数据
 * @returns {array} 骨骼数组
 */
const parseBones = (model) => {
  const bones = [];
  if (!model.Bones || !Array.isArray(model.Bones)) return bones;

  for (let b = 0; b < model.Bones.length; b++) {
    const bone = model.Bones[b];
    bones.push({
      name: bone.Name || `bone_${b}`,
      parent: bone.Parent !== undefined ? bone.Parent : -1,
      pivot: bone.PivotPoint || [0, 0, 0],
      initRotation: bone.Rotation?.Value || [0, 0, 0, 1],
      initTranslation: bone.Translation?.Value || [0, 0, 0],
      initScale: bone.Scale?.Value || [1, 1, 1]
    });
  }

  logger('debug', '解析骨骼完成', { count: bones.length });
  return bones;
};

/**
 * 解析动画数据
 * @param {object} model - MDX模型数据
 * @returns {array} 动画数组
 */
const parseAnimations = (model) => {
  const animations = [];
  if (!model.Sequences || !Array.isArray(model.Sequences)) return animations;

  for (let s = 0; s < model.Sequences.length; s++) {
    const seq = model.Sequences[s];
    let start = 0, end = 1000;

    if (seq.Interval && seq.Interval.length >= 2) {
      start = seq.Interval[0];
      end = seq.Interval[1];
    } else if (seq.IntervalStart !== undefined && seq.IntervalEnd !== undefined) {
      start = seq.IntervalStart;
      end = seq.IntervalEnd;
    }

    animations.push({
      name: seq.Name || `anim_${s}`,
      start,
      end,
      moveSpeed: seq.MoveSpeed || 0,
      rarity: seq.Rarity || 0,
      flags: seq.Flags || 0
    });
  }

  logger('debug', '解析动画完成', { count: animations.length });
  return animations;
};

/**
 * 解析几何体数据
 * @param {object} model - MDX模型数据
 * @returns {object} { geosets: 几何体数组, bounds: 模型边界 }
 */
const parseGeosets = (model) => {
  const geosets = [];
  const bounds = {
    radius: 100,
    min: [Infinity, Infinity, Infinity],
    max: [-Infinity, -Infinity, -Infinity],
    center: [0, 0, 0]
  };

  if (!model.Geosets || !Array.isArray(model.Geosets)) return { geosets, bounds };

  for (let g = 0; g < model.Geosets.length; g++) {
    const geoset = model.Geosets[g];
    const geosetData = {
      vertices: [],
      uvs: [],
      indices: [],
      materialId: geoset.MaterialId || geoset.MaterialID || 0,
      boneIndices: [],
      boneWeights: []
    };

    // 解析顶点
    if (geoset.Vertices) {
      const vertArray = Array.from(geoset.Vertices);
      for (let i = 0; i < vertArray.length; i += 3) {
        if (i + 2 >= vertArray.length) {
          logger('warn', '顶点数据不完整', { geosetIndex: g, vertexIndex: i });
          continue;
        }
        const v = [vertArray[i], vertArray[i+1], vertArray[i+2]];
        geosetData.vertices.push(v);

        // 更新边界
        bounds.min[0] = Math.min(bounds.min[0], v[0]);
        bounds.min[1] = Math.min(bounds.min[1], v[1]);
        bounds.min[2] = Math.min(bounds.min[2], v[2]);
        bounds.max[0] = Math.max(bounds.max[0], v[0]);
        bounds.max[1] = Math.max(bounds.max[1], v[1]);
        bounds.max[2] = Math.max(bounds.max[2], v[2]);
      }
    }

    // 解析UV
    if (geoset.TVertices) {
      let uvArray = [];
      if (Array.isArray(geoset.TVertices) && geoset.TVertices.length > 0) {
        uvArray = Array.from(geoset.TVertices[0]);
      } else {
        uvArray = Array.from(geoset.TVertices);
      }

      for (let i = 0; i < uvArray.length; i += 2) {
        if (i + 1 >= uvArray.length) {
          logger('warn', 'UV数据不完整', { geosetIndex: g, uvIndex: i });
          continue;
        }
        geosetData.uvs.push([uvArray[i], uvArray[i+1]]);
      }
    }

    // 解析索引
    if (geoset.Faces) {
      geosetData.indices = Array.from(geoset.Faces);
    }

    // 解析骨骼权重
    const vertexCount = geosetData.vertices.length;
    let hasBoneData = false;
    if (geoset.VertexGroup) {
      const vgArray = Array.from(geoset.VertexGroup);
      if (vgArray.length > 0) {
        hasBoneData = true;
        for (let i = 0; i < vertexCount; i++) {
          const boneIdx = i < vgArray.length ? vgArray[i] : 0;
          geosetData.boneIndices.push([boneIdx, 0, 0, 0]);
          geosetData.boneWeights.push([1.0, 0, 0, 0]);
        }
      }
    }

    // 无骨骼数据，默认值
    if (!hasBoneData) {
      for (let i = 0; i < vertexCount; i++) {
        geosetData.boneIndices.push([0, 0, 0, 0]);
        geosetData.boneWeights.push([1.0, 0, 0, 0]);
      }
    }

    geosets.push(geosetData);
  }

  // 计算边界中心和半径
  bounds.center = [
    (bounds.min[0] + bounds.max[0]) / 2,
    (bounds.min[1] + bounds.max[1]) / 2,
    (bounds.min[2] + bounds.max[2]) / 2
  ];

  let maxDist = 0;
  for (const geo of geosets) {
    for (const vert of geo.vertices) {
      const dist = Math.sqrt(
        Math.pow(vert[0] - bounds.center[0], 2) +
        Math.pow(vert[1] - bounds.center[1], 2) +
        Math.pow(vert[2] - bounds.center[2], 2)
      );
      maxDist = Math.max(maxDist, dist);
    }
  }
  bounds.radius = maxDist;

  logger('debug', '解析几何体完成', { count: geosets.length, bounds });
  return { geosets, bounds };
};

/**
 * 解析纹理路径
 * @param {object} model - MDX模型数据
 * @param {string} mdxDir - MDX文件目录
 * @param {string} war3Path - War3根路径
 * @returns {array} 纹理数组
 */
const parseTextures = async (model, mdxDir, war3Path) => {
  const textures = [];
  if (!model.Textures || !Array.isArray(model.Textures)) return textures;

  for (let t = 0; t < model.Textures.length; t++) {
    const tex = model.Textures[t];
    let rawPath = tex.Image || tex.image || tex.FileName || tex.fileName || "";
    rawPath = rawPath.replace(/\\/g, '/').toLowerCase();
    const justFileName = path.basename(rawPath);

    // 纹理路径候选列表
    const candidates = [
      path.join(mdxDir, justFileName),
      path.join(mdxDir, rawPath),
      path.join(mdxDir, 'textures', justFileName)
    ];

    if (war3Path) {
      candidates.push(
        path.join(war3Path, justFileName),
        path.join(war3Path, 'textures', justFileName),
        path.join(war3Path, rawPath)
      );
    }

    // 查找存在的纹理文件
    let foundPath = null;
    for (const candidate of Array.from(new Set(candidates))) {
      try {
        const stat = await fs.promises.stat(candidate);
        if (stat.isFile()) {
          foundPath = candidate;
          break;
        }
      } catch (e) {
        continue;
      }
    }

    textures.push({ original: rawPath, foundPath });
  }

  logger('debug', '解析纹理完成', { count: textures.length, found: textures.filter(t => t.foundPath).length });
  return textures;
};

/**
 * 解析骨骼关键帧
 * @param {object} model - MDX模型数据
 * @param {array} bones - 骨骼数组
 * @returns {object} 关键帧数据
 */
const parseKeyframes = (model, bones) => {
  const keyframes = {};
  for (let i = 0; i < bones.length; i++) {
    keyframes[i] = { translation: [], rotation: [], scale: [] };
  }

  if (!model.Bones || !Array.isArray(model.Bones)) return keyframes;

  for (let boneIdx = 0; boneIdx < model.Bones.length; boneIdx++) {
    const bone = model.Bones[boneIdx];
    if (boneIdx >= bones.length) continue;

    // 旋转关键帧
    if (bone.Rotation?.Keys && Array.isArray(bone.Rotation.Keys)) {
      for (const kf of bone.Rotation.Keys) {
        const frame = kf.Frame || 0;
        const vec = kf.Vector;
        if (vec && vec.length >= 4) {
          keyframes[boneIdx].rotation.push({ time: frame, value: [vec[0], vec[1], vec[2], vec[3]] });
        }
      }
    }

    // 平移关键帧
    if (bone.Translation?.Keys && Array.isArray(bone.Translation.Keys)) {
      for (const kf of bone.Translation.Keys) {
        const frame = kf.Frame || 0;
        const vec = kf.Vector;
        if (vec && vec.length >= 3) {
          keyframes[boneIdx].translation.push({ time: frame, value: [vec[0], vec[1], vec[2]] });
        }
      }
    }

    // 缩放关键帧
    if (bone.Scale?.Keys && Array.isArray(bone.Scale.Keys)) {
      for (const kf of bone.Scale.Keys) {
        const frame = kf.Frame || 0;
        const vec = kf.Vector;
        if (vec && vec.length >= 3) {
          keyframes[boneIdx].scale.push({ time: frame, value: [vec[0], vec[1], vec[2]] });
        }
      }
    }
  }

  logger('debug', '解析关键帧完成', { boneCount: bones.length });
  return keyframes;
};

// ===================== 7. 主线程：Express服务（优化：安全+限流+统一错误处理） =====================
if (isMainThread) {
  // 初始化资源
  initTempDir();
  const textureCache = new LRUCache(CONFIG.CACHE_MAX_SIZE, CONFIG.CACHE_EXPIRE_TIME);
  const workerPool = new WorkerPool(__filename, CONFIG.MAX_WORKERS);

  // 创建Express实例
  const app = express();

  // 跨域配置
  app.use(cors({
    origin: CONFIG.CORS_ORIGIN,
    methods: ['GET', 'POST'],
    allowedHeaders: ['Content-Type', 'X-Static-Token'],
    credentials: true
  }));

  // 接口限流
  const limiter = rateLimit({
    windowMs: CONFIG.RATE_LIMIT_WINDOW,
    max: CONFIG.RATE_LIMIT_MAX,
    standardHeaders: true,
    legacyHeaders: false,
    message: { success: false, error: '请求频率过高，请稍后再试' },
    handler: (req, res) => {
      logger('warn', '请求限流触发', { ip: req.ip, path: req.path });
      res.status(429).json({ success: false, error: '请求频率过高，请15分钟后重试' });
    }
  });
  app.use(limiter);

  // 解析请求体
  app.use(express.json({ limit: '50mb' }));
  app.use(express.urlencoded({ extended: true, limit: '50mb' }));

  // 静态资源访问（优化：添加token验证）
  app.use('/textures', (req, res, next) => {
    const token = req.headers['x-static-token'] || req.query.token;
    if (token !== CONFIG.STATIC_TOKEN) {
      logger('warn', '静态资源访问未授权', { ip: req.ip, path: req.path });
      return res.status(403).json({ success: false, error: '访问未授权' });
    }
    next();
  }, express.static(CONFIG.TEMP_DIR, {
    maxAge: CONFIG.IS_PROD ? '1d' : 0,
    fallthrough: false,
    onError: (err, req, res) => {
      logger('warn', '静态资源访问错误', { path: req.path, error: err.message });
      res.status(404).json({ success: false, error: '文件不存在' });
    }
  }));

  // BLP转换并发限制（优化：添加超时）
  const conversionLimiter = (() => {
    let running = 0;
    const queue = [];

    const run = async (fn) => {
      return new Promise((resolve, reject) => {
        // 超时处理
        const timer = setTimeout(() => {
          reject(new Error(`BLP转换排队超时（${ms(CONFIG.CONVERSION_TIMEOUT)}）`));
        }, CONFIG.CONVERSION_TIMEOUT);

        // 执行逻辑
        const execute = async () => {
          clearTimeout(timer);
          running++;
          try {
            resolve(await fn());
          } catch (e) {
            reject(e);
          } finally {
            running--;
            if (queue.length > 0) {
              queue.shift()();
            }
          }
        };

        // 并发控制
        if (running >= CONFIG.MAX_CONCURRENT_CONVERSIONS) {
          queue.push(execute);
        } else {
          execute();
        }
      });
    };

    return { run };
  })();

  // ===================== 接口定义 =====================
  /**
   * @api {post} /parse 解析MDX模型
   * @apiParam {string} filePath MDX文件路径
   * @apiParam {string} [war3Path] War3根路径
   */
  app.post('/parse', async (req, res, next) => {
    try {
      const { filePath, war3Path } = req.body;
      logger('info', '开始解析MDX模型', { filePath, ip: req.ip });

      // 路径校验
      const validatedFilePath = validatePath(filePath);
      const validatedWar3Path = war3Path ? validatePath(war3Path) : "";

      if (!validatedFilePath) {
        return res.status(400).json({ success: false, error: '文件路径无效' });
      }

      if (!fs.existsSync(validatedFilePath)) {
        return res.status(400).json({ success: false, error: '文件不存在' });
      }

      if (path.extname(validatedFilePath).toLowerCase() !== '.mdx') {
        return res.status(400).json({ success: false, error: '仅支持.mdx格式' });
      }

      // Worker解析MDX（带超时）
      const parseResult = await withTimeout(
        workerPool.run({ filePath: validatedFilePath, war3Path: validatedWar3Path }),
        CONFIG.WORKER_TASK_TIMEOUT,
        'MDX解析超时'
      );

      if (!parseResult.success) {
        return res.status(400).json(parseResult);
      }

      // 转换BLP为PNG
      const textureConvertPromises = parseResult.textures.map(async (tex) => {
        if (!tex.foundPath) return { original: tex.original, pngPath: null, pngUrl: null };

        const pngPath = await conversionLimiter.run(() => convertBlpToPng(tex.foundPath, textureCache));
        return {
          original: tex.original,
          pngPath,
          pngUrl: pngPath ? `/textures/${path.basename(pngPath)}?token=${CONFIG.STATIC_TOKEN}` : null
        };
      });

      parseResult.textures = await Promise.all(textureConvertPromises);
      delete parseResult.mdxDir;

      logger('info', 'MDX解析完成', {
        filePath: path.basename(validatedFilePath),
        bones: parseResult.bones.length,
        animations: parseResult.animations.length,
        geosets: parseResult.geosets.length
      });

      return res.json({ success: true, ...parseResult });
    } catch (error) {
      next(error);
    }
  });

  /**
   * @api {post} /clear-cache 清理缓存
   */
  app.post('/clear-cache', async (req, res, next) => {
    try {
      logger('info', '清理缓存请求', { ip: req.ip });
      await textureCache.clear();
      initTempDir(); // 重建临时目录
      res.json({ success: true, message: '缓存已清理' });
    } catch (e) {
      next(e);
    }
  });

  /**
   * @api {get} /health 健康检查
   */
  app.get('/health', (req, res) => {
    const healthInfo = {
      status: 'ok',
      timestamp: new Date().toISOString(),
      port: CONFIG.PORT,
      env: CONFIG.IS_PROD ? 'production' : 'development',
      cache: {
        size: textureCache.cache.size,
        maxSize: CONFIG.CACHE_MAX_SIZE,
        expireTime: ms(CONFIG.CACHE_EXPIRE_TIME)
      },
      worker: {
        max: CONFIG.MAX_WORKERS,
        idle: workerPool.idleWorkers.length,
        pendingTasks: workerPool.taskQueue.length
      },
      tempDir: CONFIG.IS_PROD ? 'hidden' : CONFIG.TEMP_DIR
    };
    res.json(healthInfo);
  });

  // ===================== 错误处理 =====================
  // 404处理
  app.use((req, res) => {
    logger('warn', '接口不存在', { path: req.path, ip: req.ip });
    res.status(404).json({ success: false, error: '接口不存在' });
  });

  // 全局错误处理
  app.use((error, req, res, next) => {
    logger('error', '服务异常', {
      path: req.path,
      ip: req.ip,
      error: error.message,
      stack: CONFIG.IS_PROD ? 'hidden' : error.stack
    });

    res.status(500).json({
      success: false,
      error: CONFIG.IS_PROD ? '服务内部错误' : error.message
    });
  });

  // ===================== 优雅退出 =====================
  const cleanup = async () => {
    logger('info', '收到退出信号，开始清理资源');

    // 终止Worker池
    workerPool.terminateAll();

    // 清空缓存
    await textureCache.clear();
    textureCache.destroy();

    // 删除临时目录
    if (fs.existsSync(CONFIG.TEMP_DIR)) {
      try {
        fs.rmSync(CONFIG.TEMP_DIR, { recursive: true, force: true });
        logger('info', '临时目录已删除');
      } catch (e) {
        logger('warn', '删除临时目录失败', { error: e.message });
      }
    }

    logger('info', '资源清理完成，退出进程');
    process.exit(0);
  };

  // 监听退出信号
  process.on('SIGINT', cleanup);
  process.on('SIGTERM', cleanup);
  process.on('uncaughtException', async (error) => {
    logger('error', '未捕获异常', { error: error.message, stack: error.stack });
    await cleanup();
    process.exit(1);
  });
  process.on('unhandledRejection', async (reason) => {
    logger('error', '未处理的Promise拒绝', { reason: reason?.message || reason });
    await cleanup();
    process.exit(1);
  });

  // 启动服务
  app.listen(CONFIG.PORT, () => {
    logger('info', 'MDX模型解析服务启动成功', {
      port: CONFIG.PORT,
      env: CONFIG.IS_PROD ? 'production' : 'development',
      maxWorkers: CONFIG.MAX_WORKERS,
      staticToken: CONFIG.STATIC_TOKEN // 开发环境输出token，生产环境可隐藏
    });

    console.log('========================================');
    console.log('MDX模型解析服务 (生产级优化版)');
    console.log(`服务地址: http://127.0.0.1:${CONFIG.PORT}`);
    console.log(`运行环境: ${CONFIG.IS_PROD ? '生产' : '开发'}`);
    console.log(`静态资源Token: ${CONFIG.STATIC_TOKEN}`);
    console.log('========================================');
  });

// ===================== 8. Worker线程：MDX解析核心 =====================
} else {
  // Worker线程只处理解析逻辑
  parentPort.on('message', async (workerData) => {
    const { filePath, war3Path } = workerData;
    const mdxDir = path.dirname(filePath);
    let result = { success: false, error: '未知错误' };

    try {
      logger('debug', 'Worker开始解析MDX', { threadId, filePath });

      // 读取MDX文件
      const nodeBuffer = await fs.promises.readFile(filePath);
      const arrayBuffer = nodeBuffer.buffer.slice(nodeBuffer.byteOffset, nodeBuffer.byteOffset + nodeBuffer.byteLength);
      const model = parseMDX(arrayBuffer);

      // 解析各部分数据
      const bones = parseBones(model);
      const animations = parseAnimations(model);
      const { geosets, bounds } = parseGeosets(model);
      const textures = await parseTextures(model, mdxDir, war3Path);
      const keyframes = parseKeyframes(model, bones);

      // 材质映射
      const materialToTexture = [];
      const materials = [];
      if (model.Materials && model.Materials.length > 0) {
        for (let m = 0; m < model.Materials.length; m++) {
          const mat = model.Materials[m];
          const layers = [];
          let firstTexId = 0;

          if (mat.Layers && mat.Layers.length > 0) {
            firstTexId = mat.Layers[0].TextureId || mat.Layers[0].TextureID || 0;
            for (let l = 0; l < mat.Layers.length; l++) {
              const layer = mat.Layers[l];
              layers.push({
                textureId: layer.TextureId || layer.TextureID || 0,
                filterMode: layer.FilterMode || 0,
                alpha: layer.Alpha || 1.0,
                shading: layer.Shading || 0,
                flags: layer.Flags || 0
              });
            }
          }

          materialToTexture.push(firstTexId);
          materials.push({ layers });
        }
      }

      // 组装结果
      result = {
        success: true,
        bones,
        animations,
        keyframes,
        geosets,
        materialToTexture,
        materials,
        textures,
        bounds,
        mdxDir
      };

      logger('debug', 'Worker解析MDX完成', { threadId, filePath });
    } catch (error) {
      logger('error', 'Worker解析MDX失败', { threadId, filePath, error: error.message });
      result = { success: false, error: error.message || 'MDX解析失败' };
    } finally {
      // 回传结果
      parentPort.postMessage(result);
    }
  });
}
