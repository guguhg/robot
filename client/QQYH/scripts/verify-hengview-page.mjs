#!/usr/bin/env node

/**
 * /hengview 页面静态结构验证
 * 用于确认路由迁移后，页面、场景内核与维护文档仍保持一致。
 */

import { readFile, stat } from 'fs/promises'
import { join, dirname } from 'path'
import { fileURLToPath } from 'url'

const __dirname = dirname(fileURLToPath(import.meta.url))
const rootDir = join(__dirname, '..', '..')
const frontDir = join(rootDir, 'Front')

const checks = {
  passed: [],
  failed: [],
  warnings: [],
}

const pass = (message) => {
  checks.passed.push(message)
  console.log(`PASS ${message}`)
}

const fail = (message) => {
  checks.failed.push(message)
  console.error(`FAIL ${message}`)
}

const warn = (message) => {
  checks.warnings.push(message)
  console.warn(`WARN ${message}`)
}

const readFrontFile = (path) => readFile(join(frontDir, path), 'utf-8')

async function checkFile(path, description) {
  try {
    await readFrontFile(path)
    pass(`${description} 存在`)
    return true
  } catch {
    fail(`${description} 不存在: ${path}`)
    return false
  }
}

async function checkAsset(path, description) {
  try {
    const asset = await stat(join(frontDir, path))
    if (asset.isFile() && asset.size > 0) {
      pass(`${description} 存在且非空`)
    } else {
      fail(`${description} 不是有效文件: ${path}`)
    }
  } catch {
    fail(`${description} 不存在: ${path}`)
  }
}

function checkTokens(content, description, tokens) {
  for (const token of tokens) {
    if (content.includes(token)) {
      pass(`${description} 包含 ${token}`)
    } else {
      fail(`${description} 缺少 ${token}`)
    }
  }
}

async function checkStructure() {
  console.log('\n检查页面结构\n')

  const pageReady = await checkFile('pages/hengview.vue', '/hengview 页面')
  const sceneReady = await checkFile('lib/hengview/hengview-scene.ts', 'HengviewScene 内核')
  await checkFile('lib/hengview/hengview-materials.ts', 'HENGVIEW 材质模块')
  await checkFile('lib/hengview/hengview-post.ts', 'HENGVIEW 后处理模块')
  await checkFile('components/hengview/HengviewScrollRail.vue', 'HENGVIEW 滚动轨道')
  await checkAsset('public/models/mzz01-cq-explode.glb', 'HENGVIEW 运行时模型')

  if (pageReady) {
    const page = await readFrontFile('pages/hengview.vue')
    checkTokens(page, '页面', [
      '<script setup lang="ts">',
      'HengviewScene',
      'HengviewScrollRail',
      'beginBoot',
      'prefers-reduced-motion',
      'onBeforeUnmount',
    ])
  }

  if (sceneReady) {
    const scene = await readFrontFile('lib/hengview/hengview-scene.ts')
    checkTokens(scene, '场景内核', [
      'GLTFLoader',
      'MeshoptDecoder',
      'ResizeObserver',
      'visibilitychange',
      'dispose()',
    ])
  }
}

async function checkDocumentation() {
  console.log('\n检查迁移后文档与配置\n')

  try {
    const rootReadme = await readFile(join(rootDir, 'README.md'), 'utf-8')
    const frontReadme = await readFrontFile('README.md')
    const packageMetadata = JSON.parse(await readFrontFile('package.json'))
    const agentDoc = await readFile(join(rootDir, 'agent.md'), 'utf-8')

    checkTokens(rootReadme, '根 README', [
      'qiaoqiaoyouhe.cloud',
      'Front/',
      'Media/',
      'Model/',
      '/hengview',
    ])
    checkTokens(frontReadme, 'Front README', [
      'pnpm install --frozen-lockfile',
      'pnpm typecheck',
      'pnpm build',
      'pnpm verify:hengview',
    ])

    if (agentDoc.includes('Front/pages/hengview.vue')) {
      pass('agent.md 已登记 HENGVIEW 当前入口')
    } else {
      warn('agent.md 未登记 HENGVIEW 当前入口')
    }

    if (packageMetadata.name === 'qiaoqiaoyouhe-cloud-front') {
      pass('package.json 已使用当前项目包名')
    } else {
      fail('package.json 仍使用迁移前包名')
    }

    if (packageMetadata.packageManager === 'pnpm@11.13.1') {
      pass('package.json 已固定 pnpm 版本')
    } else {
      fail('package.json 未固定当前 pnpm 版本')
    }
  } catch (error) {
    fail(`文档检查失败: ${error.message}`)
  }
}

async function main() {
  console.log('/hengview 页面验证\n')
  console.log('='.repeat(50))

  await checkStructure()
  await checkDocumentation()

  console.log(`\n通过: ${checks.passed.length}`)
  console.log(`失败: ${checks.failed.length}`)
  console.log(`警告: ${checks.warnings.length}`)

  process.exitCode = checks.failed.length === 0 ? 0 : 1
}

main().catch((error) => {
  console.error('验证脚本执行失败:', error)
  process.exitCode = 1
})
