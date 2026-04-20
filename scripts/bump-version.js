#!/usr/bin/env node
/*
 * bump-version.js
 * ----------------------------------------------------------------
 * Deploy бүрт ажиллана. Хийх зүйл:
 *   1. Шинэ version string үүсгэнэ (timestamp + git commit short hash)
 *   2. web_dashboard/version.json бичнэ → client poll хийх манифест
 *   3. HTML файлуудад <meta name="app-version"> утгыг replace хийнэ
 *
 * Usage: node scripts/bump-version.js
 * ----------------------------------------------------------------
 */

const fs   = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const ROOT     = path.resolve(__dirname, '..');
const WEB_DIR  = path.join(ROOT, 'web_dashboard');
const HTML_FILES = ['index.html', 'login.html', '404.html'];

// ---------- 1. Version string үүсгэх ----------
function getGitShortSha() {
  try {
    return execSync('git rev-parse --short HEAD', { cwd: ROOT }).toString().trim();
  } catch {
    return 'nogit';
  }
}

const now        = new Date();
const timestamp  = now.toISOString();                      // "2026-04-20T17:30:00.000Z"
const buildNum   = Math.floor(now.getTime() / 1000);       // seconds since epoch
const commitSha  = getGitShortSha();
const version    = `${timestamp.slice(0, 16).replace(/[:T-]/g, '')}-${commitSha}`;
//                    "202604201730-a8f3e21"

// ---------- 2. version.json бичих ----------
const manifest = {
  version:   version,
  buildTime: buildNum,
  commit:    commitSha,
  deployed:  timestamp
};

const manifestPath = path.join(WEB_DIR, 'version.json');
fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n');
console.log(`✓ ${path.relative(ROOT, manifestPath)} — ${version}`);

// ---------- 3. HTML файлуудад meta tag update ----------
const META_REGEX = /<meta name="app-version" content="[^"]*">/;
const META_NEW   = `<meta name="app-version" content="${version}">`;

for (const file of HTML_FILES) {
  const filePath = path.join(WEB_DIR, file);
  if (!fs.existsSync(filePath)) continue;

  let html = fs.readFileSync(filePath, 'utf8');

  if (META_REGEX.test(html)) {
    html = html.replace(META_REGEX, META_NEW);
  } else {
    // Meta tag байхгүй бол <head> дотор нэмнэ
    html = html.replace(/<head>/, `<head>\n  ${META_NEW}`);
  }

  fs.writeFileSync(filePath, html);
  console.log(`✓ ${path.relative(ROOT, filePath)}`);
}

console.log(`\nVersion bumped: ${version}`);
